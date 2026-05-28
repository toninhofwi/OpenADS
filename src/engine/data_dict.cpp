#include "engine/data_dict.h"

#include "platform/file.h"

#include <algorithm>
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
    // ADS Data Dictionary binary format.  Record layout (rec_len = 524):
    //   [0]       status 0x04=active, 0x05=deleted
    //   [1..4]    null bitmap (always zero in observed files)
    //   [5..8]    Object ID   (uint32 LE, AutoInc)
    //   [9..12]   Parent ID   (uint32 LE)
    //   [13..22]  Object Type (CHAR(10))
    //   [23..222] Object Name (CHAR(200))
    //   [223..497]Property    (VarChar(275): uint16 LE len + data; 0xFFFF=null)
    //   [498..506]More Property (Binary(9))
    //   [507..510]Info1       (uint32 LE)
    //   [511..514]Info2       (uint32 LE)
    //   [515..523]Comment     (Memo(9))
    //
    // Header fields used for write-back:
    //   0x18-0x1B: total record count (active + deleted)
    //   0x50-0x53: next ObjID to assign (= maxObjID + 1)
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

    std::size_t total = (buf.size() - hdr_len) / rec_len;
    binary_recs_.reserve(total);

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

        } else if ((rec.obj_type == "User" || rec.obj_type == "Group") &&
                   !rec.obj_name.empty()) {
            users_.insert(rec.obj_name);

        } else if (rec.obj_type == "Procedure" && !rec.obj_name.empty() &&
                   !rec.prop_null) {
            auto parts = split_nul(rec.property);
            while (parts.size() < 5) parts.emplace_back();
            ProcEntry e;
            e.name          = rec.obj_name;
            e.container     = parts[0];
            e.procedure     = parts[1];
            e.input_params  = parts[2];
            e.output_params = parts[3];
            e.comment       = parts[4];
            procs_[e.name] = std::move(e);

        } else if (rec.obj_type == "Trigger" && !rec.obj_name.empty() &&
                   !rec.prop_null) {
            auto parts = split_nul(rec.property);
            while (parts.size() < 7) parts.emplace_back();
            TriggerEntry e;
            e.name        = rec.obj_name;
            e.table_alias = parts[0];
            try { e.event_mask = static_cast<uint32_t>(std::stoul(parts[1])); } catch (...) {}
            try { e.priority   = static_cast<uint32_t>(std::stoul(parts[2])); } catch (...) {}
            e.enabled   = (parts[3] != "0");
            e.container = parts[4];
            e.procedure = parts[5];
            e.comment   = parts[6];
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
        }
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
            try { table_perms_[tbl][ug] = std::stoi(lvs); }
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
            // TRIGGER <name>=<table>;<event_mask>;<priority>;<enabled>;<container>;<procedure>;<comment>
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
            while (parts.size() < 7) parts.emplace_back();
            TriggerEntry e;
            e.name        = name;
            e.table_alias = parts[0];
            try { e.event_mask = static_cast<std::uint32_t>(std::stoul(parts[1])); } catch (...) {}
            try { e.priority   = static_cast<std::uint32_t>(std::stoul(parts[2])); } catch (...) {}
            e.enabled   = (parts[3] != "0");
            e.container = parts[4];
            e.procedure = parts[5];
            e.comment   = parts[6];
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
    return save();
}

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
            if (r.active && r.obj_type == "Procedure" && r.obj_name == e.name) {
                r.property = prop; r.prop_null = false;
                return save();
            }
        }
        BinaryRecord r;
        r.active    = true;
        r.obj_id    = binary_alloc_id_();
        r.parent_id = binary_obj_id_of_("Database", "Database");
        if (r.parent_id == 0) r.parent_id = 1;
        r.obj_type  = "Procedure";
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
            if (r.active && r.obj_type == "Procedure" && r.obj_name == name) {
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
        if (r.obj_type == "Procedure") {
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
    return save();
}

util::Result<void> DataDict::delete_group(const std::string& group) {
    groups_.erase(group);
    for (auto it = memberships_.begin(); it != memberships_.end(); ) {
        it->second.erase(group);
        if (it->second.empty()) it = memberships_.erase(it);
        else ++it;
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
