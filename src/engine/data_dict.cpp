#include "engine/data_dict.h"

#include "platform/file.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>

namespace openads::engine {

namespace {

// Split a string on NUL bytes (used for binary-record property encoding).
static std::vector<std::string> split_nul(const std::string& s) {
    std::vector<std::string> parts;
    std::size_t pos = 0;
    while (pos <= s.size()) {
        auto next = s.find('\0', pos);
        if (next == std::string::npos) {
            parts.push_back(s.substr(pos));
            break;
        }
        parts.push_back(s.substr(pos, next - pos));
        pos = next + 1;
    }
    return parts;
}

// Join strings with NUL separators, capped at max_len bytes total.
static std::string join_nul(std::initializer_list<std::string> parts,
                              std::size_t max_len = 273) {
    std::string result;
    bool first = true;
    for (const auto& p : parts) {
        if (!first) result += '\0';
        first = false;
        result += p;
        if (result.size() >= max_len) { result.resize(max_len); break; }
    }
    return result;
}

static inline uint32_t le32(const std::string& b, std::size_t off) {
    return static_cast<uint32_t>(
        static_cast<uint32_t>(static_cast<uint8_t>(b[off]))
      | (static_cast<uint32_t>(static_cast<uint8_t>(b[off+1])) << 8)
      | (static_cast<uint32_t>(static_cast<uint8_t>(b[off+2])) << 16)
      | (static_cast<uint32_t>(static_cast<uint8_t>(b[off+3])) << 24));
}

static inline uint16_t le16(const std::string& b, std::size_t off) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(static_cast<uint8_t>(b[off]))
      | static_cast<uint16_t>(static_cast<uint16_t>(static_cast<uint8_t>(b[off+1])) << 8));
}

static void put_le32(std::string& b, std::size_t off, uint32_t v) {
    b[off+0] = static_cast<char>( v        & 0xFFu);
    b[off+1] = static_cast<char>((v >>  8) & 0xFFu);
    b[off+2] = static_cast<char>((v >> 16) & 0xFFu);
    b[off+3] = static_cast<char>((v >> 24) & 0xFFu);
}

static void put_le16(std::string& b, std::size_t off, uint16_t v) {
    b[off+0] = static_cast<char>( v       & 0xFFu);
    b[off+1] = static_cast<char>((v >> 8) & 0xFFu);
}

std::string trim(std::string s) {
    auto is_ws = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    std::size_t b = 0, e = s.size();
    while (b < e && is_ws(s[b])) ++b;
    while (e > b && is_ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

} // namespace

util::Result<DataDict> DataDict::open(const std::string& path) {
    DataDict dd;
    dd.path_ = path;
    if (auto r = dd.load_(); !r) return r.error();
    return dd;
}

util::Result<DataDict> DataDict::create(const std::string& path) {
    auto fres = platform::File::open(path, platform::OpenMode::CreateRW);
    if (!fres) return fres.error();
    auto file = std::move(fres).value();
    const char* magic = "# OpenADS Data Dictionary v1\n";
    auto wrote = file.write_at(0, magic, std::strlen(magic));
    if (!wrote) return wrote.error();
    if (auto s = file.sync(); !s) return s.error();
    DataDict dd;
    dd.path_ = path;
    return dd;
}

namespace {
std::vector<std::string> split_tabs(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\t') { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}
}  // namespace

// Trim trailing spaces and embedded nulls from a fixed-length CHAR field.
static std::string trim_char(const std::string& buf,
                              std::size_t off, std::size_t maxlen) {
    std::string s = buf.substr(off, maxlen);
    auto p = s.find_last_not_of(" \0", std::string::npos, 2);
    return (p == std::string::npos) ? "" : s.substr(0, p + 1);
}

util::Result<void> DataDict::load_add_binary_(const std::string& buf) {
    // =========================================================================
    // ADS binary .add file format.
    //
    // The .add file is an Advantage Data Table (ADT) whose header (variable
    // length, field at 0x20) describes the column layout, and whose body is a
    // fixed-length record array.
    //
    // Record layout (rec_len = 524 bytes):
    //   [0]        status: 0x04=active, 0x05=deleted
    //   [1..4]     null bitmap (zero in all observed files)
    //   [5..8]     Object ID   (uint32 LE, AutoInc)
    //   [9..12]    Parent ID   (uint32 LE)
    //   [13..22]   Object Type (CHAR(10), space-padded)
    //   [23..222]  Object Name (CHAR(200), space-padded)
    //   [223..497] Property    (VarChar(275): uint16 LE plen at [223], data at
    //                           [225..225+plen-1]; 0xFFFF = SQL NULL)
    //   [498..506] More Property (Binary(9), memo block pointer for overflow)
    //   [507..510] Info1       (uint32 LE)
    //   [511..514] Info2       (uint32 LE)
    //   [515..523] Comment     (Memo(9), block pointer)
    //
    // Header fields used for write-back:
    //   0x18-0x1B: total record count (active + deleted)
    //   0x50-0x53: next ObjID to assign (= max active ObjID + 1)
    //
    // -------------------------------------------------------------------------
    // Property VarChar layout (275 bytes = 2-byte plen + 273 data bytes):
    //
    //   The 273-byte data area is split into two zones:
    //   - [0 .. plen-1]    object-type-specific inline data (varies by type)
    //   - [plen .. 272]    extra area (mostly 0xFF padding; for User records
    //                       this zone holds the group-membership token table)
    //
    //   For User records the extra area starts at offset plen and has the
    //   following layout (reversed-engineered from the ACE binary format):
    //
    //     [plen+0 .. plen+1]  count_field (uint16 LE)
    //                           0xFFFF  → no property-byte groups
    //                           N×4     → N groups are stored
    //     [plen+2 .. plen+2+N×4-1]
    //                         N×4-byte group tokens (see Third pass below)
    //     [...]               0x00 0x00 end marker + 0xFF padding
    //
    // -------------------------------------------------------------------------
    // Group-membership storage — two mechanisms coexist in the same file:
    //
    //   1. Permission records (parent_id=user, info1=group, info2=0x80000000):
    //      Written by AdsDDAddUserToGroup in SAP ACE v8+ and by OpenADS for all
    //      new memberships.  Decoded in the Second pass below.
    //
    //   2. User property-byte XOR tokens:
    //      An older per-database XOR encoding in the User property extra area.
    //      Present in databases maintained by pre-v8 SAP ACE or set up via
    //      AdsDDSetUserProperty(1102).  Decoded in the Third pass below.
    //
    //   Both sources are unioned into memberships_; duplicates are harmless.
    // =========================================================================
    if (buf.size() < 40)
        return util::Error{5000, 0, "ADD file too small", path_};

    uint32_t hdr_len = le32(buf, 0x20);
    uint32_t rec_len = le32(buf, 0x24);

    if (rec_len == 0 || hdr_len > buf.size())
        return util::Error{5000, 0, "ADD header corrupt", path_};

    binary_format_  = true;
    binary_hdr_len_ = hdr_len;
    binary_rec_len_ = rec_len;
    binary_hdr_     = buf.substr(0, hdr_len);

    // Pre-load companion .am memo file (holds overflow SP/function SQL bodies).
    // Block_size = 8 bytes; byte_offset = block_num * 8.
    std::string am_buf;
    {
        std::string am_path = path_;
        auto dot = am_path.rfind('.');
        if (dot != std::string::npos) {
            am_path = am_path.substr(0, dot) + ".am";
            auto amf_res = platform::File::open(am_path, platform::OpenMode::ReadOnly);
            if (amf_res) {
                auto am_file = std::move(amf_res).value();
                auto sz_res = am_file.size();
                if (sz_res && sz_res.value() > 0) {
                    am_buf.resize(static_cast<std::size_t>(sz_res.value()));
                    auto rr = am_file.read_at(0, am_buf.data(),
                                              static_cast<std::size_t>(sz_res.value()));
                    if (rr && rr.value() < am_buf.size()) am_buf.resize(rr.value());
                }
            }
        }
    }

    // Append .am continuation bytes referenced by a record's more_property field.
    // more_property layout (9 bytes): [4-byte LE block_num][4-byte LE data_len][0x00]
    auto append_am = [&](std::string& field,
                         const std::array<std::uint8_t, 9>& mp) {
        auto am_block = static_cast<uint32_t>(mp[0])
                      | (static_cast<uint32_t>(mp[1]) << 8)
                      | (static_cast<uint32_t>(mp[2]) << 16)
                      | (static_cast<uint32_t>(mp[3]) << 24);
        auto am_len   = static_cast<uint32_t>(mp[4])
                      | (static_cast<uint32_t>(mp[5]) << 8)
                      | (static_cast<uint32_t>(mp[6]) << 16)
                      | (static_cast<uint32_t>(mp[7]) << 24);
        if (am_block == 0 || am_len == 0 || am_buf.empty()) return;
        std::size_t am_off = static_cast<std::size_t>(am_block) * 8;
        if (am_off >= am_buf.size()) return;
        std::size_t readable = std::min<std::size_t>(am_len, am_buf.size() - am_off);
        std::string cont = am_buf.substr(am_off, readable);
        // Strip trailing garbage: scan backward to the last valid SQL byte
        // (printable ASCII 0x20-0x7E or horizontal/vertical whitespace).
        std::size_t lt = cont.size();
        while (lt > 0) {
            unsigned char uc = static_cast<unsigned char>(cont[lt - 1]);
            if ((uc >= 0x20u && uc <= 0x7Eu) || uc == '\t' || uc == '\n' || uc == '\r') break;
            --lt;
        }
        if (lt < cont.size()) cont.resize(lt);
        auto l = cont.find_last_not_of(" \t\r\n");
        if (l != std::string::npos) cont.resize(l + 1);
        field += cont;
    };

    std::size_t total = (buf.size() - hdr_len) / rec_len;
    binary_recs_.reserve(total);

    // obj_id → (name, type) — built incrementally for in-pass lookups.
    std::unordered_map<uint32_t, std::string> id_to_name;
    std::unordered_map<uint32_t, std::string> id_to_type;

    for (std::size_t i = 0; i < total; ++i) {
        std::size_t base = hdr_len + i * rec_len;
        if (base + rec_len > buf.size()) break;

        BinaryRecord r;
        uint8_t status = static_cast<uint8_t>(buf[base]);
        r.active     = (status == 0x04);
        r.obj_id     = le32(buf, base + 5);
        r.parent_id  = le32(buf, base + 9);
        r.obj_type   = trim_char(buf, base + 13, 10);
        r.obj_name   = trim_char(buf, base + 23, 200);

        // Property VarChar
        uint16_t plen = le16(buf, base + 223);
        if (plen == 0xFFFFu) {
            r.prop_null = true;
        } else {
            r.prop_null = false;
            if (plen > 0 && base + 225 + plen <= buf.size())
                r.property = buf.substr(base + 225, plen);
        }

        // More Property (9 bytes)
        for (std::size_t j = 0; j < 9; ++j)
            r.more_property[j] = static_cast<uint8_t>(buf[base + 498u + j]);

        r.info1 = le32(buf, base + 507);
        r.info2 = le32(buf, base + 511);

        // Comment (9 bytes)
        for (std::size_t j = 0; j < 9; ++j)
            r.comment[j] = static_cast<uint8_t>(buf[base + 515u + j]);

        binary_recs_.push_back(std::move(r));

        // Populate in-memory maps from active records we understand.
        const BinaryRecord& rec = binary_recs_.back();
        if (!rec.active) continue;

        // Maintain id lookup tables for cross-record references.
        if (!rec.obj_name.empty()) {
            id_to_name[rec.obj_id] = rec.obj_name;
            id_to_type[rec.obj_id] = rec.obj_type;
        }

        if (rec.obj_type == "Table" && !rec.obj_name.empty() &&
            !rec.prop_null && !rec.property.empty()) {
            // Strip embedded null terminator from stored path.
            std::string path = rec.property;
            auto nul = path.find('\0');
            if (nul != std::string::npos) path.resize(nul);
            if (!path.empty()) tables_[rec.obj_name] = path;

        } else if (rec.obj_type == "Index" && !rec.obj_name.empty() &&
                   !rec.prop_null && !rec.property.empty()) {
            // Find parent table alias for IndexEntry.table_alias.
            std::string tbl_alias;
            for (const auto& r2 : binary_recs_) {
                if (r2.active && r2.obj_type == "Table" &&
                    r2.obj_id == rec.parent_id) {
                    tbl_alias = r2.obj_name;
                    break;
                }
            }
            std::string idx_path = rec.property;
            auto nul = idx_path.find('\0');
            if (nul != std::string::npos) idx_path.resize(nul);
            if (!tbl_alias.empty() && !idx_path.empty())
                indexes_.push_back({tbl_alias, idx_path, {}});

        } else if (rec.obj_type == "User" && !rec.obj_name.empty()) {
            users_.insert(rec.obj_name);

        } else if (rec.obj_type == "Group" && !rec.obj_name.empty()) {
            groups_.insert(rec.obj_name);

        } else if ((rec.obj_type == "Procedure" || rec.obj_type == "StoredProc") &&
                   !rec.obj_name.empty()) {
            ProcEntry e;
            e.name = rec.obj_name;

            // Input params: the plen property field holds the param list (null-terminated).
            if (!rec.prop_null && !rec.property.empty()) {
                e.input_params = rec.property;
                auto nul = e.input_params.find('\0');
                if (nul != std::string::npos) e.input_params.resize(nul);
            }

            // SQL body: in the 273-byte property area beyond the plen bytes.
            // Layout: [plen=inparams] [6×0xFF] [8 bytes binary] [spaces] [CRLF] [SQL] [NUL/end]
            {
                const std::size_t PS = base + 225;  // property data start in file
                const std::size_t PL = 273;         // property area length
                if (PS + PL <= buf.size()) {
                    std::size_t pos = rec.prop_null ? 0u : static_cast<std::size_t>(plen);
                    while (pos < PL && static_cast<uint8_t>(buf[PS + pos]) == 0xFF) ++pos;
                    // Find CR LF that precedes the SQL body
                    for (; pos + 1 < PL; ++pos) {
                        if (static_cast<uint8_t>(buf[PS + pos])   == 0x0D &&
                            static_cast<uint8_t>(buf[PS + pos+1]) == 0x0A) break;
                    }
                    if (pos + 1 < PL) {
                        std::size_t end = PL;
                        for (std::size_t j = pos; j < PL; ++j)
                            if (buf[PS + j] == '\0') { end = j; break; }
                        std::string body = buf.substr(PS + pos, end - pos);
                        auto f = body.find_first_not_of(" \t\r\n");
                        if (f != std::string::npos) body = body.substr(f);
                        auto l = body.find_last_not_of(" \t\r\n");
                        if (l != std::string::npos) body.resize(l + 1);
                        e.procedure = std::move(body);
                    }
                }
            }
            append_am(e.procedure, rec.more_property);
            procs_[e.name] = std::move(e);

        } else if (rec.obj_type == "Function" && !rec.obj_name.empty()) {
            FunctionEntry e;
            e.name = rec.obj_name;

            // Property area layout (273 bytes at base+225..base+497):
            // [plen binary metadata] [6×0xFF] [le16+rettype\0] [le16+inparams\0] [le16+body]
            {
                const std::size_t PS = base + 225;
                const std::size_t PL = 273;
                if (PS + PL <= buf.size()) {
                    std::size_t pos = rec.prop_null ? 0u : static_cast<std::size_t>(plen);
                    while (pos < PL && static_cast<uint8_t>(buf[PS + pos]) == 0xFF) ++pos;

                    // read_lstr: reads a length-prefixed string from the property area.
                    // When the body's slen > remaining space the string is SPLIT:
                    // the inline area holds the first (PL-pos) bytes; the .am file
                    // holds the continuation.  Read as many bytes as fit.
                    auto read_lstr = [&](std::string& out) {
                        if (pos + 2 > PL) return;
                        uint16_t slen =
                            static_cast<uint8_t>(buf[PS + pos]) |
                            (static_cast<uint16_t>(static_cast<uint8_t>(buf[PS + pos + 1])) << 8);
                        pos += 2;
                        if (slen == 0 || slen == 0xFFFF) return;
                        std::size_t readable = std::min<std::size_t>(slen, PL - pos);
                        if (readable == 0) return;
                        out = buf.substr(PS + pos, readable);
                        pos += readable;
                        auto nul = out.find('\0');
                        if (nul != std::string::npos) out.resize(nul);
                    };

                    read_lstr(e.return_type);
                    read_lstr(e.input_params);
                    read_lstr(e.implementation);
                    auto f = e.implementation.find_first_not_of(" \t\r\n");
                    if (f != std::string::npos) e.implementation = e.implementation.substr(f);
                    auto l = e.implementation.find_last_not_of(" \t\r\n");
                    if (l != std::string::npos) e.implementation.resize(l + 1);
                }
            }
            append_am(e.implementation, rec.more_property);
            functions_[e.name] = std::move(e);

        } else if (rec.obj_type == "Trigger" && !rec.obj_name.empty()) {
            // SAP binary .add format for Trigger records (rec_len=524):
            //   Property VarChar (plen bytes at base+225): bytes [0..3] = event type (LE uint32)
            //   Property area (273 bytes at base+225..base+497):
            //     [0..3]   event  (LE uint32): 1=INSERT 2=UPDATE 3=DELETE
            //     [4..5]   0x04 (constant)
            //     [6..7]   timing (LE uint16): 1=BEFORE 2=INSTEAD_OF 4=AFTER
            //     [8..9]   0x00 (reserved/no-memos flag)
            //     [10..11] 0x04 (constant)
            //     [12..15] constant block (0x03 0x00 0x00 0x00)
            //     [16..17] inline body length (LE uint16)
            //     [18..]   inline body SQL text (up to 255 bytes)
            //   More Property (9 bytes at base+498): am_block(4) + am_len(4) + pad
            TriggerEntry e;
            e.name        = rec.obj_name;
            e.table_alias = id_to_name.count(rec.parent_id) ? id_to_name.at(rec.parent_id) : "";

            // Read event type (plen field, 4 bytes at base+225)
            if (!rec.prop_null && rec.property.size() >= 4) {
                e.event_mask = static_cast<uint32_t>(
                    (static_cast<uint8_t>(rec.property[0]))       |
                    (static_cast<uint8_t>(rec.property[1]) <<  8) |
                    (static_cast<uint8_t>(rec.property[2]) << 16) |
                    (static_cast<uint8_t>(rec.property[3]) << 24));
            }

            // Read timing (bytes 6-7 in the full 273-byte property area)
            const std::size_t PA = base + 225;  // property area start in file buffer
            if (PA + 8 <= buf.size()) {
                e.timing = static_cast<uint32_t>(
                    static_cast<uint8_t>(buf[PA + 6]) |
                    (static_cast<uint8_t>(buf[PA + 7]) << 8));
            }

            // Read inline body starting at property area byte 18
            if (PA + 18 < buf.size()) {
                std::size_t body_start = PA + 18;
                std::size_t body_end   = PA + 273;
                if (body_end > buf.size()) body_end = buf.size();
                std::string body = buf.substr(body_start, body_end - body_start);
                auto nul = body.find('\0');
                if (nul != std::string::npos) body.resize(nul);
                auto first = body.find_first_not_of(" \t\r\n");
                if (first != std::string::npos) body = body.substr(first);
                auto last  = body.find_last_not_of(" \t\r\n");
                if (last  != std::string::npos) body.resize(last + 1);
                e.container = std::move(body);
            }

            // Append .am continuation (same pattern as procs/functions)
            append_am(e.container, rec.more_property);

            e.enabled = true;
            triggers_[e.name] = std::move(e);

        } else if (rec.obj_type == "Relation" && !rec.obj_name.empty() &&
                   !rec.prop_null) {
            auto parts = split_nul(rec.property);
            while (parts.size() < 7) parts.emplace_back();
            RiEntry e;
            e.name       = rec.obj_name;
            e.parent     = parts[0];
            e.child      = parts[1];
            e.parent_tag = parts[2];
            e.child_tag  = parts[3];
            e.update_opt = parts[4];
            e.delete_opt = parts[5];
            e.fail_table = parts[6];
            ri_[e.name] = std::move(e);

        } else if (rec.obj_type == "View" && !rec.obj_name.empty() &&
                   !rec.prop_null) {
            auto parts = split_nul(rec.property);
            while (parts.size() < 2) parts.emplace_back();
            ViewEntry e;
            e.name    = rec.obj_name;
            e.comment = parts[0];
            e.sql     = parts[1];
            views_[e.name] = std::move(e);

        } else if (rec.obj_type == "Field" && !rec.obj_name.empty()) {
            // Populate field_props_ so system.permissions can enumerate fields.
            // parent_id → table's obj_id; info1 → ordinal position (1-based).
            auto tbl_it = id_to_name.find(rec.parent_id);
            if (tbl_it != id_to_name.end()) {
                const std::string& tbl = tbl_it->second;
                auto& fp = field_props_[tbl][rec.obj_name];
                if (fp.find("ordinal") == fp.end())
                    fp["ordinal"] = std::to_string(rec.info1);
            }
        }
    }

    // SAP object-type string → numeric code used in system.permissions.
    auto type_code = [](const std::string& t) -> int {
        if (t == "Table")      return 1;   // ADS_DD_TABLE_OBJECT
        if (t == "Field")      return 4;   // ADS_DD_FIELD_OBJECT
        if (t == "View")       return 6;   // ADS_DD_VIEW_OBJECT
        if (t == "User")       return 8;   // ADS_DD_USER_OBJECT
        if (t == "Group")      return 9;   // ADS_DD_USER_GROUP_OBJECT
        if (t == "StoredProc") return 10;  // ADS_DD_PROCEDURE_OBJECT
        if (t == "Database")   return 11;  // ADS_DD_DATABASE_OBJECT
        if (t == "Link")       return 12;  // ADS_DD_LINK_OBJECT
        if (t == "Function")   return 18;  // ADS_DD_FUNCTION_OBJECT
        return 0;
    };

    // -------------------------------------------------------------------------
    // Second pass: Permission records.
    //
    // Permission record layout:
    //   obj_type  = "Permission"
    //   obj_name  = sprintf("%08X", info1)   (hex of target obj_id)
    //   parent_id = granting principal (User or Group obj_id)
    //   info1     = target object obj_id
    //   info2     = permission bitmask; 0x80000000 = INHERIT (full/group-member)
    //
    // Two interpretations depending on the target's type:
    //   target type == "Group" → user-group membership
    //     parent (User) is a member of target (Group).
    //     SAP ACE creates these via AdsDDAddUserToGroup.  OpenADS uses the same
    //     format for all new write-path group memberships.
    //   target type != "Group" → ACL entry (table/object permission)
    for (const auto& rec : binary_recs_) {
        if (!rec.active || rec.obj_type != "Permission") continue;
        auto principal_it = id_to_name.find(rec.parent_id);
        if (principal_it == id_to_name.end()) continue;
        const std::string& principal = principal_it->second;

        auto target_type_it = id_to_type.find(rec.info1);
        if (target_type_it == id_to_type.end()) continue;
        const std::string& target_type = target_type_it->second;

        auto target_name_it = id_to_name.find(rec.info1);
        if (target_name_it == id_to_name.end()) continue;
        const std::string& target_name = target_name_it->second;

        if (target_type == "Group") {
            // Group-membership records come in two varieties:
            //
            //   SAP-written   (AdsDDAddUserToGroup):   prop_null = true  (plen=0xFFFF)
            //   OpenADS-written (our add_user_to_group): prop_null = false (hex property)
            //
            // SAP does NOT delete Permission records when removing a user from a
            // group — it only updates the XOR property-byte tokens (Third Pass).
            // This leaves stale records that SAP's own AdsDDGetUserProperty(1102)
            // and system.usergroups do NOT include.  Verified on pmsys.add and
            // mp.add: every SAP-written group-membership record has prop_null=true,
            // every OpenADS-written one has prop_null=false.
            //
            // Rule: trust only prop_null=false (OpenADS-written) records here.
            //       prop_null=true (SAP-written) records may be stale → skip them.
            //       Current SAP memberships are decoded from XOR tokens (Third Pass).
            if (!rec.prop_null) {
                memberships_[principal].insert(target_name);
            }
        } else {
            // ACL entry: store full bitmask for system.permissions.
            auto principal_type_it = id_to_type.find(rec.parent_id);
            bool is_grp = (principal_type_it != id_to_type.end() &&
                           principal_type_it->second == "Group");
            PermissionEntry pe;
            pe.object_name      = target_name;
            pe.object_type      = target_type;
            pe.object_type_code = type_code(target_type);
            pe.grantee          = principal;
            pe.grantee_is_group = is_grp;
            pe.bitmask          = rec.info2;
            permissions_.push_back(std::move(pe));

            // Also maintain table_perms_ for get_effective_permission().
            // SAP ADS_PERMISSION_* bit layout:
            //   bit0(0x01)=READ/SELECT  bit1(0x02)=UPDATE
            //   bit4(0x10)=INSERT       bit5(0x20)=DELETE
            //   bit31(0x80000000)=WITH_GRANT sentinel (SAP writes this for all grants)
            if (target_type == "Table") {
                int level;
                if (rec.info2 & 0x80000000u) {
                    // SAP WITH_GRANT sentinel — treat as full access (we cannot
                    // decode the actual level from the encrypted property blobs).
                    level = 4;
                } else if (rec.info2 & 0x20u) {
                    level = 3;  // DELETE present → DELETE level
                } else if (rec.info2 & 0x10u) {
                    level = 2;  // INSERT present (no DELETE) → WRITE level
                } else if (rec.info2 & 0x02u) {
                    level = 2;  // UPDATE present → WRITE level
                } else if (rec.info2 & 0x01u) {
                    level = 1;  // READ only
                } else {
                    level = 0;
                }
                table_perms_[target_name][principal] = level;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Third pass: decode User property-byte group memberships.
    //
    // Token encoding (reverse-engineered from pmsys.add with ACE32 v10,
    // verified against mp.add with SAP ACE64 v11 / 14 groups / 43 users):
    //
    //   token[slot] = K[slot] XOR group_id_LE_4bytes
    //
    //   where K[slot] is a 4-byte per-database constant.  Each user's token at
    //   slot s encodes the group that user was assigned to in s-th position.
    //   DIFFERENT users may be in different groups at the same slot index.
    //   K[slot] is the SAME for all users in the same database.
    //
    // Structure of the extra area at offset plen in the 273-byte property zone:
    //
    //   [plen+0..plen+1]  uint16 LE count_field
    //                       0xFFFF → user has no property-byte groups (skip)
    //                       N×4   → N group tokens follow
    //   [plen+2..plen+N×4+1]
    //                     N × 4-byte XOR tokens (slot 0 first)
    //   [plen+N×4+2 ..]   Post-token section (DB: built-in group membership
    //                     block).  Contains 4-byte entries alternating FFFFFFFF
    //                     separators and encoded values, optionally followed by
    //                     a uint16 length + N bytes of per-user-encrypted data.
    //                     This block uses a per-user cipher OpenADS cannot decode.
    //   [...]             0xFF padding to end of 273-byte zone
    //   [var]             XML-like <OTHER_PROPERTIES>...</OTHER_PROPERTIES> block,
    //                     stored as uint16 length + XML bytes, inside the FF zone.
    //
    //   Note: plen varies per user (it is the password-hash length), so the
    //   extra area starts at a different file offset for each user record.
    //
    // How K[slot] is derived at runtime (no documentation available):
    //
    //   K[slot] is a per-database constant computed inside the SAP ACE DLL
    //   from the 20-byte random key stored in the Database record's property
    //   field.  The derivation algorithm is proprietary and not available.
    //
    //   We brute-force K[slot] using known-plaintext: the Group obj_ids from
    //   all Group records are known, so for each candidate group_id G we
    //   compute K = first_user_token XOR G_LE and verify that every other
    //   user's slot-N token also decodes to a valid group_id.
    //
    // Disambiguation constraints:
    //
    //   1. Ascending ID order: valid group IDs are tried smallest-first.
    //      This picks the lowest-ID group for each slot's first occurrence.
    //
    //   2. No-duplicate: once a K[slot] is found, the decoded group IDs are
    //      recorded per user.  A candidate K[s] that would assign a user to a
    //      group it already holds from slot 0..s-1 is rejected.
    //
    //   3. Single-user ambiguity: if only one user has a token at slot s AND
    //      multiple K candidates pass all checks, the slot is skipped (cannot
    //      be disambiguated without a second user for cross-validation).
    //
    // Together these constraints uniquely determine K[slot] for all slots in
    // all tested databases (verified on pmsys.add, mp.add).
    //
    // Observed values for pmsys.add (pmsys database, verified with ACE32):
    //   K[0] = [0x00, 0x71, 0x0D, 0x50]  K[1] = [0xE6, 0x96, 0x26, 0x39]
    //   K[2] = [0x6F, 0xF3, 0x58, 0xE7]
    //
    // Observed values for mp.add (sfi-2021, 14 groups, 43 users, ACE64 v11):
    //   K[0] = [0xBC, 0x56, 0x29, 0x7C]  K[1] = [0xB5, 0xCF, 0x07, 0x0D]
    //   K[2] = [0xF0, 0x0B, 0x6E, 0xB2]  K[3] = [0xCF, 0xA0, 0x89, 0x50]
    //   K[4] = [0xF6, 0xCE, 0x0A, 0xD9]  K[5] = [0x8B, 0x02, 0x78, 0x96]
    //   K[6] = [0x7C, 0x9E, 0x0E, 0x6C]
    //   (7 slots used; max 8 groups/user in this DB)
    //
    // Accuracy: property-byte XOR decodes 42/43 users correctly (cross-checked
    // against system.usergroups HTML export).  The remaining user (RCB) has 3
    // additional groups in Permission records (newer-format) handled by Second
    // Pass above.  Combined accuracy: 43/43.
    //
    // Built-in groups (DB:Public, DB:Admin, DB:Debug, DB:Backup):
    //   These have NO Group records and NO XOR tokens in the .add file.
    //   DB:Admin/Backup/Debug: encoded in the post-token section with a per-user
    //   cipher (key is user-derived, not per-database — confirmed by zero shared
    //   tokens across users with known memberships).  Cannot be decoded offline.
    //   DB:Public: hardcoded below — SAP spec says every authenticated user is a
    //   member.  The other DB: groups are set by AdsDDAddUserToGroup via a
    //   separate server-side call; we cannot replicate the encoding.
    //
    // SAP DLL alternative (when adssys credentials are available):
    //   AdsDDGetUserProperty(hConn, username, 1102 /*ADS_DD_USER_GROUP_MEMBERSHIP*/,
    //                        buf, &len)
    //   Returns a semicolon-separated, null-terminated ASCII string of ALL group
    //   names including DB: built-ins.  Example (pmsys.add, user root):
    //     "General;Supervisors;Administrators;DB:Public;DB:Admin;DB:Debug;DB:Backup"
    //   This is the authoritative source when available.  A future "SAP-DLL-assisted"
    //   mode could call this function at load time (if ACE64.dll is on PATH and
    //   adssys credentials are held) to replace the XOR brute-force entirely and
    //   add DB:Admin/Debug/Backup support.  Not implemented — requires live creds.
    {
        std::unordered_set<uint32_t> valid_gids;
        for (const auto& [gid, gtype] : id_to_type)
            if (gtype == "Group") valid_gids.insert(gid);

        if (!valid_gids.empty()) {
            constexpr std::size_t MAX_SLOTS = 20;  // mp.add uses 7; pmsys uses 3
            constexpr std::size_t PROP_AREA = 273;

            struct TokEntry { std::string user; std::array<uint8_t, 4> tok; };
            std::array<std::vector<TokEntry>, MAX_SLOTS> slot_data;

            // Collect tokens from every User record.
            for (std::size_t i = 0; i < total; ++i) {
                std::size_t base = hdr_len + i * rec_len;
                if (base + rec_len > buf.size()) break;
                if (static_cast<uint8_t>(buf[base]) != 0x04) continue;
                if (trim_char(buf, base + 13, 10) != "User") continue;
                std::string uname = trim_char(buf, base + 23, 200);
                if (uname.empty()) continue;
                uint16_t plen2 = le16(buf, base + 223);
                if (plen2 == 0xFFFFu) continue;
                std::size_t xoff = base + 225 + plen2;
                std::size_t xend = base + 225 + PROP_AREA;
                if (xoff + 2 > xend) continue;
                uint16_t cf = le16(buf, xoff);
                if (cf == 0xFFFFu || cf == 0) continue;
                uint32_t ntok = static_cast<uint32_t>(cf) / 4u;
                for (uint32_t s = 0; s < ntok && s < MAX_SLOTS; ++s) {
                    std::size_t toff = xoff + 2 + s * 4;
                    if (toff + 4 > xend) break;
                    std::array<uint8_t, 4> tok = {
                        static_cast<uint8_t>(buf[toff]),
                        static_cast<uint8_t>(buf[toff + 1]),
                        static_cast<uint8_t>(buf[toff + 2]),
                        static_cast<uint8_t>(buf[toff + 3])};
                    slot_data[s].push_back({uname, tok});
                }
            }

            // Sort group IDs ascending for deterministic brute force: smaller IDs
            // are tried first, matching the natural creation order of built-in groups.
            std::vector<uint32_t> sorted_gids(valid_gids.begin(), valid_gids.end());
            std::sort(sorted_gids.begin(), sorted_gids.end());

            // For each slot, brute-force K[slot] with two constraints:
            //   (a) all tokens decode to valid group IDs, AND
            //   (b) no user is assigned to the same group more than once across slots.
            std::array<std::array<uint8_t, 4>, MAX_SLOTS> slot_keys{};
            std::array<bool, MAX_SLOTS> slot_known{};
            for (auto& k : slot_keys) k = {};
            for (auto& b : slot_known) b = false;

            // Per-user decoded group IDs (property bytes only) across slots.
            std::unordered_map<std::string, std::unordered_set<uint32_t>> user_prop_gids;

            for (std::size_t s = 0; s < MAX_SLOTS; ++s) {
                if (slot_data[s].empty()) break;
                const auto& t0 = slot_data[s][0].tok;

                // Count how many group IDs produce a valid K for this slot.
                // With multiple users any valid K is cross-validated; with a
                // single user, multiple candidates mean the result is ambiguous
                // (we cannot distinguish e.g. Readonly from Internet).
                // Strategy:
                //   n_valid == 0  → no valid K, skip
                //   n_valid == 1  → unambiguous, accept (even for 1 user)
                //   n_valid >= 2 and multiple users → cross-validated, accept first
                //   n_valid >= 2 and single user    → ambiguous, skip
                int n_valid = 0;
                std::array<uint8_t, 4> first_K{};

                for (uint32_t gid : sorted_gids) {
                    std::array<uint8_t, 4> K = {
                        static_cast<uint8_t>(t0[0] ^ static_cast<uint8_t>(gid & 0xFFu)),
                        static_cast<uint8_t>(t0[1] ^ static_cast<uint8_t>((gid >> 8) & 0xFFu)),
                        static_cast<uint8_t>(t0[2] ^ static_cast<uint8_t>((gid >> 16) & 0xFFu)),
                        static_cast<uint8_t>(t0[3] ^ static_cast<uint8_t>((gid >> 24) & 0xFFu))};
                    bool ok = true;
                    for (const auto& e : slot_data[s]) {
                        const auto& t = e.tok;
                        uint32_t dec =
                            static_cast<uint32_t>(t[0] ^ K[0]) |
                            (static_cast<uint32_t>(t[1] ^ K[1]) << 8) |
                            (static_cast<uint32_t>(t[2] ^ K[2]) << 16) |
                            (static_cast<uint32_t>(t[3] ^ K[3]) << 24);
                        if (!valid_gids.count(dec)) { ok = false; break; }
                        auto uit = user_prop_gids.find(e.user);
                        if (uit != user_prop_gids.end() && uit->second.count(dec)) {
                            ok = false; break;
                        }
                    }
                    if (ok) {
                        if (n_valid == 0) first_K = K;
                        ++n_valid;
                        // For multi-user slots the first valid K is cross-validated;
                        // no need to count further.
                        if (slot_data[s].size() >= 2) break;
                        // For single-user slots we must keep counting to detect
                        // ambiguity (more than one valid candidate).
                        if (n_valid >= 2) break;
                    }
                }

                // Skip: no valid K, or single user with multiple candidates.
                if (n_valid == 0) continue;
                if (n_valid >= 2 && slot_data[s].size() < 2) continue;

                slot_keys[s] = first_K;
                slot_known[s] = true;
                // Record decoded groups to enforce uniqueness for later slots.
                for (const auto& e : slot_data[s]) {
                    const auto& t = e.tok;
                    uint32_t dec =
                        static_cast<uint32_t>(t[0] ^ first_K[0]) |
                        (static_cast<uint32_t>(t[1] ^ first_K[1]) << 8) |
                        (static_cast<uint32_t>(t[2] ^ first_K[2]) << 16) |
                        (static_cast<uint32_t>(t[3] ^ first_K[3]) << 24);
                    user_prop_gids[e.user].insert(dec);
                }
            }

            // Decode tokens and populate memberships_.
            for (std::size_t s = 0; s < MAX_SLOTS; ++s) {
                if (!slot_known[s]) continue;
                const auto& K = slot_keys[s];
                for (const auto& e : slot_data[s]) {
                    const auto& t = e.tok;
                    uint32_t gid =
                        static_cast<uint32_t>(t[0] ^ K[0]) |
                        (static_cast<uint32_t>(t[1] ^ K[1]) << 8) |
                        (static_cast<uint32_t>(t[2] ^ K[2]) << 16) |
                        (static_cast<uint32_t>(t[3] ^ K[3]) << 24);
                    auto it = id_to_name.find(gid);
                    if (it != id_to_name.end())
                        memberships_[e.user].insert(it->second);
                }
            }
        }

        // DB:Public — SAP built-in group that every authenticated user belongs
        // to.  It has no Group record in the binary .add file; its membership is
        // encoded in a per-user cipher that OpenADS cannot decode.  Add it
        // unconditionally for all users so system.usergroupmembers matches SAP.
        for (const auto& uname : users_)
            memberships_[uname].insert("DB:Public");
    }

    return {};
}

util::Result<void> DataDict::load_() {
    auto fres = platform::File::open(path_, platform::OpenMode::ReadOnly);
    if (!fres) return fres.error();
    auto file = std::move(fres).value();
    auto sz = file.size();
    if (!sz) return sz.error();
    std::string buf(static_cast<std::size_t>(sz.value()), '\0');
    if (!buf.empty()) {
        auto rd = file.read_at(0, buf.data(), buf.size());
        if (!rd) return rd.error();
    }

    // Detect ADS proprietary binary format by its fixed signature.
    if (buf.size() >= 20 &&
        buf.compare(0, 19, "ADS Data Dictionary") == 0 &&
        static_cast<uint8_t>(buf[19]) == 0x00) {
        return load_add_binary_(buf);
    }

    std::istringstream ss(buf);
    std::string line;
    auto starts_with = [](const std::string& s, const char* k) {
        std::size_t n = std::strlen(k);
        return s.size() >= n && s.compare(0, n, k) == 0;
    };
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        if (starts_with(line, "TABLE ")) {
            std::string body = line.substr(6);
            auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            std::string alias = trim(body.substr(0, eq));
            std::string path  = trim(body.substr(eq + 1));
            if (!alias.empty() && !path.empty()) tables_[alias] = path;

        } else if (starts_with(line, "INDEX ")) {
            std::string body = line.substr(6);
            auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            std::string alias = trim(body.substr(0, eq));
            std::string rest  = body.substr(eq + 1);
            auto parts = split_tabs(rest);
            if (alias.empty() || parts.empty() || parts[0].empty()) continue;
            IndexEntry e;
            e.table_alias = alias;
            e.index_path  = parts[0];
            if (parts.size() > 1) e.comment = parts[1];
            indexes_.push_back(std::move(e));

        } else if (starts_with(line, "USER ")) {
            std::string name = trim(line.substr(5));
            if (!name.empty()) users_.insert(name);

        } else if (starts_with(line, "GROUP ")) {
            std::string name = trim(line.substr(6));
            if (!name.empty()) groups_.insert(name);

        } else if (starts_with(line, "MEMBER ")) {
            std::string body = line.substr(7);
            auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            std::string user  = trim(body.substr(0, eq));
            std::string group = trim(body.substr(eq + 1));
            if (!user.empty() && !group.empty()) {
                memberships_[user].insert(group);
            }

        } else if (starts_with(line, "LINK ")) {
            std::string body = line.substr(5);
            auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            std::string alias = trim(body.substr(0, eq));
            std::string rest  = body.substr(eq + 1);
            auto parts = split_tabs(rest);
            if (alias.empty() || parts.empty()) continue;
            LinkEntry e;
            e.alias = alias;
            e.path  = parts[0];
            if (parts.size() > 1) e.user = parts[1];
            if (parts.size() > 2) e.pwd  = parts[2];
            links_[alias] = std::move(e);

        } else if (starts_with(line, "RI ")) {
            std::string body = line.substr(3);
            auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            std::string name = trim(body.substr(0, eq));
            std::string rest = body.substr(eq + 1);
            // v2: parent;child;parent_tag;child_tag;update_opt;delete_opt;fail_table
            // v1: parent;child;tag;update_opt;delete_opt;fail_table  (no child_tag)
            std::vector<std::string> parts;
            std::string cur;
            for (char c : rest) {
                if (c == ';') { parts.push_back(cur); cur.clear(); }
                else cur.push_back(c);
            }
            parts.push_back(cur);
            std::size_t nfields = parts.size();
            while (parts.size() < 7) parts.emplace_back();
            RiEntry e;
            e.name = name;
            e.parent = parts[0];
            e.child  = parts[1];
            if (nfields >= 7) {
                // v2 format: parent;child;parent_tag;child_tag;update_opt;delete_opt;fail_table
                e.parent_tag = parts[2];
                e.child_tag  = parts[3];
                e.update_opt = parts[4];
                e.delete_opt = parts[5];
                e.fail_table = parts[6];
            } else {
                // v1 format: parent;child;tag;update_opt;delete_opt;fail_table
                e.parent_tag = parts[2];
                e.child_tag  = "";
                e.update_opt = parts[3];
                e.delete_opt = parts[4];
                e.fail_table = parts[5];
            }
            ri_[name] = std::move(e);

        } else if (starts_with(line, "DBPROP ")) {
            std::string body = line.substr(7);
            auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            db_props_[trim(body.substr(0, eq))] = trim(body.substr(eq + 1));

        } else if (starts_with(line, "USERPROP ")) {
            std::string body = line.substr(9);
            auto sc = body.find(';');
            if (sc == std::string::npos) continue;
            std::string user = trim(body.substr(0, sc));
            std::string rest = body.substr(sc + 1);
            auto eq = rest.find('=');
            if (eq == std::string::npos) continue;
            user_props_[user][trim(rest.substr(0, eq))] =
                trim(rest.substr(eq + 1));

        } else if (starts_with(line, "TABLEPERM ")) {
            // TABLEPERM <table>;<user_or_group>=<level>
            std::string body = line.substr(10);
            auto sc = body.find(';');
            if (sc == std::string::npos) continue;
            std::string tbl  = trim(body.substr(0, sc));
            std::string rest = body.substr(sc + 1);
            auto eq = rest.find('=');
            if (eq == std::string::npos) continue;
            std::string ug  = trim(rest.substr(0, eq));
            std::string lvs = trim(rest.substr(eq + 1));
            if (tbl.empty() || ug.empty() || lvs.empty()) continue;
            try {
                int level = std::stoi(lvs);
                table_perms_[tbl][ug] = level;
                // Mirror into permissions_ so eff_ops enforcement works.
                uint32_t bitmask = 0;
                if (level >= 1) bitmask |= 0x001u;           // READ
                if (level >= 2) bitmask |= 0x002u | 0x010u;  // UPDATE|INSERT
                if (level >= 3) bitmask |= 0x020u;           // DELETE
                if (level >= 4) bitmask  = 0x80000000u;      // full sentinel
                bool is_grp = (groups_.find(ug) != groups_.end());
                PermissionEntry pe;
                pe.object_name      = tbl;
                pe.object_type      = "Table";
                pe.object_type_code = 1;
                pe.grantee          = ug;
                pe.grantee_is_group = is_grp;
                pe.bitmask          = bitmask;
                permissions_.push_back(std::move(pe));
            }
            catch (...) {}

        } else if (starts_with(line, "FIELDPROP ")) {
            // FIELDPROP <table>;<field>;<key>=<value>
            std::string body = line.substr(10);
            auto sc1 = body.find(';');
            if (sc1 == std::string::npos) continue;
            std::string tbl  = trim(body.substr(0, sc1));
            std::string rest = body.substr(sc1 + 1);
            auto sc2 = rest.find(';');
            if (sc2 == std::string::npos) continue;
            std::string fld  = trim(rest.substr(0, sc2));
            std::string kv   = rest.substr(sc2 + 1);
            auto eq = kv.find('=');
            if (eq == std::string::npos) continue;
            field_props_[tbl][fld][trim(kv.substr(0, eq))] = trim(kv.substr(eq + 1));

        } else if (starts_with(line, "TRIGGER ")) {
            // TRIGGER <name>=<table>;<event_mask>;<timing>;<priority>;<enabled>;<container>;<procedure>;<comment>
            // (Legacy 7-field format: <table>;<event_mask>;<priority>;<enabled>;<container>;<proc>;<comment>)
            std::string body = line.substr(8);
            auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            std::string name = trim(body.substr(0, eq));
            std::string rest = body.substr(eq + 1);
            std::vector<std::string> parts;
            std::string cur;
            for (char c : rest) {
                if (c == ';') { parts.push_back(cur); cur.clear(); }
                else cur.push_back(c);
            }
            parts.push_back(cur);
            while (parts.size() < 8) parts.emplace_back();
            TriggerEntry e;
            e.name        = name;
            e.table_alias = parts[0];
            try { e.event_mask = static_cast<std::uint32_t>(std::stoul(parts[1])); } catch (...) {}
            try { e.timing     = static_cast<std::uint32_t>(std::stoul(parts[2])); } catch (...) {}
            try { e.priority   = static_cast<std::uint32_t>(std::stoul(parts[3])); } catch (...) {}
            e.enabled   = (parts[4] != "0");
            e.container = parts[5];
            e.procedure = parts[6];
            e.comment   = parts[7];
            if (!name.empty()) triggers_[name] = std::move(e);

        } else if (starts_with(line, "PROC ")) {
            // PROC <name>=<container>;<procedure>;<input>;<output>;<comment>
            std::string body = line.substr(5);
            auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            std::string name = trim(body.substr(0, eq));
            std::string rest = body.substr(eq + 1);
            std::vector<std::string> parts;
            std::string cur;
            for (char c : rest) {
                if (c == ';') { parts.push_back(cur); cur.clear(); }
                else cur.push_back(c);
            }
            parts.push_back(cur);
            while (parts.size() < 5) parts.emplace_back();
            ProcEntry e;
            e.name          = name;
            e.container     = parts[0];
            e.procedure     = parts[1];
            e.input_params  = parts[2];
            e.output_params = parts[3];
            e.comment       = parts[4];
            if (!name.empty()) procs_[name] = std::move(e);

        } else if (starts_with(line, "VIEW ")) {
            // VIEW <name>=<comment>;<sql>
            std::string body = line.substr(5);
            auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            std::string name = trim(body.substr(0, eq));
            std::string rest = body.substr(eq + 1);
            auto sc = rest.find(';');
            ViewEntry e;
            e.name    = name;
            e.comment = (sc != std::string::npos) ? rest.substr(0, sc) : "";
            e.sql     = (sc != std::string::npos) ? rest.substr(sc + 1) : rest;
            if (!name.empty()) views_[name] = std::move(e);
        }
    }
    return {};
}

util::Result<void>
DataDict::add_table(const std::string& alias,
                    const std::string& relative_path) {
    if (alias.empty() || relative_path.empty()) {
        return util::Error{5000, 0, "DD alias / path must be non-empty", ""};
    }
    tables_[alias] = relative_path;
    if (binary_format_) {
        // Look for an existing active Table record with this alias to update.
        for (auto& r : binary_recs_) {
            if (r.active && r.obj_type == "Table" && r.obj_name == alias) {
                r.property  = relative_path + '\0';
                r.prop_null = false;
                return save();
            }
        }
        // Not found: add a new Table record.
        BinaryRecord r;
        r.active    = true;
        r.obj_id    = binary_alloc_id_();
        r.parent_id = binary_obj_id_of_("Database", "Database");
        if (r.parent_id == 0) r.parent_id = 1;  // safe fallback
        r.obj_type  = "Table";
        r.obj_name  = alias;
        r.property  = relative_path + '\0';
        r.prop_null = false;
        r.info1     = 0;
        r.info2     = 0;
        binary_recs_.push_back(std::move(r));
    }
    return save();
}

util::Result<void> DataDict::remove_table(const std::string& alias) {
    tables_.erase(alias);
    if (binary_format_) {
        for (auto& r : binary_recs_) {
            if (r.active && r.obj_type == "Table" && r.obj_name == alias) {
                r.active = false;
                break;
            }
        }
    }
    return save();
}

std::string DataDict::resolve(const std::string& alias_or_path) const {
    auto it = tables_.find(alias_or_path);
    if (it != tables_.end()) return it->second;
    return alias_or_path;
}

util::Result<void>
DataDict::add_index_file(const std::string& table_alias,
                         const std::string& index_path,
                         const std::string& comment) {
    if (table_alias.empty() || index_path.empty()) {
        return util::Error{5000, 0, "DD index alias / path empty", ""};
    }
    indexes_.push_back({table_alias, index_path, comment});
    if (binary_format_) {
        // Derive index filename (basename) for obj_name.
        std::string idx_name = index_path;
        auto slash = idx_name.find_last_of("/\\");
        if (slash != std::string::npos) idx_name = idx_name.substr(slash + 1);

        uint32_t tbl_id = binary_obj_id_of_("Table", table_alias);
        BinaryRecord r;
        r.active    = true;
        r.obj_id    = binary_alloc_id_();
        r.parent_id = (tbl_id != 0) ? tbl_id : 1;
        r.obj_type  = "Index";
        r.obj_name  = idx_name;
        r.property  = index_path + '\0';
        r.prop_null = false;
        r.info1     = 0;
        r.info2     = 0x80000000u;
        binary_recs_.push_back(std::move(r));
    }
    return save();
}

util::Result<void>
DataDict::remove_index_file(const std::string& table_alias,
                            const std::string& index_path) {
    auto it = std::remove_if(indexes_.begin(), indexes_.end(),
        [&](const IndexEntry& e) {
            return e.table_alias == table_alias && e.index_path == index_path;
        });
    indexes_.erase(it, indexes_.end());
    if (binary_format_) {
        // Derive index filename for matching binary record obj_name.
        std::string idx_name = index_path;
        auto slash = idx_name.find_last_of("/\\");
        if (slash != std::string::npos) idx_name = idx_name.substr(slash + 1);
        for (auto& r : binary_recs_) {
            if (r.active && r.obj_type == "Index" && r.obj_name == idx_name) {
                r.active = false;
                break;
            }
        }
    }
    return save();
}

util::Result<void> DataDict::create_user(const std::string& user) {
    if (user.empty()) {
        return util::Error{5000, 0, "DD user name empty", ""};
    }
    users_.insert(user);
    if (binary_format_) {
        for (const auto& r : binary_recs_) {
            if (r.active && r.obj_type == "User" && r.obj_name == user)
                return save();
        }
        BinaryRecord r;
        r.active    = true;
        r.obj_id    = binary_alloc_id_();
        r.parent_id = binary_obj_id_of_("Database", "Database");
        if (r.parent_id == 0) r.parent_id = 1;
        r.obj_type  = "User";
        r.obj_name  = user;
        r.prop_null = true;     // password not managed by OpenADS
        r.info1     = 1;
        r.info2     = 0x80000000u;
        binary_recs_.push_back(std::move(r));
    }
    return save();
}

util::Result<void> DataDict::delete_user(const std::string& user) {
    users_.erase(user);
    memberships_.erase(user);
    user_props_.erase(user);
    if (binary_format_) {
        for (auto& r : binary_recs_) {
            if (r.active && r.obj_type == "User" && r.obj_name == user) {
                r.active = false;
                break;
            }
        }
    }
    return save();
}

util::Result<void>
DataDict::add_user_to_group(const std::string& user,
                            const std::string& group) {
    if (user.empty() || group.empty()) {
        return util::Error{5000, 0, "DD member user / group empty", ""};
    }
    memberships_[user].insert(group);
    if (binary_format_) {
        // Write a Permission record (parent=user, info1=group, info2=INHERIT).
        // This is the standard SAP ACE v8+ format for group membership and is
        // what AdsDDAddUserToGroup writes to the .add file.  We do NOT write
        // property-byte XOR tokens: the derivation of K[slot] from the database
        // key is proprietary to the ACE DLL and would require the same 20-byte
        // random Database property to reproduce.  Permission records are read
        // back correctly by the Second pass in load_add_binary_().
        uint32_t user_id  = binary_obj_id_of_("User",  user);
        uint32_t group_id = binary_obj_id_of_("Group", group);
        if (user_id == 0 || group_id == 0)
            return save();  // IDs not in DD yet — membership is in-memory only
        for (const auto& r : binary_recs_) {
            if (r.active && r.obj_type == "Permission" &&
                r.parent_id == user_id && r.info1 == group_id)
                return save();  // already exists
        }
        char name_buf[9];
        std::snprintf(name_buf, sizeof(name_buf), "%08X", group_id);
        BinaryRecord perm;
        perm.active    = true;
        perm.obj_id    = binary_alloc_id_();
        perm.parent_id = user_id;
        perm.obj_type  = "Permission";
        perm.obj_name  = name_buf;   // conventional: hex(group_id)
        perm.prop_null = false;
        perm.property  = name_buf;
        perm.info1     = group_id;
        perm.info2     = 0x80000000u;  // INHERIT flag = "member of group"
        binary_recs_.push_back(std::move(perm));
    }
    return save();
}

util::Result<void>
DataDict::remove_user_from_group(const std::string& user,
                                 const std::string& group) {
    auto it = memberships_.find(user);
    if (it != memberships_.end()) {
        it->second.erase(group);
        if (it->second.empty()) memberships_.erase(it);
    }
    if (binary_format_) {
        uint32_t user_id  = binary_obj_id_of_("User",  user);
        uint32_t group_id = binary_obj_id_of_("Group", group);
        if (user_id != 0 && group_id != 0) {
            for (auto& r : binary_recs_) {
                if (r.active && r.obj_type == "Permission" &&
                    r.parent_id == user_id && r.info1 == group_id) {
                    r.active = false;
                    break;
                }
            }
        }
    }
    return save();
}

bool DataDict::is_member_of(const std::string& user,
                            const std::string& group) const {
    auto it = memberships_.find(user);
    if (it == memberships_.end()) return false;
    return it->second.find(group) != it->second.end();
}

const std::unordered_set<std::string>&
DataDict::groups_of(const std::string& user) const {
    static const std::unordered_set<std::string> empty;
    auto it = memberships_.find(user);
    return it == memberships_.end() ? empty : it->second;
}

util::Result<void>
DataDict::create_link(const std::string& alias, const std::string& path,
                      const std::string& user, const std::string& pwd) {
    if (alias.empty() || path.empty()) {
        return util::Error{5000, 0, "DD link alias / path empty", ""};
    }
    links_[alias] = {alias, path, user, pwd};
    return save();
}

util::Result<void> DataDict::drop_link(const std::string& alias) {
    links_.erase(alias);
    return save();
}

util::Result<void>
DataDict::modify_link(const std::string& alias, const std::string& path,
                      const std::string& user, const std::string& pwd) {
    auto it = links_.find(alias);
    if (it == links_.end()) {
        return util::Error{5000, 0, "DD link not found", alias};
    }
    if (!path.empty()) it->second.path = path;
    it->second.user = user;
    it->second.pwd  = pwd;
    return save();
}

util::Result<void> DataDict::create_ri(const RiEntry& e) {
    if (e.name.empty())
        return util::Error{5000, 0, "DD RI name empty", ""};
    ri_[e.name] = e;
    if (binary_format_) {
        auto prop = join_nul({e.parent, e.child, e.parent_tag, e.child_tag,
                               e.update_opt, e.delete_opt, e.fail_table});
        for (auto& r : binary_recs_) {
            if (r.active && r.obj_type == "Relation" && r.obj_name == e.name) {
                r.property = prop; r.prop_null = false;
                return save();
            }
        }
        BinaryRecord r;
        r.active    = true;
        r.obj_id    = binary_alloc_id_();
        r.parent_id = binary_obj_id_of_("Database", "Database");
        if (r.parent_id == 0) r.parent_id = 1;
        r.obj_type  = "Relation";
        r.obj_name  = e.name;
        r.property  = prop;
        r.prop_null = false;
        binary_recs_.push_back(std::move(r));
    }
    return save();
}

util::Result<void> DataDict::remove_ri(const std::string& name) {
    ri_.erase(name);
    if (binary_format_) {
        for (auto& r : binary_recs_) {
            if (r.active && r.obj_type == "Relation" && r.obj_name == name) {
                r.active = false; break;
            }
        }
    }
    return save();
}

util::Result<void>
DataDict::set_db_property(const std::string& key, const std::string& value) {
    if (key.empty()) {
        return util::Error{5000, 0, "DD db-property key empty", ""};
    }
    db_props_[key] = value;
    return save();
}

std::string DataDict::get_db_property(const std::string& key) const {
    auto it = db_props_.find(key);
    return it == db_props_.end() ? std::string{} : it->second;
}

util::Result<void>
DataDict::set_user_property(const std::string& user,
                            const std::string& key,
                            const std::string& value) {
    if (user.empty() || key.empty()) {
        return util::Error{5000, 0, "DD user-property user / key empty", ""};
    }
    user_props_[user][key] = value;
    return save();
}

std::string
DataDict::get_user_property(const std::string& user,
                            const std::string& key) const {
    auto u = user_props_.find(user);
    if (u == user_props_.end()) return {};
    auto k = u->second.find(key);
    return k == u->second.end() ? std::string{} : k->second;
}

util::Result<void>
DataDict::set_table_permission(const std::string& table,
                                const std::string& user_or_group,
                                int level) {
    if (table.empty() || user_or_group.empty())
        return util::Error{5000, 0, "TABLEPERM table/user empty", ""};
    table_perms_[table][user_or_group] = level;

    // Mirror into permissions_ so system.permissions reflects GRANT/REVOKE.
    // Encode using SAP ADS_PERMISSION_* bitmask constants:
    //   bit0(0x01)=READ/SELECT  bit1(0x02)=UPDATE
    //   bit4(0x10)=INSERT       bit5(0x20)=DELETE
    //   bit31(0x80000000)=WITH_GRANT (used for FULL / level-4)
    uint32_t bitmask = 0;
    if (level >= 1) bitmask |= 0x01u;  // READ / SELECT
    if (level >= 2) bitmask |= 0x02u;  // UPDATE
    if (level >= 2) bitmask |= 0x10u;  // INSERT
    if (level >= 3) bitmask |= 0x20u;  // DELETE
    if (level >= 4) bitmask = 0x80000000u;  // FULL / WITH_GRANT sentinel

    // Update existing entry or add new one.
    for (auto& pe : permissions_) {
        if (pe.object_name == table && pe.grantee == user_or_group &&
            pe.object_type == "Table") {
            pe.bitmask = bitmask;
            return save();
        }
    }
    PermissionEntry pe;
    pe.object_name      = table;
    pe.object_type      = "Table";
    pe.object_type_code = 1;
    pe.grantee          = user_or_group;
    pe.grantee_is_group = (groups_.find(user_or_group) != groups_.end());
    pe.bitmask          = bitmask;
    permissions_.push_back(std::move(pe));

    return save();
}

// ---------------------------------------------------------------------------
// Per-operation effective permissions
// ---------------------------------------------------------------------------

// Translate a stored info2 bitmask + grantee context to an EffectiveOps.
//
// SAP binary .add files store 0x80000000 as a constant sentinel in info2 for
// ALL Permission records (both group and user ACL entries).  The actual per-bit
// rights (READ, UPDATE, INSERT, DELETE, …) are encoded in two encrypted 8-byte
// property-zone blobs using a proprietary SAP algorithm.  OpenADS cannot
// decode those blobs; for group grants we fall back to full DML.
//
// OpenADS-written Permission records store the actual ADS_PERMISSION_* bitmask
// directly in info2 (without encryption):
//   0x001=READ/SELECT  0x002=UPDATE   0x004=EXECUTE  0x008=INHERIT(meta, skip)
//   0x010=INSERT       0x020=DELETE
static DataDict::EffectiveOps ops_from_bitmask(uint32_t mask, bool is_group) {
    DataDict::EffectiveOps ops;
    const uint32_t SAP_SENTINEL = 0x80000000u;

    if (mask & SAP_SENTINEL) {
        // SAP-written sentinel: actual per-bit rights are in encrypted blobs.
        // For group ACL entries: assume full DML (SELECT + UPDATE + INSERT + DELETE).
        // For user ACL entries: 0x80000000 alone means INHERIT-only from groups;
        //   effective DML comes via group membership resolution in get_effective_ops.
        if (is_group) {
            ops.select_  = true;
            ops.update_  = true;
            ops.insert_  = true;
            ops.delete_  = true;
        }
    } else {
        // OpenADS-written bitmask: ADS_PERMISSION_* bit layout from ace.h.
        ops.select_  = (mask & 0x001u) != 0;  // ADS_PERMISSION_READ
        ops.update_  = (mask & 0x002u) != 0;  // ADS_PERMISSION_UPDATE
        ops.execute_ = (mask & 0x004u) != 0;  // ADS_PERMISSION_EXECUTE
        // 0x008 = ADS_PERMISSION_INHERIT — meta-flag, not a DML right, skip
        ops.insert_  = (mask & 0x010u) != 0;  // ADS_PERMISSION_INSERT
        ops.delete_  = (mask & 0x020u) != 0;  // ADS_PERMISSION_DELETE
    }
    return ops;
}

DataDict::EffectiveOps DataDict::get_effective_ops(
        const std::string& username,
        const std::string& object_name) const {

    // Collect the set of principals to check: user + all their groups.
    std::unordered_set<std::string> principals;
    principals.insert(username);
    auto mg = memberships_.find(username);
    if (mg != memberships_.end())
        for (const auto& g : mg->second)
            principals.insert(g);

    // Gather matching permission entries.
    EffectiveOps result;
    bool found_any = false;

    for (const auto& pe : permissions_) {
        if (pe.object_name != object_name) continue;
        if (principals.find(pe.grantee) == principals.end()) continue;

        found_any = true;
        auto contrib = ops_from_bitmask(pe.bitmask, pe.grantee_is_group);
        result.select_  |= contrib.select_;
        result.update_  |= contrib.update_;
        result.insert_  |= contrib.insert_;
        result.delete_  |= contrib.delete_;
        result.execute_ |= contrib.execute_;
    }

    if (!found_any) {
        // No ACL entry for this principal set → full open access.
        result.open     = true;
        result.select_  = true;
        result.update_  = true;
        result.insert_  = true;
        result.delete_  = true;
        result.execute_ = true;
    }
    return result;
}

std::vector<DataDict::EffectivePermEntry>
DataDict::get_all_effective_perms(const std::string& username) const {

    // Build set of principals.
    std::unordered_set<std::string> principals;
    principals.insert(username);
    auto mg = memberships_.find(username);
    if (mg != memberships_.end())
        for (const auto& g : mg->second)
            principals.insert(g);

    // Collect distinct objects visible to this user.
    // object_name → accumulated EffectiveOps
    std::unordered_map<std::string, EffectiveOps> acc;
    std::unordered_map<std::string, std::string>  obj_type_map;
    std::unordered_map<std::string, int>           obj_code_map;

    for (const auto& pe : permissions_) {
        if (principals.find(pe.grantee) == principals.end()) continue;
        auto& eo = acc[pe.object_name];
        auto contrib = ops_from_bitmask(pe.bitmask, pe.grantee_is_group);
        eo.select_  |= contrib.select_;
        eo.update_  |= contrib.update_;
        eo.insert_  |= contrib.insert_;
        eo.delete_  |= contrib.delete_;
        eo.execute_ |= contrib.execute_;
        obj_type_map[pe.object_name] = pe.object_type;
        obj_code_map[pe.object_name] = pe.object_type_code;
    }

    std::vector<EffectivePermEntry> result;
    result.reserve(acc.size());
    for (auto& kv : acc) {
        EffectivePermEntry e;
        e.object_name      = kv.first;
        e.object_type      = obj_type_map[kv.first];
        e.object_type_code = obj_code_map[kv.first];
        e.grantee          = username;
        e.ops              = kv.second;
        result.push_back(std::move(e));
    }
    return result;
}

// ---------------------------------------------------------------------------
// Legacy coarse-grained effective permission (kept for backward compat)
// ---------------------------------------------------------------------------

int DataDict::get_effective_permission(const std::string& username,
                                        const std::string& table) const {
    auto t = table_perms_.find(table);
    if (t == table_perms_.end()) return 4;   // no ACL → full access

    int eff = 0;
    auto u = t->second.find(username);
    if (u != t->second.end()) eff = u->second;

    auto mg = memberships_.find(username);
    if (mg != memberships_.end()) {
        for (const auto& g : mg->second) {
            auto gi = t->second.find(g);
            if (gi != t->second.end() && gi->second > eff)
                eff = gi->second;
        }
    }
    return eff;
}

// ---------------------------------------------------------------------------
// Field properties
// ---------------------------------------------------------------------------

util::Result<void>
DataDict::set_field_property(const std::string& table,
                              const std::string& field,
                              const std::string& key,
                              const std::string& value) {
    if (table.empty() || field.empty() || key.empty())
        return util::Error{5000, 0, "DD field-property table/field/key empty", ""};
    field_props_[table][field][key] = value;
    return save();
}

std::string
DataDict::get_field_property(const std::string& table,
                              const std::string& field,
                              const std::string& key) const {
    auto t = field_props_.find(table);
    if (t == field_props_.end()) return {};
    auto f = t->second.find(field);
    if (f == t->second.end()) return {};
    auto k = f->second.find(key);
    return k == f->second.end() ? std::string{} : k->second;
}

// ---------------------------------------------------------------------------
// Triggers
// ---------------------------------------------------------------------------

util::Result<void> DataDict::create_trigger(const TriggerEntry& e) {
    if (e.name.empty() || e.table_alias.empty())
        return util::Error{5000, 0, "DD trigger name/table empty", ""};
    triggers_[e.name] = e;
    if (binary_format_) {
        auto prop = join_nul({e.table_alias, std::to_string(e.event_mask),
                               std::to_string(e.priority),
                               e.enabled ? "1" : "0",
                               e.container, e.procedure, e.comment});
        for (auto& r : binary_recs_) {
            if (r.active && r.obj_type == "Trigger" && r.obj_name == e.name) {
                r.property = prop; r.prop_null = false;
                return save();
            }
        }
        BinaryRecord r;
        r.active    = true;
        r.obj_id    = binary_alloc_id_();
        r.parent_id = binary_obj_id_of_("Database", "Database");
        if (r.parent_id == 0) r.parent_id = 1;
        r.obj_type  = "Trigger";
        r.obj_name  = e.name;
        r.property  = prop;
        r.prop_null = false;
        binary_recs_.push_back(std::move(r));
    }
    return save();
}

util::Result<void> DataDict::drop_trigger(const std::string& name) {
    triggers_.erase(name);
    if (binary_format_) {
        for (auto& r : binary_recs_) {
            if (r.active && r.obj_type == "Trigger" && r.obj_name == name) {
                r.active = false; break;
            }
        }
    }
    return save();
}

// ---------------------------------------------------------------------------
// Stored procedures
// ---------------------------------------------------------------------------

util::Result<void> DataDict::create_proc(const ProcEntry& e) {
    if (e.name.empty())
        return util::Error{5000, 0, "DD proc name empty", ""};
    procs_[e.name] = e;
    if (binary_format_) {
        auto prop = join_nul({e.container, e.procedure,
                               e.input_params, e.output_params, e.comment});
        for (auto& r : binary_recs_) {
            if (r.active && (r.obj_type == "Procedure" || r.obj_type == "StoredProc") &&
                r.obj_name == e.name) {
                r.property = prop; r.prop_null = false;
                return save();
            }
        }
        BinaryRecord r;
        r.active    = true;
        r.obj_id    = binary_alloc_id_();
        r.parent_id = binary_obj_id_of_("Database", "Database");
        if (r.parent_id == 0) r.parent_id = 1;
        r.obj_type  = "StoredProc";
        r.obj_name  = e.name;
        r.property  = prop;
        r.prop_null = false;
        binary_recs_.push_back(std::move(r));
    }
    return save();
}

util::Result<void> DataDict::drop_proc(const std::string& name) {
    procs_.erase(name);
    if (binary_format_) {
        for (auto& r : binary_recs_) {
            if (r.active && (r.obj_type == "Procedure" || r.obj_type == "StoredProc") &&
                r.obj_name == name) {
                r.active = false; break;
            }
        }
    }
    return save();
}

// ---------------------------------------------------------------------------
// Views
// ---------------------------------------------------------------------------

util::Result<void> DataDict::create_view(const ViewEntry& e) {
    if (e.name.empty())
        return util::Error{5000, 0, "DD view name empty", ""};
    views_[e.name] = e;
    if (binary_format_) {
        auto prop = join_nul({e.comment, e.sql});
        for (auto& r : binary_recs_) {
            if (r.active && r.obj_type == "View" && r.obj_name == e.name) {
                r.property = prop; r.prop_null = false;
                return save();
            }
        }
        BinaryRecord r;
        r.active    = true;
        r.obj_id    = binary_alloc_id_();
        r.parent_id = binary_obj_id_of_("Database", "Database");
        if (r.parent_id == 0) r.parent_id = 1;
        r.obj_type  = "View";
        r.obj_name  = e.name;
        r.property  = prop;
        r.prop_null = false;
        binary_recs_.push_back(std::move(r));
    }
    return save();
}

util::Result<void> DataDict::drop_view(const std::string& name) {
    views_.erase(name);
    if (binary_format_) {
        for (auto& r : binary_recs_) {
            if (r.active && r.obj_type == "View" && r.obj_name == name) {
                r.active = false; break;
            }
        }
    }
    return save();
}

// ---------------------------------------------------------------------------
// Binary format helpers
// ---------------------------------------------------------------------------

std::uint32_t DataDict::binary_alloc_id_() {
    uint32_t id = le32(binary_hdr_, 0x50);
    put_le32(binary_hdr_, 0x50, id + 1);
    return id;
}

std::uint32_t DataDict::binary_obj_id_of_(const std::string& obj_type,
                                           const std::string& name) const {
    for (const auto& r : binary_recs_) {
        if (r.active && r.obj_type == obj_type && r.obj_name == name)
            return r.obj_id;
    }
    return 0;
}

std::string DataDict::serialize_binary_rec_(const BinaryRecord& r,
                                             uint32_t rec_len) {
    // Property VarChar field is always 275 bytes (2-byte prefix + 273 data).
    const uint32_t prop_field_len = 275;
    std::string rec(rec_len, '\0');

    rec[0] = static_cast<char>(r.active ? 0x04u : 0x05u);
    // bytes 1-4: null bitmap (all zero — no nullable fields in this format)
    put_le32(rec, 5, r.obj_id);
    put_le32(rec, 9, r.parent_id);

    // Object Type: CHAR(10) space-padded
    {
        std::size_t n = std::min(r.obj_type.size(), std::size_t{10});
        for (std::size_t i = 0; i < 10; ++i)
            rec[13 + i] = (i < n) ? r.obj_type[i] : ' ';
    }
    // Object Name: CHAR(200) space-padded
    {
        std::size_t n = std::min(r.obj_name.size(), std::size_t{200});
        for (std::size_t i = 0; i < 200; ++i)
            rec[23 + i] = (i < n) ? r.obj_name[i] : ' ';
    }
    // Property VarChar — prop_field_len bytes total at offset 223.
    if (r.prop_null) {
        rec[223] = static_cast<char>(0xFFu);
        rec[224] = static_cast<char>(0xFFu);
    } else {
        std::size_t max_data = prop_field_len - 2;
        std::size_t data_len = std::min(r.property.size(), max_data);
        put_le16(rec, 223, static_cast<uint16_t>(data_len));
        for (std::size_t i = 0; i < data_len; ++i)
            rec[225 + i] = r.property[i];
    }
    // More Property (9 bytes at 498)
    for (std::size_t j = 0; j < 9; ++j)
        rec[498u + j] = static_cast<char>(r.more_property[j]);
    put_le32(rec, 507, r.info1);
    put_le32(rec, 511, r.info2);
    // Comment (9 bytes at 515)
    for (std::size_t j = 0; j < 9; ++j)
        rec[515u + j] = static_cast<char>(r.comment[j]);

    return rec;
}

util::Result<void> DataDict::save_add_binary_() {
    // Regenerate property fields for mutable object types so that
    // AdsDDSet*Property mutations are reflected even without re-creating.
    for (auto& r : binary_recs_) {
        if (!r.active) continue;
        if (r.obj_type == "Procedure" || r.obj_type == "StoredProc") {
            auto it = procs_.find(r.obj_name);
            if (it != procs_.end()) {
                const auto& e = it->second;
                r.property  = join_nul({e.container, e.procedure,
                                         e.input_params, e.output_params, e.comment});
                r.prop_null = false;
            }
        } else if (r.obj_type == "Trigger") {
            auto it = triggers_.find(r.obj_name);
            if (it != triggers_.end()) {
                const auto& e = it->second;
                r.property  = join_nul({e.table_alias, std::to_string(e.event_mask),
                                         std::to_string(e.priority),
                                         e.enabled ? "1" : "0",
                                         e.container, e.procedure, e.comment});
                r.prop_null = false;
            }
        } else if (r.obj_type == "Relation") {
            auto it = ri_.find(r.obj_name);
            if (it != ri_.end()) {
                const auto& e = it->second;
                r.property  = join_nul({e.parent, e.child, e.parent_tag, e.child_tag,
                                         e.update_opt, e.delete_opt, e.fail_table});
                r.prop_null = false;
            }
        } else if (r.obj_type == "View") {
            auto it = views_.find(r.obj_name);
            if (it != views_.end()) {
                const auto& e = it->second;
                r.property  = join_nul({e.comment, e.sql});
                r.prop_null = false;
            }
        }
    }

    auto total = static_cast<uint32_t>(binary_recs_.size());
    put_le32(binary_hdr_, 0x18, total);

    std::string out;
    out.reserve(binary_hdr_len_ + total * binary_rec_len_);
    out.append(binary_hdr_);
    for (const auto& r : binary_recs_)
        out.append(serialize_binary_rec_(r, binary_rec_len_));

    auto fres = platform::File::open(path_, platform::OpenMode::CreateRW);
    if (!fres) return fres.error();
    auto file = std::move(fres).value();
    auto wrote = file.write_at(0, out.data(), out.size());
    if (!wrote) return wrote.error();
    if (wrote.value() != out.size())
        return util::Error{5000, 0, "short write on binary .add", path_};
    return file.sync();
}

util::Result<void> DataDict::create_group(const std::string& group) {
    if (group.empty())
        return util::Error{5000, 0, "DD group name empty", ""};
    groups_.insert(group);
    if (binary_format_) {
        for (const auto& r : binary_recs_) {
            if (r.active && r.obj_type == "Group" && r.obj_name == group)
                return save();
        }
        BinaryRecord r;
        r.active    = true;
        r.obj_id    = binary_alloc_id_();
        r.parent_id = binary_obj_id_of_("Database", "Database");
        if (r.parent_id == 0) r.parent_id = 1;
        r.obj_type  = "Group";
        r.obj_name  = group;
        r.prop_null = true;
        r.info1     = 0;
        r.info2     = 0;
        binary_recs_.push_back(std::move(r));
    }
    return save();
}

util::Result<void> DataDict::delete_group(const std::string& group) {
    groups_.erase(group);
    for (auto it = memberships_.begin(); it != memberships_.end(); ) {
        it->second.erase(group);
        if (it->second.empty()) it = memberships_.erase(it);
        else ++it;
    }
    if (binary_format_) {
        uint32_t group_id = binary_obj_id_of_("Group", group);
        for (auto& r : binary_recs_) {
            if (!r.active) continue;
            if (r.obj_type == "Group" && r.obj_name == group) {
                r.active = false;
            } else if (group_id != 0 && r.obj_type == "Permission" &&
                       r.info1 == group_id) {
                r.active = false;
            }
        }
    }
    return save();
}

// ---------------------------------------------------------------------------
// save (text format)
// ---------------------------------------------------------------------------

util::Result<void> DataDict::save() {
    if (binary_format_) return save_add_binary_();

    auto fres = platform::File::open(path_, platform::OpenMode::CreateRW);
    if (!fres) return fres.error();
    auto file = std::move(fres).value();

    std::string out = "# OpenADS Data Dictionary v1\n";

    auto sorted_keys = [](const auto& m) {
        std::vector<std::string> ks;
        ks.reserve(m.size());
        for (const auto& kv : m) ks.push_back(kv.first);
        std::sort(ks.begin(), ks.end());
        return ks;
    };

    for (auto& a : sorted_keys(tables_)) {
        out += "TABLE " + a + "=" + tables_.at(a) + "\n";
    }
    for (auto& e : indexes_) {
        out += "INDEX " + e.table_alias + "=" + e.index_path;
        if (!e.comment.empty()) { out += "\t"; out += e.comment; }
        out += "\n";
    }
    {
        std::vector<std::string> us(users_.begin(), users_.end());
        std::sort(us.begin(), us.end());
        for (auto& u : us) out += "USER " + u + "\n";
    }
    {
        std::vector<std::string> gs(groups_.begin(), groups_.end());
        std::sort(gs.begin(), gs.end());
        for (auto& g : gs) out += "GROUP " + g + "\n";
    }
    for (auto& u : sorted_keys(memberships_)) {
        std::vector<std::string> gs(memberships_.at(u).begin(),
                                    memberships_.at(u).end());
        std::sort(gs.begin(), gs.end());
        for (auto& g : gs) out += "MEMBER " + u + "=" + g + "\n";
    }
    for (auto& a : sorted_keys(links_)) {
        const auto& e = links_.at(a);
        out += "LINK " + a + "=" + e.path;
        if (!e.user.empty() || !e.pwd.empty()) {
            out += "\t"; out += e.user;
        }
        if (!e.pwd.empty()) { out += "\t"; out += e.pwd; }
        out += "\n";
    }
    for (auto& n : sorted_keys(ri_)) {
        const auto& e = ri_.at(n);
        out += "RI " + n + "=" + e.parent + ";" + e.child + ";" +
               e.parent_tag + ";" + e.child_tag + ";" +
               e.update_opt + ";" + e.delete_opt + ";" +
               e.fail_table + "\n";
    }
    for (auto& k : sorted_keys(db_props_)) {
        out += "DBPROP " + k + "=" + db_props_.at(k) + "\n";
    }
    for (auto& u : sorted_keys(user_props_)) {
        std::vector<std::string> ks;
        for (const auto& [k, _] : user_props_.at(u)) ks.push_back(k);
        std::sort(ks.begin(), ks.end());
        for (auto& k : ks) {
            out += "USERPROP " + u + ";" + k + "=" +
                   user_props_.at(u).at(k) + "\n";
        }
    }
    for (auto& t : sorted_keys(table_perms_)) {
        std::vector<std::string> ugs;
        for (const auto& [ug, _] : table_perms_.at(t)) ugs.push_back(ug);
        std::sort(ugs.begin(), ugs.end());
        for (auto& ug : ugs) {
            out += "TABLEPERM " + t + ";" + ug + "=" +
                   std::to_string(table_perms_.at(t).at(ug)) + "\n";
        }
    }
    for (auto& tbl : sorted_keys(field_props_)) {
        std::vector<std::string> flds;
        for (const auto& [f, _] : field_props_.at(tbl)) flds.push_back(f);
        std::sort(flds.begin(), flds.end());
        for (auto& fld : flds) {
            std::vector<std::string> ks;
            for (const auto& [k, _] : field_props_.at(tbl).at(fld)) ks.push_back(k);
            std::sort(ks.begin(), ks.end());
            for (auto& k : ks) {
                out += "FIELDPROP " + tbl + ";" + fld + ";" + k + "=" +
                       field_props_.at(tbl).at(fld).at(k) + "\n";
            }
        }
    }
    for (auto& n : sorted_keys(triggers_)) {
        const auto& e = triggers_.at(n);
        out += "TRIGGER " + n + "=" + e.table_alias + ";" +
               std::to_string(e.event_mask) + ";" +
               std::to_string(e.timing) + ";" +
               std::to_string(e.priority) + ";" +
               (e.enabled ? "1" : "0") + ";" +
               e.container + ";" + e.procedure + ";" + e.comment + "\n";
    }
    for (auto& n : sorted_keys(procs_)) {
        const auto& e = procs_.at(n);
        out += "PROC " + n + "=" + e.container + ";" + e.procedure + ";" +
               e.input_params + ";" + e.output_params + ";" + e.comment + "\n";
    }
    for (auto& n : sorted_keys(views_)) {
        const auto& e = views_.at(n);
        out += "VIEW " + n + "=" + e.comment + ";" + e.sql + "\n";
    }

    auto wrote = file.write_at(0, out.data(), out.size());
    if (!wrote) return wrote.error();
    if (auto s = file.sync(); !s) return s.error();
    return {};
}

} // namespace openads::engine
