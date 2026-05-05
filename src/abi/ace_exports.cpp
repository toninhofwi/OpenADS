#include "openads/ace.h"
#include "openads/error.h"

#include "abi/charset.h"
#include "abi/last_error.h"

#include "engine/fts.h"
#include "engine/index_expr.h"
#include "engine/table.h"
#include "session/connection.h"
#include "session/handle_registry.h"
#include "drivers/dbf_common.h"
#include "drivers/index_trait.h"
#include "drivers/ntx/ntx_index.h"
#include "drivers/cdx/cdx_index.h"
#include "platform/time.h"
#include "sql/parser.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <thread>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {

using openads::engine::Table;
using openads::session::Connection;
using openads::session::Handle;
using openads::session::HandleKind;

struct ProcessState {
    std::mutex                                                    mu;
    openads::session::HandleRegistry                              registry;
    std::unordered_map<Handle, std::unique_ptr<Connection>>       conns;
};

ProcessState& state() {
    static ProcessState s;
    return s;
}

UNSIGNED32 ok() {
    openads::abi::clear_last_error();
    return openads::AE_SUCCESS;
}

UNSIGNED32 fail(const openads::util::Error& e) {
    openads::abi::set_last_error(e);
    return static_cast<UNSIGNED32>(e.code);
}

UNSIGNED32 fail(int code, const char* msg) {
    return fail(openads::util::Error{code, 0, msg ? msg : "", ""});
}

openads::engine::TableType map_type(UNSIGNED16 t) {
    switch (t) {
        case ADS_NTX: return openads::engine::TableType::Ntx;
        case ADS_CDX: return openads::engine::TableType::Cdx;
        case ADS_ADT: return openads::engine::TableType::Adt;
        case ADS_VFP: return openads::engine::TableType::Vfp;
        default:      return openads::engine::TableType::Cdx;
    }
}

UNSIGNED16 map_field_type(openads::drivers::DbfFieldType t) {
    using openads::drivers::DbfFieldType;
    // Constants verified empirically (M8.4) against
    // c:\harbour\lib\win\msvc64\rddads.lib — see include/openads/ace.h
    // for the full sweep table.
    switch (t) {
        case DbfFieldType::Character: return ADS_STRING;        //  4
        case DbfFieldType::Numeric:
        case DbfFieldType::Float:     return ADS_NUMERIC;       //  2
        case DbfFieldType::Logical:   return ADS_LOGICAL;       //  1
        case DbfFieldType::Date:      return ADS_DATE;          //  3
        case DbfFieldType::DateTime:  return ADS_TIMESTAMP;     // 14
        case DbfFieldType::Memo:      return ADS_MEMO;          //  5
        case DbfFieldType::Integer:   return ADS_INTEGER;       // 11
        case DbfFieldType::Currency:  return ADS_MONEY;         // 18
        case DbfFieldType::Double:    return ADS_DOUBLE;        // 10
        case DbfFieldType::Unknown:   return ADS_FIELD_TYPE_UNKNOWN;
    }
    return ADS_FIELD_TYPE_UNKNOWN;
}

const openads::drivers::DbfField*
find_field(Table* tbl, const std::string& name) {
    for (std::uint16_t i = 0; i < tbl->field_count(); ++i) {
        const auto& f = tbl->field_descriptor(i);
        if (f.name == name) return &f;
    }
    return nullptr;
}

// Cursor projections (M10.8). When a SELECT carries a projection
// list, the cursor handle's entry in this map holds the source-field
// indices (in projection order) so AdsGetNumFields / AdsGetFieldName
// / AdsGetField with ADSFIELD(n) report the projected schema instead
// of the underlying table's full layout.
std::unordered_map<ADSHANDLE, std::vector<std::uint16_t>>&
cursor_projections() {
    static std::unordered_map<ADSHANDLE, std::vector<std::uint16_t>> m;
    return m;
}

const std::vector<std::uint16_t>*
projection_for(ADSHANDLE h) {
    auto& m = cursor_projections();
    auto it = m.find(h);
    if (it == m.end() || it->second.empty()) return nullptr;
    return &it->second;
}

// Projection-aware variant. Called by Get* entry points that take
// hTable + pucField; routes ADSFIELD(n) numeric handles through the
// projection map (n = position within projection, translated to the
// underlying field index). Bare-name lookups stay direct — rddads
// only ever asks for projected names so the underlying schema's
// extra columns aren't reachable through the cursor anyway.
bool resolve_field_index(Table* tbl, UNSIGNED8* pucField, std::uint16_t* out);
bool resolve_field_index_h(ADSHANDLE h, Table* tbl,
                           UNSIGNED8* pucField, std::uint16_t* out) {
    auto p = reinterpret_cast<std::uintptr_t>(pucField);
    const auto* proj = projection_for(h);
    if (proj != nullptr && p != 0 && p < 0x10000u) {
        std::uint16_t one_based = static_cast<std::uint16_t>(p);
        if (one_based >= 1 && one_based <= proj->size()) {
            *out = (*proj)[one_based - 1];
            return true;
        }
        return false;
    }
    return resolve_field_index(tbl, pucField, out);
}

// Resolve a pucField argument to a 0-based field index. Real ACE.h
// defines `ADSFIELD(n)` as `((UNSIGNED8*)(UNSIGNED_PTR)(n))`, so
// callers compiled against that header pass small integers cast to
// pointers. Anything below 0x10000 cannot be a valid string address in
// any real process layout, so we treat it as a 1-based field index.
// Otherwise pucField is a NUL-terminated field name.
bool resolve_field_index(Table* tbl, UNSIGNED8* pucField, std::uint16_t* out) {
    if (tbl == nullptr || out == nullptr) return false;
    auto p = reinterpret_cast<std::uintptr_t>(pucField);
    if (p != 0 && p < 0x10000u) {
        std::uint16_t one_based = static_cast<std::uint16_t>(p);
        if (one_based >= 1 && one_based <= tbl->field_count()) {
            *out = static_cast<std::uint16_t>(one_based - 1);
            return true;
        }
        return false;
    }
    if (pucField == nullptr) return false;
    auto name = openads::abi::to_internal(pucField, 0);
    for (std::uint16_t i = 0; i < tbl->field_count(); ++i) {
        if (tbl->field_descriptor(i).name == name) { *out = i; return true; }
    }
    return false;
}

// lookup_table_by_index — defined further down once IndexBinding is
// known. Returns the Table bound to the given index handle, or null.
Table* lookup_table_by_index(ADSHANDLE h);
openads::drivers::IIndex* iindex_for_handle(ADSHANDLE h);
openads::util::Result<void> activate_binding(ADSHANDLE h);
void purge_bindings_for_table(Table* t);

Table* get_table(ADSHANDLE h) {
    auto& s = state();
    Table* t = s.registry.lookup<Table>(h, HandleKind::Table);
    if (t != nullptr) return t;
    // Real ACE accepts an index handle anywhere a table handle is
    // expected — rddads' adsGoTop calls AdsGotoTop(hOrdCurrent) when
    // an order is active. The bound Table is the same as the table's
    // own; we additionally swap the binding's parked IIndex into the
    // Table's active order so navigation actually walks the requested
    // tag (multi-tag CDX support, M8.9).
    Table* via_idx = lookup_table_by_index(h);
    if (via_idx != nullptr) {
        (void)activate_binding(h);
    }
    return via_idx;
}

} // namespace

// M9.16: chunked AdsSetBinary keeps a per-(table, field) accumulator;
// table teardown drains it. Forward-declared here so the close /
// disconnect paths above can call it before the definition arrives.
void purge_pending_binaries_for_table(openads::engine::Table* t);

extern "C" {

UNSIGNED32 AdsConnect60(UNSIGNED8* pucServer, UNSIGNED16 /*usServerType*/,
                        UNSIGNED8* /*pucUser*/, UNSIGNED8* /*pucPwd*/,
                        UNSIGNED32 /*ulOptions*/, ADSHANDLE* phConnect) {
    if (phConnect == nullptr) return fail(openads::AE_INTERNAL_ERROR,
                                          "phConnect is null");
    auto path = openads::abi::to_internal(pucServer, 0);
    auto opened = Connection::open(path);
    if (!opened) return fail(opened.error());
    auto holder = std::make_unique<Connection>(std::move(opened).value());
    Connection* raw = holder.get();
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    Handle h = s.registry.register_object(HandleKind::Connection, raw);
    s.conns.emplace(h, std::move(holder));
    *phConnect = h;
    return ok();
}

UNSIGNED32 AdsDisconnect(ADSHANDLE hConnect) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    // Purge any index bindings whose Table* belongs to a table owned
    // by this connection — otherwise the bindings outlive the conns
    // entry that owned the Table and leave dangling pointers behind.
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (c != nullptr) {
        std::vector<Table*> to_purge;
        s.registry.for_each_handle([&](Handle, HandleKind k, void* p) {
            if (k != HandleKind::Table) return;
            Table* tp = static_cast<Table*>(p);
            if (tp == nullptr) return;
            // Heuristic: if the connection's lookup_table on any handle
            // returns this pointer, the table belongs to this conn. We
            // don't have that handle here, so collect all tables and
            // purge any whose driver path lives under the conn's data
            // dir. Simpler: purge every Table* registered, since at
            // teardown the caller already closed its tables and any
            // stale residue is per-table.
            to_purge.push_back(tp);
        });
        for (Table* tp : to_purge) {
            purge_bindings_for_table(tp);
            purge_pending_binaries_for_table(tp);
        }
    }
    s.registry.release(hConnect);
    s.conns.erase(hConnect);
    return ok();
}

UNSIGNED32 AdsOpenTable(ADSHANDLE  hConnect,
                        UNSIGNED8* pucName,
                        UNSIGNED8* /*pucAlias*/,
                        UNSIGNED16 usTableType,
                        UNSIGNED16 /*usCharType*/,
                        UNSIGNED16 /*usLockType*/,
                        UNSIGNED16 /*usCheckRights*/,
                        UNSIGNED16 /*usMode*/,
                        ADSHANDLE* phTable) {
    if (phTable == nullptr) return fail(openads::AE_INTERNAL_ERROR,
                                        "phTable is null");
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    auto* conn = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE,
                                     "unknown connection");
    auto name = openads::abi::to_internal(pucName, 0);
    auto th = conn->open_table(name, map_type(usTableType));
    if (!th) return fail(th.error());
    Table* tbl = conn->lookup_table(th.value());
    Handle gh = s.registry.register_object(HandleKind::Table, tbl);
    *phTable = gh;
    return ok();
}

UNSIGNED32 AdsGetTableType(ADSHANDLE hTable, UNSIGNED16* pusType) {
    Table* t = get_table(hTable);
    if (!t || pusType == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    // Map our internal TableType back to ACE constants. We only own
    // CDX and NTX today; the rest are out of scope for phase 1.
    namespace fs = std::filesystem;
    fs::path p(t->path());
    auto ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(
                            static_cast<unsigned char>(c)));
    if (ext == ".dbf") {
        // Distinguishing CDX vs NTX vs VFP from a bare DBF requires
        // probing the matching index file alongside it, which we
        // don't do yet. Return CDX (the most common case).
        *pusType = ADS_CDX;
    } else if (ext == ".adt") {
        *pusType = ADS_ADT;
    } else {
        *pusType = ADS_CDX;
    }
    return ok();
}

UNSIGNED32 AdsGetTableFilename(ADSHANDLE hTable, UNSIGNED16 /*usOption*/,
                               UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    Table* t = get_table(hTable);
    if (!t || pusLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    openads::abi::copy_to_caller(pucBuf, pusLen, t->path());
    return ok();
}

UNSIGNED32 AdsGetRecordLength(ADSHANDLE hTable, UNSIGNED32* pulLen) {
    Table* t = get_table(hTable);
    if (!t || pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (t->driver() == nullptr) { *pulLen = 0; return ok(); }
    *pulLen = t->driver()->record_length();
    return ok();
}

extern "C++" {
namespace {

// Map an rddads type name (Character, Numeric, Date, ...) to the
// DBF field-descriptor type byte plus a default length / decimals
// when those aren't explicit in the field-def string.
struct DbfTypeSpec {
    char         type   = 'C';
    std::uint8_t length = 0;
    std::uint8_t dec    = 0;
    bool         needs_memo = false;
};

DbfTypeSpec dbf_type_for(const std::string& name) {
    auto eq = [&](const char* k) {
        if (name.size() != std::strlen(k)) return false;
        for (std::size_t i = 0; i < name.size(); ++i) {
            char a = static_cast<char>(std::tolower(
                            static_cast<unsigned char>(name[i])));
            char b = static_cast<char>(std::tolower(
                            static_cast<unsigned char>(k[i])));
            if (a != b) return false;
        }
        return true;
    };
    if (eq("Character") || eq("Char") || eq("CICHARACTER"))
        return {'C', 0, 0, false};
    if (eq("Numeric") || eq("Long") || eq("Number"))
        return {'N', 0, 0, false};
    if (eq("Logical") || eq("Bool"))
        return {'L', 1, 0, false};
    if (eq("Date") || eq("ShortDate"))
        return {'D', 8, 0, false};
    if (eq("Memo") || eq("NMemo"))
        return {'M', 10, 0, true};
    if (eq("Binary") || eq("Image"))
        return {'M', 10, 0, true};
    if (eq("Integer") || eq("ShortInt") || eq("LongLong"))
        return {'N', 0, 0, false};
    if (eq("Double") || eq("Money") || eq("CurDouble"))
        return {'N', 0, 0, false};
    if (eq("Time") || eq("Timestamp") || eq("ModTime"))
        return {'C', 23, 0, false};   // store as ISO-8601 string for now
    return {'C', 0, 0, false};        // unknown -> Character
}

// Trim leading/trailing whitespace.
std::string trim(std::string s) {
    while (!s.empty() && std::isspace(
                static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(
                static_cast<unsigned char>(s.back())))  s.pop_back();
    return s;
}

// rddads `NAME,Type,Len,Dec;…` parser. Empty `defs` returns an empty
// vector. Used by AdsCreateTable (M9.5) and AdsRestructureTable (M9.26).
struct FieldOut {
    std::string  name;
    char         type   = 'C';
    std::uint8_t length = 0;
    std::uint8_t dec    = 0;
};

std::vector<FieldOut> parse_rddads_field_defs(const std::string& defs) {
    std::vector<FieldOut> fields;
    std::string buf;
    auto flush = [&] {
        if (buf.empty()) return;
        std::vector<std::string> parts;
        std::string p;
        for (char c2 : buf) {
            if (c2 == ',') { parts.push_back(trim(p)); p.clear(); }
            else p.push_back(c2);
        }
        parts.push_back(trim(p));
        if (parts.size() >= 2) {
            DbfTypeSpec ts = dbf_type_for(parts[1]);
            FieldOut f;
            f.name = parts[0];
            if (f.name.size() > 10) f.name.resize(10);
            f.type = ts.type;
            f.length = ts.length;
            f.dec    = ts.dec;
            if (parts.size() >= 3) {
                int n = std::atoi(parts[2].c_str());
                if (n > 0 && n < 256) f.length = static_cast<std::uint8_t>(n);
            }
            if (parts.size() >= 4) {
                int d = std::atoi(parts[3].c_str());
                if (d >= 0 && d < 256) f.dec = static_cast<std::uint8_t>(d);
            }
            if (f.length == 0) f.length = 10;
            fields.push_back(std::move(f));
        }
        buf.clear();
    };
    for (std::size_t i = 0; i <= defs.size(); ++i) {
        char ch = (i < defs.size()) ? defs[i] : ';';
        if (ch == ';') flush();
        else           buf.push_back(ch);
    }
    return fields;
}

} // namespace
} // extern "C++"

UNSIGNED32 AdsCreateTable(ADSHANDLE     hConn,
                          UNSIGNED8*    pucName,
                          UNSIGNED8*    /*pucAlias*/,
                          UNSIGNED16    /*usTableType*/,
                          UNSIGNED16    /*usCharType*/,
                          UNSIGNED16    /*usLockType*/,
                          UNSIGNED16    /*usCheckRights*/,
                          UNSIGNED16    /*usMemoBlockSize*/,
                          UNSIGNED8*    pucFields,
                          ADSHANDLE*    phTable) {
    if (pucName == nullptr || pucFields == nullptr || phTable == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "null arg");
    }
    auto& s = state();
    Connection* c = s.registry.lookup<Connection>(hConn,
                            HandleKind::Connection);
    if (c == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }

    auto rel  = openads::abi::to_internal(pucName, 0);
    auto defs = openads::abi::to_internal(pucFields, 0);

    namespace fs = std::filesystem;
    fs::path full = fs::path(c->data_dir()) / rel;
    if (!full.has_extension()) full.replace_extension(".dbf");

    auto fields = parse_rddads_field_defs(defs);
    if (fields.empty()) {
        return fail(openads::AE_INTERNAL_ERROR, "no fields");
    }

    // Compute header + record sizes.
    std::uint16_t header_len = static_cast<std::uint16_t>(
        32 + 32 * fields.size() + 1);
    std::uint32_t rec_len = 1; // delete-flag byte
    for (auto& f : fields) rec_len += f.length;
    if (rec_len > 0xFFFF) {
        return fail(openads::AE_INTERNAL_ERROR, "record too long");
    }

    std::vector<std::uint8_t> hdr(32, 0);
    hdr[0]  = 0x03;                                            // dBASE III
    hdr[4]  = 0; hdr[5] = 0; hdr[6] = 0; hdr[7] = 0;           // 0 records
    hdr[8]  = static_cast<std::uint8_t>(header_len & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>(rec_len & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rec_len >> 8) & 0xFFu);

    std::vector<std::uint8_t> file = hdr;
    for (auto& f : fields) {
        std::vector<std::uint8_t> fd(32, 0);
        std::size_t n = std::min<std::size_t>(f.name.size(), 10);
        std::memcpy(fd.data(), f.name.data(), n);
        fd[11] = static_cast<std::uint8_t>(f.type);
        fd[16] = f.length;
        fd[17] = f.dec;
        file.insert(file.end(), fd.begin(), fd.end());
    }
    file.push_back(0x0D);
    file.push_back(0x1A);

    // Atomic-ish write: just truncate-create.
    {
        std::error_code ec;
        fs::remove(full, ec);
    }
    {
        std::ofstream out(full, std::ios::binary);
        if (!out) return fail(openads::AE_INTERNAL_ERROR,
                              "AdsCreateTable: open for write failed");
        out.write(reinterpret_cast<const char*>(file.data()),
                  static_cast<std::streamsize>(file.size()));
        if (!out) return fail(openads::AE_INTERNAL_ERROR,
                              "AdsCreateTable: write failed");
    }

    // Open the freshly-created table through the regular path so the
    // caller gets a usable handle.
    UNSIGNED8 namebuf[260] = {0};
    std::size_t nb = std::min<std::size_t>(rel.size(), sizeof(namebuf) - 1);
    std::memcpy(namebuf, rel.data(), nb);
    return AdsOpenTable(hConn, namebuf, namebuf,
                        ADS_CDX,    // table type
                        0, 0, 0, 1, // char/lock/checkrights/mode
                        phTable);
}

// --- M9.26 AdsRestructureTable (ADD-only) ----------------------------------
//
// Real ACE rebuilds the DBF with three field-def strings — add,
// delete, and change. The most common rddads call site only feeds
// the "add" list (`pucDeleteFields` / `pucChangeFields` empty), which
// is what 0.2.x supports. Non-empty delete / change lists return
// AE_FUNCTION_NOT_AVAILABLE until the 0.3.x VFP / ADT structural
// extensions land.
//
// Indexes are NOT auto-rebuilt (real ACE handles that internally).
// Apps that depend on a bound index after a restructure should
// follow up with AdsReindex; the on-disk record format changed, so
// stale entries point at the wrong recnos.

UNSIGNED32 AdsRestructureTable(ADSHANDLE   hConnect,
                               UNSIGNED8*  pucTableName,
                               UNSIGNED8*  /*pucAlias*/,
                               UNSIGNED16  /*usFileType*/,
                               UNSIGNED16  /*usCharType*/,
                               UNSIGNED16  /*usLockType*/,
                               UNSIGNED16  /*usCheckRights*/,
                               UNSIGNED8*  pucAddFields,
                               UNSIGNED8*  pucDeleteFields,
                               UNSIGNED8*  pucChangeFields) {
    if (pucTableName == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR,
                    "AdsRestructureTable: null table name");
    }
    auto& s = state();
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (c == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    auto del = pucDeleteFields ? openads::abi::to_internal(pucDeleteFields, 0)
                               : std::string();
    auto chg = pucChangeFields ? openads::abi::to_internal(pucChangeFields, 0)
                               : std::string();

    auto add = pucAddFields ? openads::abi::to_internal(pucAddFields, 0)
                            : std::string();
    auto add_fields = parse_rddads_field_defs(add);

    // CHANGE list (M10.12): same shape as ADD (NAME,Type,Len,Dec;…).
    // Each entry replaces the same-named existing field's length /
    // decimals. The Type must match the existing field — type
    // conversion (rename / retype) needs a clean-room ADS spec and
    // stays deferred. Apps that need it can issue DELETE + ADD.
    auto change_fields = parse_rddads_field_defs(chg);
    std::unordered_map<std::string, FieldOut> change_map;
    for (auto& cf : change_fields) {
        change_map[cf.name] = cf;
    }

    // DELETE list is a `;`-separated list of bare field names —
    // unlike pucAddFields the entries carry no type / len info.
    std::unordered_set<std::string> del_set;
    {
        std::string buf;
        auto flush = [&] {
            std::string trimmed = buf;
            while (!trimmed.empty() &&
                   std::isspace(static_cast<unsigned char>(trimmed.front()))) {
                trimmed.erase(trimmed.begin());
            }
            while (!trimmed.empty() &&
                   std::isspace(static_cast<unsigned char>(trimmed.back()))) {
                trimmed.pop_back();
            }
            if (!trimmed.empty()) del_set.insert(trimmed);
            buf.clear();
        };
        for (std::size_t i = 0; i <= del.size(); ++i) {
            char ch = (i < del.size()) ? del[i] : ';';
            if (ch == ';' || ch == ',') flush();
            else buf.push_back(ch);
        }
    }

    if (add_fields.empty() && del_set.empty() && change_fields.empty()) {
        return ok();   // nothing to do
    }

    auto rel = openads::abi::to_internal(pucTableName, 0);
    namespace fs = std::filesystem;
    fs::path full = fs::path(c->data_dir()) / rel;
    if (!full.has_extension()) full.replace_extension(".dbf");

    fs::path tmp = full;
    tmp += ".restructure.tmp";
    {
        std::error_code ec;
        fs::remove(tmp, ec);
    }

    // Read the source schema + record bytes inside an inner scope so
    // the engine's File handle on `full` is closed before the rename.
    {
        auto opened = openads::engine::Table::open(
            full.string(), openads::engine::TableType::Cdx,
            openads::engine::OpenMode::Read);
        if (!opened) return fail(opened.error());
        auto& t = opened.value();

        // Per-field copy plan: keep the source order, drop fields that
        // appear in the DELETE list, apply the CHANGE list's
        // length/decimals overrides for matching surviving fields,
        // then append the ADD list. Each surviving field tracks where
        // to copy from in the old record (or no source for newly-added
        // columns).
        struct PerField {
            FieldOut       descriptor;
            bool           from_old      = false;
            std::uint16_t  old_offset    = 0;
            std::uint8_t   old_length    = 0;
        };
        std::vector<PerField> plan;
        for (std::uint16_t i = 0; i < t.field_count(); ++i) {
            const auto& src = t.field_descriptor(i);
            if (del_set.find(src.name) != del_set.end()) continue;
            PerField p;
            p.descriptor.name   = src.name;
            p.descriptor.type   = src.raw_type;
            p.descriptor.length = src.length;
            p.descriptor.dec    = src.decimals;
            p.from_old   = true;
            p.old_offset = src.record_offset;
            p.old_length = src.length;

            auto cit = change_map.find(src.name);
            if (cit != change_map.end()) {
                if (cit->second.type != src.raw_type) {
                    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                                "AdsRestructureTable: CHANGE type "
                                "conversion deferred — name + type "
                                "must match the existing column");
                }
                p.descriptor.length = cit->second.length;
                p.descriptor.dec    = cit->second.dec;
            }
            plan.push_back(std::move(p));
        }
        for (auto& nf : add_fields) {
            for (auto& existing : plan) {
                if (existing.descriptor.name == nf.name) {
                    return fail(openads::AE_INTERNAL_ERROR,
                                "AdsRestructureTable: duplicate field name");
                }
            }
            PerField p;
            p.descriptor = nf;
            p.from_old   = false;
            plan.push_back(std::move(p));
        }
        if (plan.empty()) {
            return fail(openads::AE_INTERNAL_ERROR,
                        "AdsRestructureTable: every field deleted "
                        "without an ADD — would leave the table empty");
        }
        std::vector<FieldOut> merged;
        merged.reserve(plan.size());
        for (auto& p : plan) merged.push_back(p.descriptor);

        std::uint16_t header_len = static_cast<std::uint16_t>(
            32 + 32 * merged.size() + 1);
        std::uint32_t rec_len = 1;
        for (auto& f : merged) rec_len += f.length;
        if (rec_len > 0xFFFF) {
            return fail(openads::AE_INTERNAL_ERROR,
                        "AdsRestructureTable: record exceeds 64 KiB");
        }

        std::vector<std::uint8_t> hdr(32, 0);
        hdr[0]  = 0x03;
        std::uint32_t rcount = t.record_count();
        hdr[4]  = static_cast<std::uint8_t>( rcount        & 0xFFu);
        hdr[5]  = static_cast<std::uint8_t>((rcount >> 8)  & 0xFFu);
        hdr[6]  = static_cast<std::uint8_t>((rcount >> 16) & 0xFFu);
        hdr[7]  = static_cast<std::uint8_t>((rcount >> 24) & 0xFFu);
        hdr[8]  = static_cast<std::uint8_t>(header_len & 0xFFu);
        hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
        hdr[10] = static_cast<std::uint8_t>(rec_len & 0xFFu);
        hdr[11] = static_cast<std::uint8_t>((rec_len >> 8) & 0xFFu);

        std::vector<std::uint8_t> file_bytes = hdr;
        for (auto& f : merged) {
            std::vector<std::uint8_t> fd(32, 0);
            std::size_t n = std::min<std::size_t>(f.name.size(), 10);
            std::memcpy(fd.data(), f.name.data(), n);
            fd[11] = static_cast<std::uint8_t>(f.type);
            fd[16] = f.length;
            fd[17] = f.dec;
            file_bytes.insert(file_bytes.end(), fd.begin(), fd.end());
        }
        file_bytes.push_back(0x0D);

        std::uint16_t old_rec_len = t.driver()->record_length();
        for (std::uint32_t r = 1; r <= rcount; ++r) {
            auto rec = t.driver()->read_record_raw(r);
            if (!rec) return fail(rec.error());
            std::vector<std::uint8_t> old_buf = std::move(rec).value();

            std::vector<std::uint8_t> new_buf(rec_len, ' ');
            new_buf[0] = old_buf.empty() ? ' ' : old_buf[0];
            std::uint16_t out_off = 1;
            for (auto& p : plan) {
                if (p.from_old) {
                    std::uint8_t copy_len =
                        std::min<std::uint8_t>(p.old_length,
                                               p.descriptor.length);
                    if (old_buf.size() >=
                        static_cast<std::size_t>(p.old_offset) +
                        static_cast<std::size_t>(copy_len)) {
                        std::memcpy(new_buf.data() + out_off,
                                    old_buf.data() + p.old_offset,
                                    copy_len);
                    }
                    // Tail bytes (when new length > old length) stay
                    // as the blank-pad already in new_buf.
                }
                out_off = static_cast<std::uint16_t>(
                    out_off + p.descriptor.length);
            }
            (void)old_rec_len;
            file_bytes.insert(file_bytes.end(),
                              new_buf.begin(), new_buf.end());
        }
        file_bytes.push_back(0x1A);

        std::ofstream out(tmp, std::ios::binary);
        if (!out) return fail(openads::AE_INTERNAL_ERROR,
                              "AdsRestructureTable: tmp open failed");
        out.write(reinterpret_cast<const char*>(file_bytes.data()),
                  static_cast<std::streamsize>(file_bytes.size()));
        if (!out) return fail(openads::AE_INTERNAL_ERROR,
                              "AdsRestructureTable: tmp write failed");
    }   // engine handle on `full` closes here

    {
        std::error_code ec;
        fs::remove(full, ec);
        fs::rename(tmp, full, ec);
        if (ec) {
            return fail(openads::AE_INTERNAL_ERROR,
                        "AdsRestructureTable: rename failed");
        }
    }
    return ok();
}

UNSIGNED32 AdsRefreshRecord(ADSHANDLE hTable) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    if (t->eof() || t->bof() || t->recno() == 0) return ok();
    auto r = t->goto_record(t->recno());
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsExtractKey(ADSHANDLE hIndex, UNSIGNED8* pucBuf,
                         UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "null len");
    Table* t = lookup_table_by_index(hIndex);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    openads::drivers::IIndex* idx = iindex_for_handle(hIndex);
    if (!idx) return fail(openads::AE_INTERNAL_ERROR, "index not loaded");
    auto k = openads::engine::evaluate_index_expr(*t, idx->expression(),
                                                  idx->key_length());
    if (!k) return fail(k.error());
    openads::abi::copy_to_caller(pucBuf, pusLen, k.value());
    return ok();
}

UNSIGNED32 AdsGotoRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->goto_record(ulRecord);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsCheckExistence(ADSHANDLE /*hConn*/, UNSIGNED8* pucName,
                             UNSIGNED16* pbExists) {
    if (pbExists == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "null out");
    }
    if (pucName == nullptr) {
        *pbExists = 0;
        return ok();
    }
    auto path = openads::abi::to_internal(pucName, 0);
    std::error_code ec;
    *pbExists = std::filesystem::exists(path, ec) ? 1 : 0;
    return ok();
}

UNSIGNED32 AdsDeleteFile(ADSHANDLE /*hConn*/, UNSIGNED8* pucName) {
    if (pucName == nullptr) return fail(openads::AE_INTERNAL_ERROR, "null name");
    auto path = openads::abi::to_internal(pucName, 0);
    std::error_code ec;
    if (!std::filesystem::remove(path, ec)) {
        return fail(openads::AE_INTERNAL_ERROR,
                    "AdsDeleteFile: file not found / cannot remove");
    }
    return ok();
}

UNSIGNED32 AdsCloseAllTables(ADSHANDLE hConn) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConn,
                            HandleKind::Connection);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    // Walk every Table handle that points to a Table belonging to
    // this connection and release it. Connection's tables_ map owns
    // the unique_ptrs; the handle registry just borrows pointers.
    std::vector<Handle> to_release;
    s.registry.for_each_handle([&](Handle h, HandleKind k, void* p) {
        if (k != HandleKind::Table) return;
        Table* tp = static_cast<Table*>(p);
        if (tp == nullptr) return;
        // Table belongs to this connection if c->lookup_table on its
        // handle returns the same pointer. We don't track the
        // back-edge directly, so iterate all Connection's table
        // handles instead.
        (void)tp;
        to_release.push_back(h);
    });
    for (Handle h : to_release) {
        Table* t = s.registry.lookup<Table>(h, HandleKind::Table);
        if (t) {
            (void)t->flush();
            purge_bindings_for_table(t);
            purge_pending_binaries_for_table(t);
        }
        s.registry.release(h);
    }
    return ok();
}

UNSIGNED32 AdsCloseTable(ADSHANDLE hTable) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    // Flush the table (driver + active order + extra index views)
    // before releasing the handle. Without an explicit transaction,
    // this is the only point at which mutations made since open
    // reach disk; with one, commit_tx already flushed and this is
    // a no-op. Also drop any index bindings tied to this Table so
    // a future Table allocation at the same heap address doesn't
    // inherit stale entries.
    Table* t = s.registry.lookup<Table>(hTable, HandleKind::Table);
    if (t != nullptr) {
        (void)t->flush();
        purge_bindings_for_table(t);
        purge_pending_binaries_for_table(t);
    }
    cursor_projections().erase(hTable);
    s.registry.release(hTable);
    return ok();
}

UNSIGNED32 AdsGotoTop(ADSHANDLE hTable) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->goto_top();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsGotoBottom(ADSHANDLE hTable) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->goto_bottom();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsSkip(ADSHANDLE hTable, SIGNED32 lRows) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->skip(lRows);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsAtEOF(ADSHANDLE hTable, UNSIGNED16* pbAtEnd) {
    Table* t = get_table(hTable);
    if (!t || pbAtEnd == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *pbAtEnd = t->eof() ? 1 : 0;
    return ok();
}

UNSIGNED32 AdsAtBOF(ADSHANDLE hTable, UNSIGNED16* pbAtBegin) {
    Table* t = get_table(hTable);
    if (!t || pbAtBegin == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *pbAtBegin = t->bof() ? 1 : 0;
    return ok();
}

UNSIGNED32 AdsGetNumFields(ADSHANDLE hTable, UNSIGNED16* pusFields) {
    Table* t = get_table(hTable);
    if (!t || pusFields == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* p = projection_for(hTable); p != nullptr) {
        *pusFields = static_cast<UNSIGNED16>(p->size());
    } else {
        *pusFields = t->field_count();
    }
    return ok();
}

UNSIGNED32 AdsGetFieldName(ADSHANDLE hTable, UNSIGNED16 usFieldNum,
                           UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto* p = projection_for(hTable);
    std::uint16_t src_idx = 0;
    if (p != nullptr) {
        if (usFieldNum == 0 || usFieldNum > p->size()) {
            return fail(openads::AE_COLUMN_NOT_FOUND, "");
        }
        src_idx = (*p)[usFieldNum - 1];
    } else {
        if (usFieldNum == 0 || usFieldNum > t->field_count()) {
            return fail(openads::AE_COLUMN_NOT_FOUND, "field index out of range");
        }
        src_idx = static_cast<std::uint16_t>(usFieldNum - 1);
    }
    const auto& f = t->field_descriptor(src_idx);
    openads::abi::copy_to_caller(pucBuf, pusLen, f.name);
    return ok();
}

UNSIGNED32 AdsGetFieldType(ADSHANDLE hTable, UNSIGNED8* pucField,
                           UNSIGNED16* pusType) {
    Table* t = get_table(hTable);
    if (!t || pusType == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index_h(hTable, t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pusType = map_field_type(t->field_descriptor(idx).type);
    return ok();
}

UNSIGNED32 AdsGetFieldLength(ADSHANDLE hTable, UNSIGNED8* pucField,
                             UNSIGNED32* pulLen) {
    Table* t = get_table(hTable);
    if (!t || pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index_h(hTable, t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pulLen = t->field_descriptor(idx).length;
    return ok();
}

UNSIGNED32 AdsGetFieldDecimals(ADSHANDLE hTable, UNSIGNED8* pucField,
                               UNSIGNED16* pusDec) {
    Table* t = get_table(hTable);
    if (!t || pusDec == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index_h(hTable, t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pusDec = t->field_descriptor(idx).decimals;
    return ok();
}

UNSIGNED32 AdsGetLong(ADSHANDLE hTable, UNSIGNED8* pucField, SIGNED32* plVal) {
    Table* t = get_table(hTable);
    if (!t || plVal == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    *plVal = static_cast<SIGNED32>(v.value().as_double);
    return ok();
}

UNSIGNED32 AdsGetDouble(ADSHANDLE hTable, UNSIGNED8* pucField, double* pdVal) {
    Table* t = get_table(hTable);
    if (!t || pdVal == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    *pdVal = v.value().as_double;
    return ok();
}

namespace {

// Gregorian -> Clipper/Harbour Julian Day Number. Same formula as
// hb_dateEncode in Harbour core.
SIGNED32 to_julian(int y, int m, int d) {
    long y32 = y;
    long m32 = m;
    long d32 = d;
    return static_cast<SIGNED32>(
        (1461 * (y32 + 4800 + (m32 - 14) / 12)) / 4
      + (367  * (m32 - 2 - 12 * ((m32 - 14) / 12))) / 12
      - (3    * ((y32 + 4900 + (m32 - 14) / 12) / 100)) / 4
      + d32 - 32075);
}

} // namespace

UNSIGNED32 AdsGetJulian(ADSHANDLE hTable, UNSIGNED8* pucField, SIGNED32* plDate) {
    Table* t = get_table(hTable);
    if (!t || plDate == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    const std::string& s = v.value().as_string;
    *plDate = 0;
    if (s.size() >= 8) {
        int y = (s[0] - '0') * 1000 + (s[1] - '0') * 100
              + (s[2] - '0') * 10   + (s[3] - '0');
        int m = (s[4] - '0') * 10   + (s[5] - '0');
        int d = (s[6] - '0') * 10   + (s[7] - '0');
        if (y > 0 && m >= 1 && m <= 12 && d >= 1 && d <= 31) {
            *plDate = to_julian(y, m, d);
        }
    }
    return ok();
}

UNSIGNED32 AdsGetRecordNum(ADSHANDLE hTable, UNSIGNED16 /*bFilterOption*/,
                           UNSIGNED32* pulRecordNum) {
    Table* t = get_table(hTable);
    if (!t || pulRecordNum == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *pulRecordNum = t->recno();
    return ok();
}

UNSIGNED32 AdsGetRecordCount(ADSHANDLE hTable, UNSIGNED16 /*bFilterOption*/,
                             UNSIGNED32* pulRecordCount) {
    Table* t = get_table(hTable);
    if (!t || pulRecordCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *pulRecordCount = t->record_count();
    return ok();
}

UNSIGNED32 AdsGetField(ADSHANDLE hTable, UNSIGNED8* pucField,
                       UNSIGNED8* pucBuf, UNSIGNED32* pulLen,
                       UNSIGNED16 /*usOption*/) {
    Table* t = get_table(hTable);
    if (!t || pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index_h(hTable, t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    UNSIGNED16 cap = static_cast<UNSIGNED16>(
        *pulLen > 0xFFFFu ? 0xFFFFu : *pulLen);
    UNSIGNED16 cap_inout = cap;
    openads::abi::copy_to_caller(pucBuf, &cap_inout, v.value().as_string);
    *pulLen = cap_inout;
    return ok();
}

UNSIGNED32 AdsGetLastError(UNSIGNED32* pulCode, UNSIGNED8* pucBuf,
                           UNSIGNED16* pusBufLen) {
    if (pulCode != nullptr) *pulCode = static_cast<UNSIGNED32>(
        openads::abi::last_error_code());
    if (pucBuf != nullptr && pusBufLen != nullptr) {
        openads::abi::copy_to_caller(pucBuf, pusBufLen,
                                     openads::abi::last_error_message());
    }
    return openads::AE_SUCCESS;
}

UNSIGNED32 AdsGetVersion(UNSIGNED32* pulMajor, UNSIGNED32* pulMinor,
                         UNSIGNED32* pulLetter, UNSIGNED32* pulDesc) {
    if (pulMajor != nullptr)  *pulMajor  = 0;
    if (pulMinor != nullptr)  *pulMinor  = 0;
    if (pulLetter != nullptr) *pulLetter = 'a';
    if (pulDesc != nullptr)   *pulDesc   = 1;
    return openads::AE_SUCCESS;
}

// --- M9.15 server info -----------------------------------------------------
//
// Local-mode connections now report the host name + the local wall clock
// instead of empty strings / 0. AdsGetServerTime returns a six-arg shape
// matching the ACE 6.x signature rddads' ADSGETSERVERTIME function expects
// (date string, time string, milliseconds since midnight) — the previous
// 2-arg stub left rddads' on-stack pucDateBuf / pucTimeBuf uninitialised.

namespace {

UNSIGNED32 emit_text_with_u16len(UNSIGNED8* pucBuf, UNSIGNED16* pusLen,
                                 const std::string& s) {
    if (pusLen == nullptr) return openads::AE_INTERNAL_ERROR;
    UNSIGNED16 cap = *pusLen;
    UNSIGNED16 n = static_cast<UNSIGNED16>(s.size() < cap ? s.size() : cap);
    if (pucBuf != nullptr && cap > 0) {
        if (n > 0) std::memcpy(pucBuf, s.data(), n);
        if (n < cap) pucBuf[n] = '\0';
    }
    *pusLen = static_cast<UNSIGNED16>(s.size());
    return openads::AE_SUCCESS;
}

}  // namespace

UNSIGNED32 AdsGetServerName(ADSHANDLE /*hConnect*/,
                            UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    return emit_text_with_u16len(pucBuf, pusLen,
                                 openads::platform::host_name());
}

UNSIGNED32 AdsGetServerTime(ADSHANDLE  /*hConnect*/,
                            UNSIGNED8* pucDateBuf, UNSIGNED16* pusDateLen,
                            SIGNED32*  plTime,
                            UNSIGNED8* pucTimeBuf, UNSIGNED16* pusTimeLen) {
    auto wc = openads::platform::now_local();
    auto rc = emit_text_with_u16len(pucDateBuf, pusDateLen, wc.date);
    if (rc != openads::AE_SUCCESS) return rc;
    rc = emit_text_with_u16len(pucTimeBuf, pusTimeLen, wc.time);
    if (rc != openads::AE_SUCCESS) return rc;
    if (plTime != nullptr) *plTime = wc.ms_of_day;
    return openads::AE_SUCCESS;
}

UNSIGNED32 AdsAppendRecord(ADSHANDLE hTable) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->append_record();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsWriteRecord(ADSHANDLE hTable) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->flush();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDeleteRecord(ADSHANDLE hTable) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->mark_deleted();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsRecallRecord(ADSHANDLE hTable) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->recall_deleted();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsIsRecordDeleted(ADSHANDLE hTable, UNSIGNED16* pbDeleted) {
    Table* t = get_table(hTable);
    if (!t || pbDeleted == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *pbDeleted = t->is_deleted() ? 1 : 0;
    return ok();
}

UNSIGNED32 AdsSetString(ADSHANDLE hTable, UNSIGNED8* pucField,
                        UNSIGNED8* pucValue, UNSIGNED32 ulLen) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    std::string val(reinterpret_cast<const char*>(pucValue), ulLen);
    auto r = t->set_field(idx, val);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsSetLogical(ADSHANDLE hTable, UNSIGNED8* pucField,
                         UNSIGNED16 bValue) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto r = t->set_field(idx, bValue != 0);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsSetDouble(ADSHANDLE hTable, UNSIGNED8* pucField,
                        double dValue) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto r = t->set_field(idx, dValue);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsSetLongLong(ADSHANDLE hTable, UNSIGNED8* pucField,
                          std::int64_t llValue) {
    return AdsSetDouble(hTable, pucField, static_cast<double>(llValue));
}

namespace {

// Inverse of to_julian — convert a Clipper Julian Day Number back to
// a Gregorian (Y, M, D) triple.
void julian_to_ymd(SIGNED32 jd, int& y, int& m, int& d) {
    long L = static_cast<long>(jd) + 68569;
    long N = (4 * L) / 146097;
    L = L - (146097 * N + 3) / 4;
    long I = (4000 * (L + 1)) / 1461001;
    L = L - (1461 * I) / 4 + 31;
    long J = (80 * L) / 2447;
    d = static_cast<int>(L - (2447 * J) / 80);
    L = J / 11;
    m = static_cast<int>(J + 2 - 12 * L);
    y = static_cast<int>(100 * (N - 49) + I + L);
}

} // namespace

// Memo readers: rddads' adsGetValue routes HB_FT_MEMO fields through
// AdsGetMemoDataType + AdsGetMemoLength + AdsGetString. The first
// reports the memo's content type (text vs binary), the second the
// payload length, and the third copies the bytes into the caller's
// buffer. We resolve the field as before, fetch the memo block via
// the attached IMemoStore, and answer in kind.
UNSIGNED32 AdsGetMemoLength(ADSHANDLE hTable, UNSIGNED8* pucField,
                            UNSIGNED32* pulLen) {
    Table* t = get_table(hTable);
    if (!t || pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    *pulLen = static_cast<UNSIGNED32>(v.value().as_string.size());
    return ok();
}

UNSIGNED32 AdsGetMemoDataType(ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED16* pusType) {
    Table* t = get_table(hTable);
    if (!t || pusType == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto r = t->field_memo_type(idx);
    if (!r) return fail(r.error());
    switch (r.value()) {
        case openads::drivers::MemoBlockType::Text:
            *pusType = static_cast<UNSIGNED16>(ADS_STRING);
            break;
        case openads::drivers::MemoBlockType::Picture:
            *pusType = static_cast<UNSIGNED16>(ADS_IMAGE);
            break;
        case openads::drivers::MemoBlockType::Object:
            *pusType = static_cast<UNSIGNED16>(ADS_BINARY);
            break;
    }
    return ok();
}

UNSIGNED32 AdsGetString(ADSHANDLE hTable, UNSIGNED8* pucField,
                        UNSIGNED8* pucBuf, UNSIGNED32* pulLen,
                        UNSIGNED16 /*usOption*/) {
    Table* t = get_table(hTable);
    if (!t || pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    const std::string& s = v.value().as_string;
    UNSIGNED32 cap = *pulLen;
    UNSIGNED32 n   = cap > 0 ? std::min<UNSIGNED32>(cap - 1,
                                static_cast<UNSIGNED32>(s.size()))
                             : 0;
    if (pucBuf != nullptr && cap > 0) {
        if (n > 0) std::memcpy(pucBuf, s.data(), n);
        pucBuf[n] = '\0';
    }
    *pulLen = static_cast<UNSIGNED32>(s.size());
    return ok();
}

// --- M9.17 Unicode (*W) variants ------------------------------------------
//
// rddads' ADS_LIB_VERSION >= 1000 path routes UNICODE-flagged columns
// through AdsSetStringW / AdsGetStringW / AdsGetFieldW with WCHAR*
// buffers (UTF-16LE on Windows). The engine stores byte sequences
// without a fixed codepage assumption, so the W variants transcode
// at the boundary: UTF-16LE -> UTF-8 on the way in, UTF-8 -> UTF-16LE
// on the way out. Field names are 7-bit ASCII inside the DBF, so the
// pucFieldW name is dropped to ASCII via the same converter.

namespace {

// Resolve a UTF-16 / numeric `pucFieldW` to a 0-based field index.
// Mirrors `resolve_field_index` for the ASCII path: small pointer
// values are interpreted as a 1-based field number (rddads' ADSFIELD
// macro), otherwise the value is a UTF-16LE NUL-terminated field
// name that is transcoded to UTF-8 before the name lookup.
bool resolve_field_index_w(Table* tbl, UNSIGNED16* pucFieldW,
                           std::uint16_t* out) {
    if (tbl == nullptr || out == nullptr) return false;
    auto p = reinterpret_cast<std::uintptr_t>(pucFieldW);
    if (p != 0 && p < 0x10000u) {
        std::uint16_t one_based = static_cast<std::uint16_t>(p);
        if (one_based >= 1 && one_based <= tbl->field_count()) {
            *out = static_cast<std::uint16_t>(one_based - 1);
            return true;
        }
        return false;
    }
    if (pucFieldW == nullptr) return false;
    std::size_t n = 0;
    while (pucFieldW[n] != 0) ++n;
    auto name = openads::abi::utf16le_to_utf8(
        reinterpret_cast<const std::uint16_t*>(pucFieldW), n);
    for (std::uint16_t i = 0; i < tbl->field_count(); ++i) {
        if (tbl->field_descriptor(i).name == name) { *out = i; return true; }
    }
    return false;
}

UNSIGNED32 emit_utf16(UNSIGNED16* pucBufW, UNSIGNED32* pulLenW,
                      const std::string& utf8) {
    if (pulLenW == nullptr) return openads::AE_INTERNAL_ERROR;
    auto units = openads::abi::utf8_to_utf16le(utf8);
    UNSIGNED32 cap = *pulLenW;
    UNSIGNED32 n   = cap > 0
        ? std::min<UNSIGNED32>(cap - 1,
                               static_cast<UNSIGNED32>(units.size()))
        : 0;
    if (pucBufW != nullptr && cap > 0) {
        if (n > 0) {
            std::memcpy(pucBufW, units.data(),
                        n * sizeof(std::uint16_t));
        }
        pucBufW[n] = 0;
    }
    *pulLenW = static_cast<UNSIGNED32>(units.size());
    return openads::AE_SUCCESS;
}

}  // namespace

UNSIGNED32 AdsSetStringW(ADSHANDLE hTable, UNSIGNED16* pucFieldW,
                         UNSIGNED16* pucValueW, UNSIGNED32 ulLen) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::uint16_t idx = 0;
    if (!resolve_field_index_w(t, pucFieldW, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    std::size_t units = ulLen;
    if (units == 0 && pucValueW != nullptr) {
        while (pucValueW[units] != 0) ++units;
    }
    std::string utf8 = openads::abi::utf16le_to_utf8(
        reinterpret_cast<const std::uint16_t*>(pucValueW), units);
    auto r = t->set_field(idx, utf8);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsGetStringW(ADSHANDLE hTable, UNSIGNED16* pucFieldW,
                         UNSIGNED16* pucBufW, UNSIGNED32* pulLenW,
                         UNSIGNED16 /*usOption*/) {
    Table* t = get_table(hTable);
    if (!t || pulLenW == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index_w(t, pucFieldW, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    return emit_utf16(pucBufW, pulLenW, v.value().as_string);
}

UNSIGNED32 AdsGetFieldW(ADSHANDLE hTable, UNSIGNED16* pucFieldW,
                        UNSIGNED16* pucBufW, UNSIGNED32* pulLenW,
                        UNSIGNED16 /*usOption*/) {
    return AdsGetStringW(hTable, pucFieldW, pucBufW, pulLenW, 0);
}

UNSIGNED32 AdsSetJulian(ADSHANDLE hTable, UNSIGNED8* pucField,
                        SIGNED32 lDate) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    char buf[9];
    if (lDate <= 0) {
        std::memset(buf, ' ', 8); buf[8] = '\0';
    } else {
        int y = 0, m = 0, d = 0;
        julian_to_ymd(lDate, y, m, d);
        std::snprintf(buf, sizeof(buf), "%04d%02d%02d", y, m, d);
    }
    std::string val(buf, 8);
    auto r = t->set_field(idx, val);
    if (!r) return fail(r.error());
    return ok();
}

// --- M9.18 lock retry policy ----------------------------------------------
//
// Real ACE exposes a per-connection (cycle_ms, retry_count) tuple that
// callers tune via AdsSetLockCycle / AdsSetLockRetryCount; AdsLockTable
// and AdsLockRecord then re-attempt a contended lock up to that limit
// before reporting AE_LOCK_FAILED. OpenADS keeps a process-global
// policy (the hConnect arg is accepted for ABI compat but the value is
// shared across connections in this build); the retry loop sleeps
// `cycle_ms` between attempts and gives up after `retry_count` cycles.

namespace {

struct LockPolicy {
    UNSIGNED32 cycle_ms    = 100;   // ACE default
    UNSIGNED16 retry_count = 10;
};

LockPolicy& lock_policy() {
    static LockPolicy p;
    return p;
}

UNSIGNED32 lock_with_retry(std::function<openads::util::Result<void>()> fn) {
    LockPolicy p = lock_policy();
    for (UNSIGNED16 i = 0; ; ++i) {
        auto r = fn();
        if (r) return openads::AE_SUCCESS;
        if (i >= p.retry_count) return fail(r.error());
        if (p.cycle_ms > 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(p.cycle_ms));
        }
    }
}

}  // namespace

UNSIGNED32 AdsSetLockCycle(ADSHANDLE /*hConnect*/, UNSIGNED32 ulCycle) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    lock_policy().cycle_ms = ulCycle;
    return ok();
}

UNSIGNED32 AdsGetLockCycle(ADSHANDLE /*hConnect*/, UNSIGNED32* pulCycle) {
    if (pulCycle == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    *pulCycle = lock_policy().cycle_ms;
    return ok();
}

UNSIGNED32 AdsSetLockRetryCount(ADSHANDLE /*hConnect*/, UNSIGNED16 usRetryCount) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    lock_policy().retry_count = usRetryCount;
    return ok();
}

UNSIGNED32 AdsGetLockRetryCount(ADSHANDLE /*hConnect*/, UNSIGNED16* pusRetryCount) {
    if (pusRetryCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    *pusRetryCount = lock_policy().retry_count;
    return ok();
}

UNSIGNED32 AdsLockRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    return lock_with_retry([t, ulRecord]() {
        return t->try_lock_record_excl(ulRecord);
    });
}

UNSIGNED32 AdsUnlockRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->unlock_record(ulRecord);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsLockTable(ADSHANDLE hTable) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    return lock_with_retry([t]() { return t->try_lock_table_excl(); });
}

UNSIGNED32 AdsUnlockTable(ADSHANDLE hTable) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->unlock_table();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsFlushFileBuffers(ADSHANDLE hTable) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->flush();
    if (!r) return fail(r.error());
    return ok();
}

// --- M3 index / scope / seek -----------------------------------------------

extern "C++" {

namespace {

// Index handles: a single "logical" handle wraps the table-bound order
// since openads::engine::Table owns the active IIndex via Order. We keep a
// per-process map so the L1 thunks can resolve the table from the index
// handle.
// Binding for one open tag. Multi-tag CDX files create one binding
// per tag. At most ONE binding per table is "live" — its `idx` has
// been moved into Table::order_; the rest park their IIndex here so
// OrdSetFocus / AdsGetIndexHandle can swap them in on demand.
struct IndexBinding {
    Table*                                  table = nullptr;
    std::string                             tag_name;
    std::unique_ptr<openads::drivers::IIndex> parked;  // nullptr when this is the active binding
    std::string                             path;     // resolved index file path (M9.14)
};

std::unordered_map<ADSHANDLE, IndexBinding>& index_bindings() {
    static std::unordered_map<ADSHANDLE, IndexBinding> m;
    return m;
}

// Records, per table, which binding handle currently owns the active
// IIndex (i.e. the one moved into Table::order_).
std::unordered_map<Table*, ADSHANDLE>& active_binding_for() {
    static std::unordered_map<Table*, ADSHANDLE> m;
    return m;
}

// Drop every binding tied to `t`. Called from AdsCloseTable / AdsCloseAllTables
// / AdsDisconnect — without this, a Connection teardown leaves the bindings
// behind, so a later test (or app reconnect) that allocates a Table at the
// same heap slot inherits the stale entries and table_has_active misfires.
void purge_bindings_for_table(Table* t) {
    auto& m   = index_bindings();
    auto& act = active_binding_for();
    for (auto it = m.begin(); it != m.end(); ) {
        if (it->second.table == t) it = m.erase(it);
        else                       ++it;
    }
    act.erase(t);
}

Table* lookup_table_by_index(ADSHANDLE h) {
    auto& m = index_bindings();
    auto it = m.find(h);
    if (it == m.end()) return nullptr;
    return it->second.table;
}

openads::drivers::IIndex* iindex_for_handle(ADSHANDLE h) {
    auto& m = index_bindings();
    auto it = m.find(h);
    if (it == m.end()) return nullptr;
    if (it->second.parked) return it->second.parked.get();
    Table* t = it->second.table;
    if (t && t->order()) return t->order()->index();
    return nullptr;
}

// Make `h` the active order for its table. If another binding is
// currently active, park its IIndex back into that binding before
// stealing the requested one. No-op when `h` is already active.
openads::util::Result<void> activate_binding(ADSHANDLE h) {
    auto& m = index_bindings();
    auto it = m.find(h);
    if (it == m.end()) {
        return openads::util::Error{
            openads::AE_INTERNAL_ERROR, 0, "unknown index", ""};
    }
    Table* t = it->second.table;
    auto& act = active_binding_for();
    auto act_it = act.find(t);
    if (act_it != act.end() && act_it->second == h) return {};   // already live

    // Park the currently-active binding's index back into its slot
    // and register it as an extra view (so multi-index sync still
    // touches it after the swap). When act_it points to a handle
    // that's no longer in the binding map (stale entry left by a
    // previous AdsCloseAllIndexes / test cleanup that didn't tidy
    // act_), drop the act entry but leave Table::order_ alone — the
    // current code may have set it via the legacy AdsCreateIndex path
    // that doesn't populate `act_`.
    if (act_it != act.end()) {
        auto prev = m.find(act_it->second);
        if (prev != m.end()) {
            auto taken = t->take_order();
            openads::drivers::IIndex* raw = taken.get();
            prev->second.parked = std::move(taken);
            if (raw) t->register_extra_index_view(raw);
        }
        // else: stale act entry; leave Table::order_ untouched.
    }

    // Move the parked IIndex from this binding into the table; drop
    // its extra-view entry since the active order's IIndex is already
    // walked by the sync loop.
    if (it->second.parked) {
        openads::drivers::IIndex* raw = it->second.parked.get();
        t->unregister_extra_index_view(raw);
        t->set_order(std::move(it->second.parked));
    }
    act[t] = h;
    return {};
}

ADSHANDLE next_index_handle() {
    static std::uint64_t n = 0x40000000ULL;  // disjoint from table handles
    return ++n;
}

Table* table_for_index(ADSHANDLE hIndex) {
    auto it = index_bindings().find(hIndex);
    if (it == index_bindings().end()) return nullptr;
    // Activate this binding so the Table's order_ reflects the
    // requested index — AdsSeek / AdsGotoTop / etc. always operate
    // through the Table's active order, and rddads passes the index
    // handle (pArea->hOrdCurrent) as the operand.
    (void)activate_binding(hIndex);
    return it->second.table;
}

bool path_ends_with_ci(const std::string& s, const char* suffix) {
    auto n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        char a = static_cast<char>(std::tolower(
            static_cast<unsigned char>(s[s.size() - n + i])));
        char b = static_cast<char>(std::tolower(
            static_cast<unsigned char>(suffix[i])));
        if (a != b) return false;
    }
    return true;
}

std::unique_ptr<openads::drivers::IIndex>
make_index_for(const std::string& path) {
    if (path_ends_with_ci(path, ".cdx")) {
        return std::make_unique<openads::drivers::cdx::CdxIndex>();
    }
    return std::make_unique<openads::drivers::ntx::NtxIndex>();
}

} // namespace

} // extern "C++"

// Compare two filesystem paths for the "is this the same on-disk
// file?" question. Falls back to a case-insensitive lexical compare
// when canonical resolution fails (e.g. file doesn't exist yet).
namespace {
bool same_index_path(const std::string& a, const std::string& b) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto ca = fs::weakly_canonical(fs::path(a), ec);
    auto cb = fs::weakly_canonical(fs::path(b), ec);
    auto sa = ec ? a : ca.string();
    auto sb = ec ? b : cb.string();
    if (sa.size() != sb.size()) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            char ca2 = static_cast<char>(std::tolower(
                static_cast<unsigned char>(a[i])));
            char cb2 = static_cast<char>(std::tolower(
                static_cast<unsigned char>(b[i])));
            if (ca2 != cb2) return false;
        }
        return true;
    }
    for (std::size_t i = 0; i < sa.size(); ++i) {
        char ca2 = static_cast<char>(std::tolower(
            static_cast<unsigned char>(sa[i])));
        char cb2 = static_cast<char>(std::tolower(
            static_cast<unsigned char>(sb[i])));
        if (ca2 != cb2) return false;
    }
    return true;
}
}  // namespace

// Real-ACE 4-arg signature: opens an index FILE, registers one handle
// per tag, and writes the handles into ahIndex[] / *pu16ArrayLen.
//
// M9.14 made this additive: a second AdsOpenIndex against a different
// file path no longer wipes the prior bindings. Instead, the new
// indices land as parked extra views (their writes still sync) and
// the first one only steals Table::order_ when no active order is
// currently bound. Repeated calls with the SAME path drop the prior
// bindings for that path (refresh semantics) so reopening the same
// .ntx / .cdx leaves at most one binding per tag.
UNSIGNED32 AdsOpenIndex(ADSHANDLE hTable, UNSIGNED8* pucName,
                        ADSHANDLE* ahIndex, UNSIGNED16* pu16ArrayLen) {
    Table* t = get_table(hTable);
    if (!t || ahIndex == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown table or null out");
    }
    auto bag_name = openads::abi::to_internal(pucName, 0);
    namespace fs = std::filesystem;
    fs::path p(bag_name);
    if (!p.is_absolute()) {
        fs::path table_dir = fs::path(t->path()).parent_path();
        p = table_dir / p;
    }
    if (!p.has_extension()) {
        p.replace_extension(".cdx");
    }
    auto path = p.string();

    auto& m   = index_bindings();
    auto& act = active_binding_for();

    // Refresh: drop any prior bindings for this Table that came from
    // the same file path. If the active binding was among them, also
    // surrender Table::order_; the caller's reopen will repopulate it.
    bool active_dropped = false;
    auto act_it = act.find(t);
    ADSHANDLE active_h = act_it != act.end() ? act_it->second : 0;
    for (auto it = m.begin(); it != m.end(); ) {
        if (it->second.table == t && same_index_path(it->second.path, path)) {
            if (it->first == active_h) {
                active_dropped = true;
            } else if (it->second.parked) {
                t->unregister_extra_index_view(it->second.parked.get());
            }
            it = m.erase(it);
        } else {
            ++it;
        }
    }
    if (active_dropped) {
        act.erase(t);
        t->clear_order();
    }
    bool table_has_active = act.find(t) != act.end();

    // Enumerate tags. CDX exposes list_tags; NTX has only its single
    // tag, which open() reports via name().
    std::vector<std::string> tags;
    if (path_ends_with_ci(path, ".cdx")) {
        auto r = openads::drivers::cdx::CdxIndex::list_tags(path);
        if (!r) return fail(r.error());
        tags = std::move(r).value();
    }
    if (tags.empty()) {
        // NTX or empty CDX: open once via the legacy path. M9.14 lets
        // multiple NTX files coexist on the same Table — when the
        // table already has an active order, the new NTX parks as an
        // extra view instead of replacing it.
        auto idx = make_index_for(path);
        if (auto r = idx->open(path, openads::drivers::IndexOpenMode::Shared); !r) {
            return fail(r.error());
        }
        std::string tag_name = idx->name();
        ADSHANDLE h = next_index_handle();
        if (!table_has_active) {
            t->set_order(std::move(idx));
            m[h] = IndexBinding{t, tag_name, nullptr, path};
            act[t] = h;
        } else {
            openads::drivers::IIndex* raw = idx.get();
            m[h] = IndexBinding{t, tag_name, std::move(idx), path};
            t->register_extra_index_view(raw);
        }
        ahIndex[0] = h;
        if (pu16ArrayLen != nullptr) *pu16ArrayLen = 1;
        return ok();
    }

    // CDX with one or more tags: open each by name. The first tag's
    // IIndex moves into Table::order_ (becomes default order) only
    // when the table doesn't already have an active order; the rest
    // (and the first tag in the additive case) park as extra views.
    UNSIGNED16 cap = (pu16ArrayLen != nullptr && *pu16ArrayLen > 0)
                   ? *pu16ArrayLen : 1;
    UNSIGNED16 count = 0;
    for (const auto& name : tags) {
        if (count >= cap) break;
        auto sub = std::make_unique<openads::drivers::cdx::CdxIndex>();
        if (auto r = sub->open_named(path,
                          openads::drivers::IndexOpenMode::Shared,
                          name); !r) {
            return fail(r.error());
        }
        ADSHANDLE h = next_index_handle();
        if (!table_has_active) {
            t->set_order(std::move(sub));
            m[h] = IndexBinding{t, name, nullptr, path};
            act[t] = h;
            table_has_active = true;
        } else {
            openads::drivers::IIndex* raw = sub.get();
            m[h] = IndexBinding{t, name, std::move(sub), path};
            t->register_extra_index_view(raw);
        }
        ahIndex[count++] = h;
    }
    if (pu16ArrayLen != nullptr) *pu16ArrayLen = count;
    return ok();
}

UNSIGNED32 AdsCloseIndex(ADSHANDLE hIndex) {
    auto& m = index_bindings();
    auto& act = active_binding_for();
    auto it = m.find(hIndex);
    if (it == m.end()) return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    Table* t = it->second.table;
    if (t != nullptr) {
        auto act_it = act.find(t);
        if (act_it != act.end() && act_it->second == hIndex) {
            t->clear_order();
            act.erase(act_it);
        } else if (it->second.parked) {
            t->unregister_extra_index_view(it->second.parked.get());
        }
    }
    m.erase(it);
    return ok();
}

UNSIGNED32 AdsCloseAllIndexes(ADSHANDLE hTable) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto& m = index_bindings();
    auto& act = active_binding_for();
    for (auto it = m.begin(); it != m.end(); ) {
        if (it->second.table == t) {
            if (it->second.parked) {
                t->unregister_extra_index_view(it->second.parked.get());
            }
            it = m.erase(it);
        } else {
            ++it;
        }
    }
    act.erase(t);
    t->clear_order();
    t->clear_extra_index_views();
    return ok();
}

UNSIGNED32 AdsCreateIndex61(ADSHANDLE   hTable,
                            UNSIGNED8*  pucFileName,
                            UNSIGNED8*  pucIndexName,
                            UNSIGNED8*  pucExpr,
                            UNSIGNED8*  /*pucCondition*/,
                            UNSIGNED8*  /*pucKeyFilter*/,
                            UNSIGNED32  ulOptions,
                            UNSIGNED16  /*usPageSize*/,
                            ADSHANDLE*  phIndex) {
    Table* t = get_table(hTable);
    if (!t || phIndex == nullptr || pucFileName == nullptr ||
        pucIndexName == nullptr || pucExpr == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "null arg");
    }
    auto bag  = openads::abi::to_internal(pucFileName, 0);
    auto tag  = openads::abi::to_internal(pucIndexName, 0);
    auto expr = openads::abi::to_internal(pucExpr, 0);

    namespace fs = std::filesystem;
    fs::path p(bag);
    if (!p.is_absolute()) {
        fs::path tdir = fs::path(t->path()).parent_path();
        p = tdir / p;
    }
    if (!p.has_extension()) p.replace_extension(".cdx");
    bool is_cdx = path_ends_with_ci(p.string(), ".cdx");

    bool unique  = (ulOptions & 0x01u) != 0;
    bool descend = (ulOptions & 0x02u) != 0;

    // Determine key length by evaluating the expression against the
    // first live record. Empty tables get a 32-char default.
    std::uint16_t klen = 32;
    if (t->record_count() > 0) {
        if (auto g = t->goto_record(1); g) {
            auto k = openads::engine::evaluate_index_expr(*t, expr, 254);
            if (k) {
                std::string s = std::move(k).value();
                while (!s.empty() && s.back() == ' ') s.pop_back();
                if (!s.empty()) {
                    klen = static_cast<std::uint16_t>(
                        std::min<std::size_t>(s.size(), 254));
                }
            }
        }
    }

    std::unique_ptr<openads::drivers::IIndex> idx_owner;
    openads::drivers::IIndex* idx_view = nullptr;
    bool exists = false;
    {
        std::error_code ec;
        exists = is_cdx && fs::exists(p, ec);
    }
    if (is_cdx && exists) {
        auto added = openads::drivers::cdx::CdxIndex::add_tag(
            p.string(), tag, expr, klen, unique, descend);
        if (!added) return fail(added.error());
        idx_owner = std::make_unique<openads::drivers::cdx::CdxIndex>(
            std::move(added).value());
    } else if (is_cdx) {
        auto created = openads::drivers::cdx::CdxIndex::create(
            p.string(), tag, expr, klen, unique, descend);
        if (!created) return fail(created.error());
        idx_owner = std::make_unique<openads::drivers::cdx::CdxIndex>(
            std::move(created).value());
    } else {
        auto created = openads::drivers::ntx::NtxIndex::create(
            p.string(), tag, expr, klen, unique, descend);
        if (!created) return fail(created.error());
        idx_owner = std::make_unique<openads::drivers::ntx::NtxIndex>(
            std::move(created).value());
    }
    idx_view = idx_owner.get();

    auto rec_count = t->record_count();
    for (std::uint32_t r = 1; r <= rec_count; ++r) {
        if (auto g = t->goto_record(r); !g) return fail(g.error());
        if (t->is_deleted()) continue;
        auto k = openads::engine::evaluate_index_expr(*t, expr, klen);
        if (!k) return fail(k.error());
        if (auto ins = idx_owner->insert(r, k.value()); !ins) {
            return fail(ins.error());
        }
    }
    if (auto fl = idx_owner->flush(); !fl) return fail(fl.error());

    auto& m   = index_bindings();
    auto& act = active_binding_for();
    bool table_has_active = false;
    for (auto& kv : m) {
        if (kv.second.table == t) { table_has_active = true; break; }
    }
    ADSHANDLE h = next_index_handle();
    if (!table_has_active) {
        t->set_order(std::move(idx_owner));
        m[h] = IndexBinding{t, tag, nullptr, p.string()};
        act[t] = h;
    } else {
        t->register_extra_index_view(idx_view);
        m[h] = IndexBinding{t, tag, std::move(idx_owner), p.string()};
    }
    *phIndex = h;
    return ok();
}

UNSIGNED32 AdsCreateIndex(ADSHANDLE hTable, UNSIGNED8* pucFile,
                          UNSIGNED8* pucTag, UNSIGNED8* pucExpr,
                          UNSIGNED8* /*pucCondition*/, UNSIGNED32 /*ulOptions*/,
                          UNSIGNED16 /*usKeyType*/, ADSHANDLE* phIndex) {
    Table* t = get_table(hTable);
    if (!t || phIndex == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown table or null out");
    }
    auto file = openads::abi::to_internal(pucFile, 0);
    auto tag  = openads::abi::to_internal(pucTag,  0);
    auto expr = openads::abi::to_internal(pucExpr, 0);

    // Resolve the field referenced by the expression to determine key length.
    std::int32_t fidx = t->field_index(expr);
    if (fidx < 0) {
        return fail(openads::AE_COLUMN_NOT_FOUND,
                    "AdsCreateIndex: expression must be a bare field name");
    }
    std::uint16_t klen = t->field_descriptor(static_cast<std::uint16_t>(fidx)).length;

    std::unique_ptr<openads::drivers::IIndex> idx;
    if (path_ends_with_ci(file, ".cdx")) {
        auto created = openads::drivers::cdx::CdxIndex::create(
            file, tag, expr, klen, false, false);
        if (!created) return fail(created.error());
        idx = std::make_unique<openads::drivers::cdx::CdxIndex>(
            std::move(created).value());
    } else {
        auto created = openads::drivers::ntx::NtxIndex::create(
            file, tag, expr, klen, false, false);
        if (!created) return fail(created.error());
        idx = std::make_unique<openads::drivers::ntx::NtxIndex>(
            std::move(created).value());
    }

    // Populate from existing live records in primary order. Deleted
    // records are skipped so AdsSeek over the new index never returns
    // phantom recnos.
    auto rec_count = t->record_count();
    for (std::uint32_t r = 1; r <= rec_count; ++r) {
        if (auto rr = t->goto_record(r); !rr) return fail(rr.error());
        if (t->is_deleted()) continue;
        auto v = t->read_field(static_cast<std::uint16_t>(fidx));
        if (!v) return fail(v.error());
        std::string padded = v.value().as_string;
        if (padded.size() < klen) padded.append(klen - padded.size(), ' ');
        if (padded.size() > klen) padded.resize(klen);
        if (auto ins = idx->insert(r, padded); !ins) return fail(ins.error());
    }
    if (auto fl = idx->flush(); !fl) return fail(fl.error());

    auto& m   = index_bindings();
    auto& act = active_binding_for();
    bool table_has_active = act.find(t) != act.end();
    ADSHANDLE h = next_index_handle();
    if (!table_has_active) {
        t->set_order(std::move(idx));
        m[h] = IndexBinding{t, tag, nullptr, file};
        act[t] = h;
    } else {
        openads::drivers::IIndex* raw = idx.get();
        m[h] = IndexBinding{t, tag, std::move(idx), file};
        t->register_extra_index_view(raw);
    }
    *phIndex = h;
    return ok();
}

UNSIGNED32 AdsDeleteIndex(ADSHANDLE hIndex) {
    return AdsCloseIndex(hIndex);
}

// --- M9.20 custom-key indexes ---------------------------------------------
//
// rddads' DBOI_KEYADD / DBOI_KEYDELETE branches call AdsAddCustomKey
// / AdsDeleteCustomKey with just an index handle and expect the call
// to operate on the **current record**. Real ACE evaluates the
// index's expression against the positioned row and inserts (or
// erases) the resulting (key, recno) entry — the "custom" wording
// comes from the surrounding `ADS_CUSTOM` flag on the index, which
// disables the engine's auto-sync so apps drive the index manually
// through these two entry points.
//
// OpenADS today doesn't separately track the `ADS_CUSTOM` flag, so
// these calls always evaluate + insert/erase. Apps that opt into
// custom mode get correct behaviour because they're the ones
// explicitly invoking these functions; expression-driven apps stay
// out of the call site.

namespace {

openads::drivers::IIndex* iindex_for_binding(IndexBinding& b) {
    if (b.parked) return b.parked.get();
    if (auto* o = b.table ? b.table->order() : nullptr; o) {
        return const_cast<openads::engine::Order*>(o)->index();
    }
    return nullptr;
}

}  // namespace

UNSIGNED32 AdsAddCustomKey(ADSHANDLE hIndex) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    auto& m = index_bindings();
    auto it = m.find(hIndex);
    if (it == m.end()) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown index handle");
    }
    Table* t = it->second.table;
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto* idx = iindex_for_binding(it->second);
    if (!idx) return fail(openads::AE_INTERNAL_ERROR, "no IIndex for binding");

    std::uint16_t klen = idx->key_length();
    if (klen == 0) klen = 32;
    auto k = openads::engine::evaluate_index_expr(*t, idx->expression(), klen);
    if (!k) return fail(k.error());
    auto r = idx->insert(t->recno(), k.value());
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDeleteCustomKey(ADSHANDLE hIndex) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    auto& m = index_bindings();
    auto it = m.find(hIndex);
    if (it == m.end()) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown index handle");
    }
    Table* t = it->second.table;
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto* idx = iindex_for_binding(it->second);
    if (!idx) return fail(openads::AE_INTERNAL_ERROR, "no IIndex for binding");

    std::uint16_t klen = idx->key_length();
    if (klen == 0) klen = 32;
    auto k = openads::engine::evaluate_index_expr(*t, idx->expression(), klen);
    if (!k) return fail(k.error());
    auto r = idx->erase(t->recno(), k.value());
    if (!r) return fail(r.error());
    return ok();
}

// --- M9.19 Full-text search ------------------------------------------------
//
// Creates an OpenADS-native `.fts` inverted-index file alongside the
// table. The format is plain UTF-8 text — clean-room, NOT derived
// from any proprietary ADS FTS layout. Search support (token lookup
// at query time) is a follow-up milestone; today the create path
// gives apps a stable artefact to commit and visit.
//
// Most of the optional configuration knobs are honoured: min/max
// word length, custom delimiter / noise-word arrays. The page-size,
// drop-char, conditional-char, and reserved arguments are accepted
// (so the ABI shape matches rddads' ADSCREATEFTSINDEX call) and
// don't affect today's text emitter.

}  // extern "C"

namespace {

openads::engine::FtsOptions
build_fts_options(UNSIGNED32  ulMinWordLen, UNSIGNED32  ulMaxWordLen,
                  UNSIGNED16  usUseDefaultDelim,
                  const UNSIGNED8* pucDelimiters,
                  UNSIGNED16  usUseDefaultNoise,
                  const UNSIGNED8* pucNoiseWords) {
    openads::engine::FtsOptions opts;
    if (ulMinWordLen > 0) opts.min_word_len = ulMinWordLen;
    if (ulMaxWordLen > 0) opts.max_word_len = ulMaxWordLen;

    if (!usUseDefaultDelim && pucDelimiters != nullptr) {
        opts.extra_delims = openads::abi::to_internal(
            const_cast<UNSIGNED8*>(pucDelimiters), 0);
    }

    if (usUseDefaultNoise) {
        // Standard English stop-word seed; apps can override.
        for (auto* w : {"a", "an", "the", "and", "or", "but", "if",
                        "of", "in", "on", "at", "to", "for", "is",
                        "are", "was", "were", "be", "by", "with",
                        "as", "from", "this", "that", "these",
                        "those", "it", "its", "not"}) {
            opts.noise_words.insert(w);
        }
    } else if (pucNoiseWords != nullptr) {
        auto raw = openads::abi::to_internal(
            const_cast<UNSIGNED8*>(pucNoiseWords), 0);
        std::string cur;
        for (char c : raw) {
            if (c == ' ' || c == '\t' || c == ',' || c == ';' || c == '\n') {
                if (!cur.empty()) { opts.noise_words.insert(cur); cur.clear(); }
            } else {
                cur.push_back(static_cast<char>(std::tolower(
                    static_cast<unsigned char>(c))));
            }
        }
        if (!cur.empty()) opts.noise_words.insert(cur);
    }
    return opts;
}

}  // namespace

extern "C" {

// --- M9.23 fill in remaining MISS exports ---------------------------------

UNSIGNED32 AdsGetLongLong(ADSHANDLE hTable, UNSIGNED8* pucField,
                          std::int64_t* pllValue) {
    Table* t = get_table(hTable);
    if (!t || pllValue == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    auto& s = v.value().as_string;
    // Strip leading whitespace; strtoll handles signed prefix and digits.
    std::size_t i = 0;
    while (i < s.size() &&
           std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    *pllValue = static_cast<std::int64_t>(
        std::strtoll(s.c_str() + i, nullptr, 10));
    return ok();
}

UNSIGNED32 AdsSetFieldRaw(ADSHANDLE hTable, UNSIGNED8* pucField,
                          UNSIGNED8* pucBuf, UNSIGNED32 ulLen) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    std::string raw;
    if (pucBuf != nullptr && ulLen > 0) {
        raw.assign(reinterpret_cast<const char*>(pucBuf), ulLen);
    }
    auto r = t->set_field(idx, raw);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsVerifySQL(ADSHANDLE /*hStatement*/, UNSIGNED8* pucSQL) {
    if (pucSQL == nullptr) return fail(openads::AE_PARSE_ERROR, "null SQL");
    auto sql = openads::abi::to_internal(pucSQL, 0);
    auto r = openads::sql::parse_select(sql);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsFailedTransactionRecovery(UNSIGNED8* pucServer) {
    if (pucServer == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "null server path");
    }
    auto path = openads::abi::to_internal(pucServer, 0);
    // Recovery happens automatically on Connection::open — the open
    // path scans openads.txlog, replays orphan transactions' before-
    // images, and truncates the log. Open + close gives the caller a
    // single explicit recovery pass.
    auto opened = Connection::open(path);
    if (!opened) return fail(opened.error());
    return ok();
}

UNSIGNED32 AdsGetAllLocks(ADSHANDLE hTable, UNSIGNED32* paRecnos,
                          UNSIGNED16* pusCount) {
    Table* t = get_table(hTable);
    if (!t || pusCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto held = t->held_record_locks();
    UNSIGNED16 cap = *pusCount;
    UNSIGNED16 n   = static_cast<UNSIGNED16>(
        std::min<std::size_t>(held.size(), cap));
    if (paRecnos != nullptr && n > 0) {
        for (UNSIGNED16 i = 0; i < n; ++i) {
            paRecnos[i] = static_cast<UNSIGNED32>(held[i]);
        }
    }
    *pusCount = static_cast<UNSIGNED16>(held.size());
    return ok();
}

UNSIGNED32 AdsSkipUnique(ADSHANDLE hIndex, SIGNED32 lDirection) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    auto* idx = iindex_for_handle(hIndex);
    Table* t  = lookup_table_by_index(hIndex);
    if (!idx || !t) return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    (void)activate_binding(hIndex);

    std::string start_key = idx->current_key();
    auto step = [&]() {
        return lDirection < 0 ? idx->prev() : idx->next();
    };

    for (;;) {
        auto r = step();
        if (!r) return fail(r.error());
        if (!r.value().positioned) {
            return fail(openads::AE_INTERNAL_ERROR, "no more unique keys");
        }
        if (idx->current_key() != start_key) {
            // Position the table on the new recno.
            (void)t->goto_record(r.value().recno);
            return ok();
        }
    }
}

// AdsFTSSearch is an OpenADS extension. The original ACE SDK doesn't
// export an entry point with this exact shape that rddads' Harbour
// surface ever reached (rddads is silent on FTS query semantics), so
// OpenADS publishes a small clean-room API: load the .fts file at
// `pucFile`, tokenise the query with the standard rules, intersect
// the per-token recno lists, and write up to `*pulCount` recnos into
// `paRecnos`. `*pulCount` is treated as in/out — the caller passes
// the array capacity and reads back the total number of matches
// (which may be larger than the buffer).
UNSIGNED32 AdsFTSSearch(ADSHANDLE   /*hConnect*/,
                        UNSIGNED8*  pucFile,
                        UNSIGNED8*  pucQuery,
                        UNSIGNED32* paRecnos,
                        UNSIGNED32* pulCount) {
    if (pucFile == nullptr || pucQuery == nullptr || pulCount == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "AdsFTSSearch: null arg");
    }
    auto path  = openads::abi::to_internal(pucFile,  0);
    auto query = openads::abi::to_internal(pucQuery, 0);

    auto loaded = openads::engine::Fts::load(path);
    if (!loaded) return fail(loaded.error());

    openads::engine::FtsOptions opts;
    auto hits = openads::engine::Fts::search(loaded.value(), query, opts);

    UNSIGNED32 cap = *pulCount;
    UNSIGNED32 n   = static_cast<UNSIGNED32>(
        std::min<std::size_t>(hits.size(), cap));
    if (paRecnos != nullptr && n > 0) {
        std::memcpy(paRecnos, hits.data(), n * sizeof(UNSIGNED32));
    }
    *pulCount = static_cast<UNSIGNED32>(hits.size());
    return ok();
}

// --- M10.1 Data-Dictionary CRUD --------------------------------------------
//
// Real persistence in OpenADS' clean-room DD text format. When the
// caller's connection has no DD attached (i.e. the connection was
// opened against a plain data directory, not a `.add` file), the
// CRUD calls report AE_SUCCESS and no-op — matching the "everything
// quiescent" contract used for AdsMg* in M9.24. Apps that opened
// the DD via `Connection::open(<.add>)` (M6) get round-trip
// persistence.

namespace {

openads::engine::DataDict* dd_from_handle(ADSHANDLE hConn) {
    auto& s = state();
    Connection* c = s.registry.lookup<Connection>(hConn, HandleKind::Connection);
    if (c == nullptr || !c->has_dd()) return nullptr;
    return c->dd();
}

}  // namespace

UNSIGNED32 AdsDDAddIndexFile(ADSHANDLE hConn,
                             UNSIGNED8* pucTable, UNSIGNED8* pucIndex,
                             UNSIGNED8* pucComment) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto tbl  = openads::abi::to_internal(pucTable, 0);
    auto idx  = openads::abi::to_internal(pucIndex, 0);
    auto cmt  = pucComment ? openads::abi::to_internal(pucComment, 0)
                           : std::string();
    auto r = dd->add_index_file(tbl, idx, cmt);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDRemoveIndexFile(ADSHANDLE hConn,
                                UNSIGNED8* pucTable, UNSIGNED8* pucIndex,
                                UNSIGNED16 /*opt*/) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto tbl = openads::abi::to_internal(pucTable, 0);
    auto idx = openads::abi::to_internal(pucIndex, 0);
    auto r = dd->remove_index_file(tbl, idx);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDCreateUser(ADSHANDLE hConn, UNSIGNED8* /*pucGroup*/,
                           UNSIGNED8* pucUser, UNSIGNED8* /*pucPwd*/,
                           UNSIGNED8* /*pucDesc*/) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto user = openads::abi::to_internal(pucUser, 0);
    auto r = dd->create_user(user);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDDeleteUser(ADSHANDLE hConn, UNSIGNED8* pucUser) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto user = openads::abi::to_internal(pucUser, 0);
    auto r = dd->delete_user(user);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDAddUserToGroup(ADSHANDLE hConn,
                               UNSIGNED8* pucGroup, UNSIGNED8* pucUser) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto group = openads::abi::to_internal(pucGroup, 0);
    auto user  = openads::abi::to_internal(pucUser, 0);
    auto r = dd->add_user_to_group(user, group);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDRemoveUserFromGroup(ADSHANDLE hConn,
                                    UNSIGNED8* pucGroup, UNSIGNED8* pucUser) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto group = openads::abi::to_internal(pucGroup, 0);
    auto user  = openads::abi::to_internal(pucUser, 0);
    auto r = dd->remove_user_from_group(user, group);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDCreateLink(ADSHANDLE hConn, UNSIGNED8* pucAlias,
                           UNSIGNED8* pucPath, UNSIGNED8* pucUser,
                           UNSIGNED8* pucPwd, UNSIGNED16 /*opt*/) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto alias = openads::abi::to_internal(pucAlias, 0);
    auto path  = openads::abi::to_internal(pucPath, 0);
    auto user  = pucUser ? openads::abi::to_internal(pucUser, 0) : std::string();
    auto pwd   = pucPwd  ? openads::abi::to_internal(pucPwd, 0)  : std::string();
    auto r = dd->create_link(alias, path, user, pwd);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDDropLink(ADSHANDLE hConn, UNSIGNED8* pucAlias,
                         UNSIGNED16 /*opt*/) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto alias = openads::abi::to_internal(pucAlias, 0);
    auto r = dd->drop_link(alias);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDModifyLink(ADSHANDLE hConn, UNSIGNED8* pucAlias,
                           UNSIGNED8* pucPath, UNSIGNED8* pucUser,
                           UNSIGNED8* pucPwd, UNSIGNED16 /*opt*/) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto alias = openads::abi::to_internal(pucAlias, 0);
    auto path  = pucPath ? openads::abi::to_internal(pucPath, 0) : std::string();
    auto user  = pucUser ? openads::abi::to_internal(pucUser, 0) : std::string();
    auto pwd   = pucPwd  ? openads::abi::to_internal(pucPwd, 0)  : std::string();
    auto r = dd->modify_link(alias, path, user, pwd);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDCreateRefIntegrity(ADSHANDLE hConn,
                                   UNSIGNED8* pucName, UNSIGNED8* pucFail,
                                   UNSIGNED8* pucParent, UNSIGNED8* pucChild,
                                   UNSIGNED8* pucTag,
                                   UNSIGNED16 usUpdate, UNSIGNED16 usDelete,
                                   UNSIGNED8* /*pucDesc*/, UNSIGNED16 /*opt*/) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    openads::engine::DataDict::RiEntry e;
    e.name        = openads::abi::to_internal(pucName,   0);
    e.parent      = pucParent ? openads::abi::to_internal(pucParent, 0) : std::string();
    e.child       = pucChild  ? openads::abi::to_internal(pucChild,  0) : std::string();
    e.tag         = pucTag    ? openads::abi::to_internal(pucTag,    0) : std::string();
    e.update_opt  = std::to_string(static_cast<unsigned>(usUpdate));
    e.delete_opt  = std::to_string(static_cast<unsigned>(usDelete));
    e.fail_table  = pucFail   ? openads::abi::to_internal(pucFail,   0) : std::string();
    auto r = dd->create_ri(e);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDRemoveRefIntegrity(ADSHANDLE hConn, UNSIGNED8* pucName) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto name = openads::abi::to_internal(pucName, 0);
    auto r = dd->remove_ri(name);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDSetDatabaseProperty(ADSHANDLE hConn, UNSIGNED16 usProp,
                                    void* pBuf, UNSIGNED16 usLen) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    std::string key = "prop_" + std::to_string(static_cast<unsigned>(usProp));
    std::string val;
    if (pBuf != nullptr && usLen > 0) {
        val.assign(reinterpret_cast<const char*>(pBuf), usLen);
    }
    auto r = dd->set_db_property(key, val);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDGetDatabaseProperty(ADSHANDLE hConn, UNSIGNED16 usProp,
                                    void* pBuf, UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto* dd = dd_from_handle(hConn);
    UNSIGNED16 cap = *pusLen;
    if (pBuf != nullptr && cap > 0) {
        std::memset(pBuf, 0, cap);
    }
    if (dd == nullptr) { *pusLen = 0; return ok(); }
    std::string key = "prop_" + std::to_string(static_cast<unsigned>(usProp));
    auto val = dd->get_db_property(key);
    UNSIGNED16 n = static_cast<UNSIGNED16>(
        std::min<std::size_t>(val.size(), cap));
    if (pBuf != nullptr && n > 0) std::memcpy(pBuf, val.data(), n);
    *pusLen = static_cast<UNSIGNED16>(val.size());
    return ok();
}

UNSIGNED32 AdsDDGetUserProperty(ADSHANDLE hConn, UNSIGNED8* pucUser,
                                UNSIGNED16 usProp, void* pBuf,
                                UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto* dd = dd_from_handle(hConn);
    UNSIGNED16 cap = *pusLen;
    if (pBuf != nullptr && cap > 0) std::memset(pBuf, 0, cap);
    if (dd == nullptr) { *pusLen = 0; return ok(); }
    auto user = openads::abi::to_internal(pucUser, 0);
    std::string key = "prop_" + std::to_string(static_cast<unsigned>(usProp));
    auto val = dd->get_user_property(user, key);
    UNSIGNED16 n = static_cast<UNSIGNED16>(
        std::min<std::size_t>(val.size(), cap));
    if (pBuf != nullptr && n > 0) std::memcpy(pBuf, val.data(), n);
    *pusLen = static_cast<UNSIGNED16>(val.size());
    return ok();
}

UNSIGNED32 AdsCreateFTSIndex(ADSHANDLE   hTable,
                             UNSIGNED8*  pucFileName,
                             UNSIGNED8*  pucTag,
                             UNSIGNED8*  pucField,
                             UNSIGNED32  /*ulPageSize*/,
                             UNSIGNED32  ulMinWordLen,
                             UNSIGNED32  ulMaxWordLen,
                             UNSIGNED16  usUseDefaultDelim,
                             UNSIGNED8*  pucDelimiters,
                             UNSIGNED16  usUseDefaultNoise,
                             UNSIGNED8*  pucNoiseWords,
                             UNSIGNED16  /*usUseDefaultDrop*/,
                             UNSIGNED8*  /*pucDropChars*/,
                             UNSIGNED16  /*usUseDefaultConditionals*/,
                             UNSIGNED8*  /*pucConditionalChars*/,
                             UNSIGNED8*  /*pucReserved1*/,
                             UNSIGNED8*  /*pucReserved2*/,
                             UNSIGNED32  /*ulOptions*/) {
    Table* t = get_table(hTable);
    if (!t || pucTag == nullptr || pucField == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "AdsCreateFTSIndex: null arg");
    }
    auto tag   = openads::abi::to_internal(pucTag, 0);
    auto field = openads::abi::to_internal(pucField, 0);

    namespace fs = std::filesystem;
    fs::path p;
    if (pucFileName != nullptr && pucFileName[0] != '\0') {
        p = openads::abi::to_internal(pucFileName, 0);
    } else {
        // Compound auto-open form: file lives next to the table with
        // the table's stem and a `.fts` extension.
        p = fs::path(t->path()).replace_extension(".fts");
    }
    if (!p.is_absolute()) {
        fs::path tdir = fs::path(t->path()).parent_path();
        p = tdir / p;
    }
    if (!p.has_extension()) p.replace_extension(".fts");

    auto opts = build_fts_options(ulMinWordLen, ulMaxWordLen,
                                  usUseDefaultDelim, pucDelimiters,
                                  usUseDefaultNoise, pucNoiseWords);
    auto r = openads::engine::Fts::create(*t, p.string(), tag, field, opts);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsGetNumIndexes(ADSHANDLE hTable, UNSIGNED16* pusCount) {
    Table* t = get_table(hTable);
    if (!t || pusCount == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    UNSIGNED16 n = 0;
    for (auto& [_, b] : index_bindings()) {
        if (b.table == t) ++n;
    }
    *pusCount = n;
    return ok();
}

UNSIGNED32 AdsGetIndexHandle(ADSHANDLE hTable, UNSIGNED8* pucName,
                             ADSHANDLE* phIndex) {
    Table* t = get_table(hTable);
    if (!t || phIndex == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    auto name = openads::abi::to_internal(pucName, 0);
    // Strip trailing whitespace + nulls (rddads space-pads tag names
    // up to ADS_MAX_TAG_NAME before passing them to us).
    while (!name.empty() && (name.back() == ' ' || name.back() == '\0')) {
        name.pop_back();
    }
    for (auto& [h, b] : index_bindings()) {
        if (b.table == t && b.tag_name == name) { *phIndex = h; return ok(); }
    }
    return fail(openads::AE_INTERNAL_ERROR, "index name not found");
}


UNSIGNED32 AdsGetIndexHandleByOrder(ADSHANDLE hTable, UNSIGNED16 /*usOrder*/,
                                    ADSHANDLE* phIndex) {
    Table* t = get_table(hTable);
    if (!t || phIndex == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    for (auto& [h, b] : index_bindings()) {
        if (b.table == t) { *phIndex = h; return ok(); }
    }
    return fail(openads::AE_INTERNAL_ERROR, "no active index");
}

UNSIGNED32 AdsGetIndexExpr(ADSHANDLE hIndex, UNSIGNED8* pucBuf,
                           UNSIGNED16* pusBufLen) {
    auto& m = index_bindings();
    auto it = m.find(hIndex);
    if (it == m.end()) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    }
    // Pull the expression from whichever IIndex carries it: parked
    // binding has its own; the active one's IIndex sits on the Table.
    std::string expr;
    if (it->second.parked) {
        expr = it->second.parked->expression();
    } else if (it->second.table && it->second.table->order()
            && it->second.table->order()->index()) {
        expr = it->second.table->order()->index()->expression();
    }
    openads::abi::copy_to_caller(pucBuf, pusBufLen, expr);
    return ok();
}

UNSIGNED32 AdsGetIndexName(ADSHANDLE hIndex, UNSIGNED8* pucBuf,
                           UNSIGNED16* pusBufLen) {
    auto& m = index_bindings();
    auto it = m.find(hIndex);
    if (it == m.end()) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    }
    openads::abi::copy_to_caller(pucBuf, pusBufLen, it->second.tag_name);
    return ok();
}

UNSIGNED32 AdsSetIndexDirection(ADSHANDLE hIndex, UNSIGNED16 usDir) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    auto it = index_bindings().find(hIndex);
    if (it == index_bindings().end()) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    }
    Table* t = it->second.table;
    if (t == nullptr) return fail(openads::AE_INTERNAL_ERROR, "no table");
    (void)activate_binding(hIndex);
    auto* o = t->order();
    if (o == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "no active order");
    }
    // ACE convention: usDir == 0 (ADS_ASCENDING) → forward; non-zero
    // (ADS_DESCENDING) → reverse.
    const_cast<openads::engine::Order*>(o)->set_descending_traverse(
        usDir != 0);
    return ok();
}

// ACE / rddads signature: 6 args.
//   AdsSeek(hIndex, pucKey, u16KeyLen, u16KeyType, u16SeekType, &u16Found)
//
// u16KeyType  : ADS_STRINGKEY / ADS_NUMERICKEY / ... — describes
//               pucKey's encoding. We accept whatever the caller sends
//               and pass the bytes through as-is; the engine compares
//               on raw bytes after padding to the index's key length.
// u16SeekType : 0 = exact (hard), 1 = soft. Bit 1 = AfterKey.
// rddads' hb_adsUpdateAreaFlags asks AdsIsFound after every seek to
// decide whether Found() should report .T. — return the flag the
// engine set inside seek_key.
UNSIGNED32 AdsIsFound(ADSHANDLE hTable, UNSIGNED16* pbFound) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    if (pbFound != nullptr) *pbFound = t->last_seek_found() ? 1 : 0;
    return ok();
}

UNSIGNED32 AdsSeek(ADSHANDLE hIndex,
                   UNSIGNED8* pucKey,
                   UNSIGNED16 u16KeyLen,
                   UNSIGNED16 /*u16KeyType*/,
                   UNSIGNED16 u16SeekType,
                   UNSIGNED16* pbFound) {
    Table* t = table_for_index(hIndex);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    std::string key(reinterpret_cast<const char*>(pucKey),
                    static_cast<std::size_t>(u16KeyLen));
    bool soft = (u16SeekType & 0x01) != 0;
    auto r = t->seek_key(key, soft);
    if (!r) return fail(r.error());
    if (pbFound != nullptr) *pbFound = r.value() ? 1 : 0;
    return ok();
}

UNSIGNED32 AdsSeekLast(ADSHANDLE hIndex,
                       UNSIGNED8* pucKey,
                       UNSIGNED16 u16KeyLen,
                       UNSIGNED16 u16KeyType,
                       UNSIGNED16* pbFound) {
    return AdsSeek(hIndex, pucKey, u16KeyLen, u16KeyType,
                   /*soft*/ 0, pbFound);
}

UNSIGNED32 AdsSetScope(ADSHANDLE hIndex, UNSIGNED16 usScope,
                       UNSIGNED8* pucKey) {
    Table* t = table_for_index(hIndex);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    auto key = openads::abi::to_internal(pucKey, 0);
    auto r = t->set_scope(usScope == ADS_TOP, key);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsClearScope(ADSHANDLE hIndex, UNSIGNED16 usScope) {
    Table* t = table_for_index(hIndex);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    auto r = t->clear_scope(usScope == ADS_TOP);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsGetScope(ADSHANDLE hIndex, UNSIGNED16 usScope,
                       UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    Table* t = table_for_index(hIndex);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    auto s = t->get_scope(usScope == ADS_TOP);
    openads::abi::copy_to_caller(pucBuf, pusLen, s.value_or(""));
    return ok();
}

UNSIGNED32 AdsPackTable(ADSHANDLE hTable) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->pack();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsZapTable(ADSHANDLE hTable) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->zap();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsCopyTable(ADSHANDLE   hHandle,
                        UNSIGNED16  /*usFilterOption*/,
                        UNSIGNED8*  pucFile) {
    if (pucFile == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "null target");
    }
    Table* t = get_table(hHandle);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    if (!t->driver()) return fail(openads::AE_INTERNAL_ERROR, "no driver");

    namespace fs = std::filesystem;
    auto raw  = openads::abi::to_internal(pucFile, 0);
    fs::path dst(raw);
    if (!dst.is_absolute()) {
        fs::path src_dir = fs::path(t->path()).parent_path();
        dst = src_dir / dst;
    }
    if (!dst.has_extension()) dst.replace_extension(".dbf");

    // Build a new DBF that mirrors the source schema. Copy live
    // records (deleted rows skipped — filter options beyond
    // ADS_RESPECTFILTERS land later).
    const auto& src_fields = t->driver()->fields();
    if (src_fields.empty()) {
        return fail(openads::AE_INTERNAL_ERROR, "source has no fields");
    }
    std::uint16_t header_len = static_cast<std::uint16_t>(
        32 + 32 * src_fields.size() + 1);
    std::uint16_t rec_len = t->driver()->record_length();

    std::vector<std::uint8_t> file;
    std::vector<std::uint8_t> hdr(32, 0);
    hdr[0]  = 0x03;
    hdr[8]  = static_cast<std::uint8_t>(header_len & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>(rec_len & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rec_len >> 8) & 0xFFu);
    file = hdr;
    for (const auto& f : src_fields) {
        std::vector<std::uint8_t> fd(32, 0);
        std::size_t n = std::min<std::size_t>(f.name.size(), 10);
        std::memcpy(fd.data(), f.name.data(), n);
        fd[11] = static_cast<std::uint8_t>(f.raw_type ? f.raw_type : 'C');
        fd[16] = f.length;
        fd[17] = f.decimals;
        file.insert(file.end(), fd.begin(), fd.end());
    }
    file.push_back(0x0D);

    // Walk source records, append live ones to the buffered file.
    auto src_count = t->driver()->record_count();
    std::uint32_t live = 0;
    for (std::uint32_t r = 1; r <= src_count; ++r) {
        auto rec = t->driver()->read_record_raw(r);
        if (!rec) return fail(rec.error());
        const auto& buf = rec.value();
        if (!buf.empty() && buf[0] == '*') continue;   // deleted
        ++live;
        file.insert(file.end(), buf.begin(), buf.end());
    }
    file.push_back(0x1A);

    // Patch the record count.
    file[4] = static_cast<std::uint8_t>( live        & 0xFFu);
    file[5] = static_cast<std::uint8_t>((live >>  8) & 0xFFu);
    file[6] = static_cast<std::uint8_t>((live >> 16) & 0xFFu);
    file[7] = static_cast<std::uint8_t>((live >> 24) & 0xFFu);

    {
        std::error_code ec;
        fs::remove(dst, ec);
    }
    {
        std::ofstream out(dst, std::ios::binary);
        if (!out) return fail(openads::AE_INTERNAL_ERROR,
                              "AdsCopyTable: open for write failed");
        out.write(reinterpret_cast<const char*>(file.data()),
                  static_cast<std::streamsize>(file.size()));
        if (!out) return fail(openads::AE_INTERNAL_ERROR,
                              "AdsCopyTable: write failed");
    }
    return ok();
}

UNSIGNED32 AdsCopyTableContents(ADSHANDLE hSrc, ADSHANDLE hDst) {
    Table* src = get_table(hSrc);
    Table* dst = get_table(hDst);
    if (!src || !dst) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    if (!src->driver() || !dst->driver()) {
        return fail(openads::AE_INTERNAL_ERROR, "no driver");
    }
    if (src->driver()->record_length() != dst->driver()->record_length()) {
        return fail(openads::AE_INTERNAL_ERROR,
                    "record length mismatch between src and dst");
    }
    auto src_count = src->driver()->record_count();
    for (std::uint32_t r = 1; r <= src_count; ++r) {
        auto rec = src->driver()->read_record_raw(r);
        if (!rec) return fail(rec.error());
        const auto& buf = rec.value();
        if (!buf.empty() && buf[0] == '*') continue;
        auto a = dst->driver()->append_record_raw(buf.data(), buf.size());
        if (!a) return fail(a.error());
    }
    if (auto fl = dst->flush(); !fl) return fail(fl.error());
    return ok();
}

UNSIGNED32 AdsConvertTable(ADSHANDLE   hHandle,
                           UNSIGNED16  usFilterOption,
                           UNSIGNED8*  pucFile,
                           UNSIGNED16  /*usTargetType*/) {
    // Single-format engine for now (CDX-flavoured DBF). Convert is a
    // copy that mirrors the source format; once ADT / VFP land the
    // target type will pick a different writer.
    return AdsCopyTable(hHandle, usFilterOption, pucFile);
}

UNSIGNED32 AdsReindex(ADSHANDLE hTable) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->reindex();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsPackTable_DEFERRED(ADSHANDLE /*hTable*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "AdsPackTable lands in M4 alongside memo store");
}

UNSIGNED32 AdsZapTable_DEFERRED(ADSHANDLE /*hTable*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "AdsZapTable lands in M4 alongside memo store");
}

UNSIGNED32 AdsSetAOF(ADSHANDLE /*hTable*/, UNSIGNED8* /*pucCondition*/,
                     UNSIGNED16 /*usResolve*/) {
    return ok();   // accept silently; AOF level remains NONE
}

UNSIGNED32 AdsGetAOFOptLevel(ADSHANDLE /*hTable*/, UNSIGNED16* pusLevel,
                             UNSIGNED8* /*pucBuf*/, UNSIGNED16* /*pusLen*/) {
    if (pusLevel != nullptr) *pusLevel = ADS_OPTIMIZED_NONE;
    return ok();
}

UNSIGNED32 AdsClearAOF(ADSHANDLE /*hTable*/) {
    return ok();
}

// --- M4 memo + autoinc + encryption ----------------------------------------

UNSIGNED32 AdsBinaryToFile(ADSHANDLE hTable, UNSIGNED8* pucField,
                           UNSIGNED8* pucPath) {
    Table* t = get_table(hTable);
    if (!t || pucPath == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    auto name = openads::abi::to_internal(pucField, 0);
    std::int32_t idx = t->field_index(name);
    if (idx < 0) return fail(openads::AE_COLUMN_NOT_FOUND, name.c_str());
    auto v = t->read_field(static_cast<std::uint16_t>(idx));
    if (!v) return fail(v.error());
    auto path = openads::abi::to_internal(pucPath, 0);
    auto fres = openads::platform::File::open(
        path, openads::platform::OpenMode::CreateRW);
    if (!fres) return fail(fres.error());
    auto file = std::move(fres).value();
    const auto& payload = v.value().as_string;
    auto wrote = file.write_at(0, payload.data(), payload.size());
    if (!wrote) return fail(wrote.error());
    return ok();
}

UNSIGNED32 AdsFileToBinary(ADSHANDLE hTable, UNSIGNED8* pucField,
                           UNSIGNED16 /*usType*/, UNSIGNED8* pucPath) {
    Table* t = get_table(hTable);
    if (!t || pucPath == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    auto path = openads::abi::to_internal(pucPath, 0);
    auto fres = openads::platform::File::open(
        path, openads::platform::OpenMode::ReadOnly);
    if (!fres) return fail(fres.error());
    auto file = std::move(fres).value();
    auto sz = file.size();
    if (!sz) return fail(sz.error());
    std::string payload;
    payload.resize(static_cast<std::size_t>(sz.value()));
    if (!payload.empty()) {
        auto rd = file.read_at(0, payload.data(), payload.size());
        if (!rd) return fail(rd.error());
    }
    auto name = openads::abi::to_internal(pucField, 0);
    std::int32_t idx = t->field_index(name);
    if (idx < 0) return fail(openads::AE_COLUMN_NOT_FOUND, name.c_str());
    auto r = t->set_field(static_cast<std::uint16_t>(idx), payload);
    if (!r) return fail(r.error());
    return ok();
}

// --- M9.13 binary memo (ADS_BINARY / ADS_IMAGE) ----------------------------
//
// rddads' adsGetValue / adsPutValue branch for ADS_BINARY+ADS_IMAGE
// fields call this trio instead of AdsGetString/AdsSetString so the
// payload is treated as raw bytes (length-prefixed, no NUL trimming,
// embedded zeros preserved). The engine stores the bytes through the
// existing memo store with an explicit FPT block-type tag, and reads
// back the field as bytes plus an offset window so the caller can do
// chunked reads through a small fixed-size buffer.

UNSIGNED32 AdsGetBinaryLength(ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED32* pulLength) {
    Table* t = get_table(hTable);
    if (!t || pulLength == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    *pulLength = static_cast<UNSIGNED32>(v.value().as_string.size());
    return ok();
}

UNSIGNED32 AdsGetBinary(ADSHANDLE hTable, UNSIGNED8* pucField,
                        UNSIGNED32 ulOffset, UNSIGNED8* pucBuf,
                        UNSIGNED32* pulLen) {
    Table* t = get_table(hTable);
    if (!t || pulLen == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    const std::string& s = v.value().as_string;
    UNSIGNED32 cap = *pulLen;
    UNSIGNED32 n = 0;
    if (ulOffset < s.size()) {
        UNSIGNED32 remaining = static_cast<UNSIGNED32>(s.size() - ulOffset);
        n = cap < remaining ? cap : remaining;
        if (pucBuf != nullptr && n > 0) {
            std::memcpy(pucBuf, s.data() + ulOffset, n);
        }
    }
    *pulLen = n;
    return ok();
}

// M9.16: chunked AdsSetBinary writes. The accumulator below holds
// pending bytes per (Table*, field_idx) pair until the caller has
// delivered every byte of `ulTotalBytes`; only then does the payload
// land in the memo store via set_field_binary. Stale accumulators are
// scrubbed when the table closes (purge_pending_binaries_for_table).

namespace {

struct PendingBinaryKey {
    Table*        table;
    std::uint16_t field;
    bool operator==(const PendingBinaryKey& o) const noexcept {
        return table == o.table && field == o.field;
    }
};
struct PendingBinaryHash {
    std::size_t operator()(const PendingBinaryKey& k) const noexcept {
        return std::hash<void*>{}(k.table) ^
               (static_cast<std::size_t>(k.field) << 1);
    }
};
struct PendingBinary {
    std::string                   payload;
    std::uint32_t                 total = 0;
    openads::drivers::MemoBlockType type =
        openads::drivers::MemoBlockType::Object;
};

std::unordered_map<PendingBinaryKey, PendingBinary, PendingBinaryHash>&
pending_binaries() {
    static std::unordered_map<PendingBinaryKey, PendingBinary,
                              PendingBinaryHash> m;
    return m;
}

openads::drivers::MemoBlockType
map_binary_type(UNSIGNED16 usBinaryType) {
    if (usBinaryType == ADS_IMAGE) {
        return openads::drivers::MemoBlockType::Picture;
    }
    if (usBinaryType == ADS_STRING || usBinaryType == ADS_MEMO) {
        return openads::drivers::MemoBlockType::Text;
    }
    return openads::drivers::MemoBlockType::Object;
}

}  // namespace

}  // extern "C"

void purge_pending_binaries_for_table(openads::engine::Table* t) {
    using openads::engine::Table;
    auto& m = pending_binaries();
    for (auto it = m.begin(); it != m.end(); ) {
        if (it->first.table == t) it = m.erase(it);
        else                      ++it;
    }
}

extern "C" {

UNSIGNED32 AdsSetBinary(ADSHANDLE hTable, UNSIGNED8* pucField,
                        UNSIGNED16 usBinaryType,
                        UNSIGNED32 ulTotalBytes, UNSIGNED32 ulOffset,
                        UNSIGNED8* pucBuf, UNSIGNED32 ulBytes) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto type = map_binary_type(usBinaryType);

    // Single-shot fast path. No accumulator state is created when the
    // caller delivers the whole payload in one go.
    if (ulOffset == 0 && ulBytes == ulTotalBytes) {
        // Drop any stale accumulator from a prior aborted chunked write.
        pending_binaries().erase(PendingBinaryKey{t, idx});
        std::string payload;
        if (pucBuf != nullptr && ulBytes > 0) {
            payload.assign(reinterpret_cast<const char*>(pucBuf), ulBytes);
        }
        auto r = t->set_field_binary(idx, payload, type);
        if (!r) return fail(r.error());
        return ok();
    }

    // Chunked path. Accumulate at the caller's offset; flush when the
    // payload reaches the announced total.
    auto& m = pending_binaries();
    PendingBinaryKey key{t, idx};
    auto it = m.find(key);
    if (ulOffset == 0) {
        // First chunk — reset (or create) the accumulator and lock in
        // the announced total + binary type.
        if (it != m.end()) it->second = PendingBinary{};
        else               it = m.emplace(key, PendingBinary{}).first;
        it->second.total = ulTotalBytes;
        it->second.type  = type;
        it->second.payload.assign(static_cast<std::size_t>(ulTotalBytes),
                                  '\0');
    } else {
        if (it == m.end()) {
            return fail(openads::AE_INTERNAL_ERROR,
                        "chunked AdsSetBinary: no pending payload");
        }
        if (ulTotalBytes != it->second.total) {
            return fail(openads::AE_INTERNAL_ERROR,
                        "chunked AdsSetBinary: total bytes changed mid-write");
        }
    }
    if (static_cast<std::uint64_t>(ulOffset) +
        static_cast<std::uint64_t>(ulBytes) >
        static_cast<std::uint64_t>(it->second.total)) {
        m.erase(it);
        return fail(openads::AE_INTERNAL_ERROR,
                    "chunked AdsSetBinary: chunk runs past total");
    }
    if (pucBuf != nullptr && ulBytes > 0) {
        std::memcpy(it->second.payload.data() + ulOffset, pucBuf, ulBytes);
    }

    if (ulOffset + ulBytes == it->second.total) {
        std::string payload = std::move(it->second.payload);
        auto pending_type = it->second.type;
        m.erase(it);
        auto r = t->set_field_binary(idx, payload, pending_type);
        if (!r) return fail(r.error());
    }
    return ok();
}

UNSIGNED32 AdsGetLastAutoinc(ADSHANDLE hTable, UNSIGNED32* pulValue) {
    Table* t = get_table(hTable);
    if (!t || pulValue == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    // ADT/VFP autoinc tracking lands when those drivers gain extended
    // type support. For now report 0 — the field still reads as part
    // of the record buffer for non-autoinc types.
    *pulValue = 0;
    return ok();
}

// Encryption thunks. The AES primitive is real (engine::Aes, validated
// against FIPS-197 / NIST SP 800-38A); the record-level boundary that
// marks a table encrypted on disk and re-keys per-record is part of a
// later milestone alongside ADT, since ADS-original encryption mode
// is not yet documented byte-for-byte. The thunks below behave as
// no-ops or fail with AE_FUNCTION_NOT_AVAILABLE.

UNSIGNED32 AdsEnableEncryption(ADSHANDLE /*hConnect*/, UNSIGNED8* /*pucPassword*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "AdsEnableEncryption pending ADS encryption-mode RE");
}

UNSIGNED32 AdsDisableEncryption(ADSHANDLE /*hConnect*/) {
    return ok();
}

UNSIGNED32 AdsIsEncryptionEnabled(ADSHANDLE /*hConnect*/, UNSIGNED16* pbEnabled) {
    if (pbEnabled != nullptr) *pbEnabled = 0;
    return ok();
}

UNSIGNED32 AdsIsTableEncrypted(ADSHANDLE /*hTable*/, UNSIGNED16* pbEncrypted) {
    if (pbEncrypted != nullptr) *pbEncrypted = 0;
    return ok();
}

UNSIGNED32 AdsIsRecordEncrypted(ADSHANDLE /*hTable*/, UNSIGNED16* pbEncrypted) {
    if (pbEncrypted != nullptr) *pbEncrypted = 0;
    return ok();
}

UNSIGNED32 AdsEncryptTable(ADSHANDLE /*hTable*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "AdsEncryptTable pending ADS encryption-mode RE");
}

UNSIGNED32 AdsDecryptTable(ADSHANDLE /*hTable*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "AdsDecryptTable pending ADS encryption-mode RE");
}

UNSIGNED32 AdsEncryptRecord(ADSHANDLE /*hTable*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "AdsEncryptRecord pending ADS encryption-mode RE");
}

UNSIGNED32 AdsDecryptRecord(ADSHANDLE /*hTable*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "AdsDecryptRecord pending ADS encryption-mode RE");
}

// --- M5 transaction surface (in-memory; WAL persistence pending) -----------

UNSIGNED32 AdsBeginTransaction(ADSHANDLE hConnect) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = c->begin_tx();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsCommitTransaction(ADSHANDLE hConnect) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = c->commit_tx();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsRollbackTransaction(ADSHANDLE hConnect) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = c->rollback_tx();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsInTransaction(ADSHANDLE hConnect, UNSIGNED16* pbInTx) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c || pbInTx == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    *pbInTx = c->in_tx() ? 1 : 0;
    return ok();
}

UNSIGNED32 AdsCreateSavepoint(ADSHANDLE hConnect, UNSIGNED8* pucName) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c || pucName == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    auto name = openads::abi::to_internal(pucName, 0);
    auto r = c->create_savepoint(name);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsRollbackTransaction80(ADSHANDLE hConnect, UNSIGNED8* pucSavepoint) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (pucSavepoint == nullptr) {
        // Full rollback if no savepoint name supplied (matches ACE legacy).
        auto r = c->rollback_tx();
        if (!r) return fail(r.error());
        return ok();
    }
    auto name = openads::abi::to_internal(pucSavepoint, 0);
    auto r = c->rollback_to_savepoint(name);
    if (!r) return fail(r.error());
    return ok();
}

// --- M9.12 Table directory iteration ---------------------------------------
//
// AdsFindFirstTable / AdsFindNextTable / AdsFindClose walk the
// connection's data directory and emit each entry whose name matches
// `pucMask` (FoxPro-style glob with `*` and `?`, case insensitive).
// Matches AE_SUCCESS while names remain; once exhausted, returns
// AE_NO_FILE_FOUND so the caller breaks out of its loop. The find
// handle is registered in the global registry under HandleKind::Find
// so it round-trips through ADSHANDLE without aliasing tables/cursors.

namespace {

// Truncate the matched filename into `pucBuf`, NUL-terminate when
// there's room, and report the on-wire length back through `pusLen`
// (matching the ACE convention rddads' AdsFindFirstTable/NextTable
// callers depend on).
UNSIGNED32 emit_name(UNSIGNED8* pucBuf, UNSIGNED16* pusLen,
                     const std::string& name) {
    if (pusLen == nullptr) return openads::AE_INTERNAL_ERROR;
    UNSIGNED16 cap = *pusLen;
    UNSIGNED16 n = static_cast<UNSIGNED16>(
        name.size() < cap ? name.size() : cap);
    if (pucBuf != nullptr && cap > 0) {
        std::memcpy(pucBuf, name.data(), n);
        if (n < cap) pucBuf[n] = '\0';
    }
    *pusLen = static_cast<UNSIGNED16>(name.size());
    return openads::AE_SUCCESS;
}

}  // namespace

UNSIGNED32 AdsFindFirstTable(ADSHANDLE   hConnect,
                             UNSIGNED8*  pucMask,
                             UNSIGNED8*  pucFileName,
                             UNSIGNED16* pusFileNameLen,
                             ADSHANDLE*  phFind) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (c == nullptr || phFind == nullptr || pusFileNameLen == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    std::string mask = pucMask
        ? openads::abi::to_internal(pucMask, 0)
        : std::string("*.dbf");
    if (mask.empty()) mask = "*.dbf";

    auto r = c->find_first_table(mask);
    if (!r) return fail(r.error());

    auto [find_ptr, name] = std::move(r).value();
    Handle gh = s.registry.register_object(HandleKind::Find, find_ptr);
    *phFind = gh;
    return emit_name(pucFileName, pusFileNameLen, name);
}

UNSIGNED32 AdsFindNextTable(ADSHANDLE   hConnect,
                            ADSHANDLE   hFind,
                            UNSIGNED8*  pucFileName,
                            UNSIGNED16* pusFileNameLen) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (c == nullptr || pusFileNameLen == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    auto* find = s.registry.lookup<Connection::TableFind>(hFind, HandleKind::Find);
    if (find == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "invalid find handle");
    }
    auto r = c->find_next_table(find);
    if (!r) return fail(r.error());
    return emit_name(pucFileName, pusFileNameLen, r.value());
}

UNSIGNED32 AdsFindClose(ADSHANDLE hConnect, ADSHANDLE hFind) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (c == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto* find = s.registry.lookup<Connection::TableFind>(hFind, HandleKind::Find);
    if (find == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "invalid find handle");
    }
    (void)c->find_close(find);
    s.registry.release(hFind);
    return ok();
}

// --- M6 Data Dictionary ----------------------------------------------------

UNSIGNED32 AdsDDCreate(UNSIGNED8* pucDictionary, UNSIGNED16 /*bEncrypt*/,
                       UNSIGNED8* /*pucAdminPassword*/,
                       ADSHANDLE* phConnect) {
    if (pucDictionary == nullptr || phConnect == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "null DD args");
    }
    auto path = openads::abi::to_internal(pucDictionary, 0);
    // Materialise an empty DD on disk, then open a Connection rooted at it.
    auto created = openads::engine::DataDict::create(path);
    if (!created) return fail(created.error());

    auto opened = Connection::open(path);
    if (!opened) return fail(opened.error());

    auto holder = std::make_unique<Connection>(std::move(opened).value());
    Connection* raw = holder.get();
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    Handle h = s.registry.register_object(HandleKind::Connection, raw);
    s.conns.emplace(h, std::move(holder));
    *phConnect = h;
    return ok();
}

UNSIGNED32 AdsDDAddTable(ADSHANDLE hConnect, UNSIGNED8* pucAlias,
                         UNSIGNED8* pucTablePath, UNSIGNED8* /*pucIndexPath*/,
                         UNSIGNED16 /*usCharType*/, UNSIGNED8* /*pucDescription*/,
                         UNSIGNED8* /*pucValidationExpression*/,
                         UNSIGNED8* /*pucValidationMessage*/) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (!c->has_dd()) {
        return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                    "connection has no data dictionary");
    }
    if (pucAlias == nullptr || pucTablePath == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "null DD-AddTable args");
    }
    auto alias = openads::abi::to_internal(pucAlias, 0);
    auto path  = openads::abi::to_internal(pucTablePath, 0);
    auto r = c->dd()->add_table(alias, path);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDRemoveTable(ADSHANDLE hConnect, UNSIGNED8* pucAlias,
                            UNSIGNED16 /*usDeleteFiles*/) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (!c->has_dd()) {
        return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                    "connection has no data dictionary");
    }
    if (pucAlias == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto alias = openads::abi::to_internal(pucAlias, 0);
    auto r = c->dd()->remove_table(alias);
    if (!r) return fail(r.error());
    return ok();
}

// --- M7.1 SQL surface -------------------------------------------------------

extern "C++" {

namespace {

struct SqlStatement {
    Connection* conn = nullptr;
    std::string sql;
};

std::unordered_map<ADSHANDLE, std::unique_ptr<SqlStatement>>& stmt_map() {
    static std::unordered_map<ADSHANDLE, std::unique_ptr<SqlStatement>> m;
    return m;
}

ADSHANDLE next_stmt_handle() {
    static std::uint64_t n = 0x60000000ULL;
    return ++n;
}

} // namespace

} // extern "C++"

UNSIGNED32 AdsCreateSQLStatement(ADSHANDLE hConnect, ADSHANDLE* phStatement) {
    if (phStatement == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto stmt = std::make_unique<SqlStatement>();
    stmt->conn = c;
    ADSHANDLE h = next_stmt_handle();
    stmt_map()[h] = std::move(stmt);
    *phStatement = h;
    return ok();
}

UNSIGNED32 AdsCloseSQLStatement(ADSHANDLE hStatement) {
    auto& m = stmt_map();
    m.erase(hStatement);
    return ok();
}

UNSIGNED32 AdsPrepareSQL(ADSHANDLE hStatement, UNSIGNED8* pucSQL) {
    auto& m = stmt_map();
    auto it = m.find(hStatement);
    if (it == m.end()) return fail(openads::AE_INTERNAL_ERROR, "unknown stmt");
    it->second->sql = openads::abi::to_internal(pucSQL, 0);
    return ok();
}

UNSIGNED32 AdsExecuteSQL(ADSHANDLE hStatement, ADSHANDLE* phCursor) {
    auto& m = stmt_map();
    auto it = m.find(hStatement);
    if (it == m.end()) return fail(openads::AE_INTERNAL_ERROR, "unknown stmt");
    if (it->second->sql.empty()) {
        return fail(openads::AE_PARSE_ERROR, "no prepared SQL");
    }
    UNSIGNED8 buf[2048];
    std::size_t n = std::min<std::size_t>(it->second->sql.size(), sizeof(buf) - 1);
    std::memcpy(buf, it->second->sql.data(), n);
    buf[n] = '\0';
    return AdsExecuteSQLDirect(hStatement, buf, phCursor);
}

UNSIGNED32 AdsExecuteSQLDirect(ADSHANDLE hStatement, UNSIGNED8* pucSQL,
                               ADSHANDLE* phCursor) {
    if (phCursor == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto& m = stmt_map();
    auto it = m.find(hStatement);
    if (it == m.end()) return fail(openads::AE_INTERNAL_ERROR, "unknown stmt");
    Connection* c = it->second->conn;
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto sql = openads::abi::to_internal(pucSQL, 0);

    // M10.5/M10.7/M10.9: dispatch on the leading keyword. INSERT /
    // UPDATE / DELETE / CREATE TABLE / CREATE INDEX write through
    // the engine and return no cursor (phCursor → 0); SELECT keeps
    // the M9.21 path.
    if (openads::sql::sql_is_create_table(sql)) {
        auto& s = state();
        auto ct = openads::sql::parse_create_table(sql);
        if (!ct) return fail(ct.error());
        // Build the rddads `NAME,Type,Len,Dec;…` field-def string and
        // route through AdsCreateTable so M9.5's parser owns the
        // schema-write logic.
        std::string defs;
        for (const auto& col : ct.value().columns) {
            if (!defs.empty()) defs.push_back(';');
            defs += col.name;
            defs.push_back(',');
            defs += col.type;
            if (col.length > 0) {
                defs.push_back(',');
                defs += std::to_string(col.length);
            }
            if (col.decimals > 0) {
                defs.push_back(',');
                defs += std::to_string(col.decimals);
            }
        }
        std::vector<UNSIGNED8> name_buf(ct.value().table.size() + 1, 0);
        std::memcpy(name_buf.data(), ct.value().table.data(),
                    ct.value().table.size());
        std::vector<UNSIGNED8> def_buf(defs.size() + 1, 0);
        std::memcpy(def_buf.data(), defs.data(), defs.size());
        ADSHANDLE hTable = 0;
        // Resolve the connection's registry handle so AdsCreateTable
        // can look it up the same way external callers do.
        ADSHANDLE conn_h = 0;
        s.registry.for_each_handle([&](Handle h, HandleKind k, void* p) {
            if (k != HandleKind::Connection) return;
            if (static_cast<Connection*>(p) == c) conn_h = h;
        });
        if (conn_h == 0) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
        UNSIGNED32 rc = AdsCreateTable(conn_h, name_buf.data(), nullptr,
                                       ADS_CDX, 0, 0, 0, 0,
                                       def_buf.data(), &hTable);
        if (rc != openads::AE_SUCCESS) return rc;
        // Close the table immediately; CREATE TABLE returns no cursor.
        AdsCloseTable(hTable);
        *phCursor = 0;
        return ok();
    }

    if (openads::sql::sql_is_create_index(sql)) {
        auto& s = state();
        auto ci = openads::sql::parse_create_index(sql);
        if (!ci) return fail(ci.error());
        // Resolve the connection handle and open the table to obtain
        // an ADSHANDLE for AdsCreateIndex61.
        ADSHANDLE conn_h = 0;
        s.registry.for_each_handle([&](Handle h, HandleKind k, void* p) {
            if (k != HandleKind::Connection) return;
            if (static_cast<Connection*>(p) == c) conn_h = h;
        });
        if (conn_h == 0) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
        std::vector<UNSIGNED8> name_buf(ci.value().table.size() + 1, 0);
        std::memcpy(name_buf.data(), ci.value().table.data(),
                    ci.value().table.size());
        ADSHANDLE hTable = 0;
        if (auto rc = AdsOpenTable(conn_h, name_buf.data(), name_buf.data(),
                                   ADS_CDX, 1, 1, 0, 1, &hTable);
            rc != openads::AE_SUCCESS) {
            return rc;
        }

        // CREATE INDEX writes a sibling .cdx by default. Build the
        // file name from the table's stem so subsequent reopens
        // auto-attach. Caller can specify the bag explicitly via the
        // tag name's prefix; for now derive it.
        namespace fs = std::filesystem;
        fs::path tbl_path(c->data_dir());
        tbl_path /= ci.value().table;
        if (!tbl_path.has_extension()) tbl_path.replace_extension(".dbf");
        fs::path bag = tbl_path;
        bag.replace_extension(".cdx");

        std::vector<UNSIGNED8> bag_buf(bag.string().size() + 1, 0);
        std::memcpy(bag_buf.data(), bag.string().data(),
                    bag.string().size());
        std::vector<UNSIGNED8> tag_buf(ci.value().tag.size() + 1, 0);
        std::memcpy(tag_buf.data(), ci.value().tag.data(),
                    ci.value().tag.size());
        std::vector<UNSIGNED8> expr_buf(ci.value().expression.size() + 1, 0);
        std::memcpy(expr_buf.data(), ci.value().expression.data(),
                    ci.value().expression.size());
        UNSIGNED32 opts = 0;
        if (ci.value().unique)     opts |= 0x01u;
        if (ci.value().descending) opts |= 0x02u;
        ADSHANDLE hIdx = 0;
        UNSIGNED32 rc = AdsCreateIndex61(
            hTable, bag_buf.data(), tag_buf.data(),
            expr_buf.data(), nullptr, nullptr,
            opts, 512, &hIdx);
        AdsCloseTable(hTable);
        if (rc != openads::AE_SUCCESS) return rc;
        *phCursor = 0;
        return ok();
    }

    if (openads::sql::sql_is_update(sql)) {
        auto upd = openads::sql::parse_update(sql);
        if (!upd) return fail(upd.error());
        auto th = c->open_table(upd.value().table,
                                openads::engine::TableType::Cdx,
                                openads::engine::OpenMode::Shared);
        if (!th) return fail(th.error());
        openads::engine::Table* tbl = c->lookup_table(th.value());
        if (!tbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
        // Pre-resolve assignments so a typo surfaces before any write.
        struct Assn {
            std::uint16_t                   field_index;
            openads::sql::InsertLiteral     value;
        };
        std::vector<Assn> assns;
        assns.reserve(upd.value().assignments.size());
        for (const auto& a : upd.value().assignments) {
            std::int32_t fidx = tbl->field_index(a.column);
            if (fidx < 0) {
                return fail(openads::AE_COLUMN_NOT_FOUND, a.column.c_str());
            }
            assns.push_back({static_cast<std::uint16_t>(fidx), a.value});
        }
        // Walk every live record, run optional WHERE via the same
        // engine filter machinery, and apply the assignments inline.
        if (upd.value().where) {
            // Leverage the same compile path as SELECT — but inline
            // a smaller version that walks the AST recursively. Reuse
            // is fine: same structure, no SQL features missing.
            // (Helper extraction is deferred until UPDATE picks up
            // CONTAINS or AND/OR — for now the closures below fully
            // cover the tree.)
            using Pred = std::function<bool(openads::engine::Table&)>;
            std::function<openads::util::Result<Pred>(
                const openads::sql::WhereExpr&)> compile;
            compile = [&](const openads::sql::WhereExpr& node)
                      -> openads::util::Result<Pred> {
                using Kind = openads::sql::WhereExpr::Kind;
                if (node.kind == Kind::And) {
                    std::vector<Pred> ks;
                    for (auto& cn : node.children) {
                        auto r = compile(*cn);
                        if (!r) return r.error();
                        ks.push_back(std::move(r).value());
                    }
                    return Pred{[ks = std::move(ks)](openads::engine::Table& t) {
                        for (auto& k : ks) if (!k(t)) return false;
                        return true;
                    }};
                }
                if (node.kind == Kind::Or) {
                    std::vector<Pred> ks;
                    for (auto& cn : node.children) {
                        auto r = compile(*cn);
                        if (!r) return r.error();
                        ks.push_back(std::move(r).value());
                    }
                    return Pred{[ks = std::move(ks)](openads::engine::Table& t) {
                        for (auto& k : ks) if (k(t)) return true;
                        return false;
                    }};
                }
                if (node.kind == Kind::Not) {
                    auto inner = compile(*node.child);
                    if (!inner) return inner.error();
                    return Pred{[p = std::move(inner).value()]
                                (openads::engine::Table& t){return !p(t);}};
                }
                const auto& w = node.cmp;
                std::int32_t fidx = tbl->field_index(w.column);
                if (fidx < 0) {
                    return openads::util::Error{
                        openads::AE_COLUMN_NOT_FOUND, 0,
                        w.column.c_str(), ""};
                }
                std::uint16_t fi = static_cast<std::uint16_t>(fidx);
                openads::sql::WhereOp op = w.op;
                std::string lit = w.literal;
                bool is_num     = w.is_numeric;
                double num      = w.number;
                return Pred{[fi, op, lit, is_num, num]
                            (openads::engine::Table& t) {
                    auto v = t.read_field(fi);
                    if (!v) return false;
                    int cmp = 0;
                    if (is_num) {
                        double d = v.value().as_double;
                        if      (d < num) cmp = -1;
                        else if (d > num) cmp =  1;
                    } else {
                        cmp = v.value().as_string.compare(lit);
                    }
                    switch (op) {
                        case openads::sql::WhereOp::Eq: return cmp == 0;
                        case openads::sql::WhereOp::Ne: return cmp != 0;
                        case openads::sql::WhereOp::Lt: return cmp <  0;
                        case openads::sql::WhereOp::Gt: return cmp >  0;
                        case openads::sql::WhereOp::Le: return cmp <= 0;
                        case openads::sql::WhereOp::Ge: return cmp >= 0;
                        case openads::sql::WhereOp::Contains: return false;
                    }
                    return false;
                }};
            };
            auto compiled = compile(*upd.value().where);
            if (!compiled) return fail(compiled.error());
            tbl->set_filter(std::move(compiled).value());
        }
        std::uint32_t rcount = tbl->record_count();
        for (std::uint32_t r = 1; r <= rcount; ++r) {
            if (auto g = tbl->goto_record(r); !g) continue;
            if (tbl->is_deleted()) continue;
            if (!tbl->passes_filter()) continue;
            for (const auto& a : assns) {
                if (a.value.is_numeric) {
                    auto wr = tbl->set_field(a.field_index, a.value.number);
                    if (!wr) return fail(wr.error());
                } else {
                    auto wr = tbl->set_field(a.field_index, a.value.text);
                    if (!wr) return fail(wr.error());
                }
            }
        }
        if (auto fl = tbl->flush(); !fl) return fail(fl.error());
        tbl->clear_filter();
        c->close_table(th.value());
        *phCursor = 0;
        return ok();
    }

    if (openads::sql::sql_is_delete(sql)) {
        auto del = openads::sql::parse_delete(sql);
        if (!del) return fail(del.error());
        auto th = c->open_table(del.value().table,
                                openads::engine::TableType::Cdx,
                                openads::engine::OpenMode::Shared);
        if (!th) return fail(th.error());
        openads::engine::Table* tbl = c->lookup_table(th.value());
        if (!tbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
        // Reuse the WHERE filter machinery for SELECT: it's already
        // wired and the predicate semantics match exactly.
        if (del.value().where) {
            // The compile lambda from the SELECT branch lives below;
            // copy a minimal bool-tree walker inline so DELETE isn't
            // forced to call into SELECT's path.
            using Pred = std::function<bool(openads::engine::Table&)>;
            std::function<openads::util::Result<Pred>(
                const openads::sql::WhereExpr&)> compile;
            compile = [&](const openads::sql::WhereExpr& node)
                      -> openads::util::Result<Pred> {
                using Kind = openads::sql::WhereExpr::Kind;
                if (node.kind == Kind::And) {
                    std::vector<Pred> ks;
                    for (auto& cn : node.children) {
                        auto r = compile(*cn);
                        if (!r) return r.error();
                        ks.push_back(std::move(r).value());
                    }
                    return Pred{[ks = std::move(ks)](openads::engine::Table& t) {
                        for (auto& k : ks) if (!k(t)) return false;
                        return true;
                    }};
                }
                if (node.kind == Kind::Or) {
                    std::vector<Pred> ks;
                    for (auto& cn : node.children) {
                        auto r = compile(*cn);
                        if (!r) return r.error();
                        ks.push_back(std::move(r).value());
                    }
                    return Pred{[ks = std::move(ks)](openads::engine::Table& t) {
                        for (auto& k : ks) if (k(t)) return true;
                        return false;
                    }};
                }
                if (node.kind == Kind::Not) {
                    auto inner = compile(*node.child);
                    if (!inner) return inner.error();
                    return Pred{[p = std::move(inner).value()]
                                (openads::engine::Table& t){return !p(t);}};
                }
                const auto& w = node.cmp;
                std::int32_t fidx = tbl->field_index(w.column);
                if (fidx < 0) {
                    return openads::util::Error{
                        openads::AE_COLUMN_NOT_FOUND, 0,
                        w.column.c_str(), ""};
                }
                std::uint16_t fi = static_cast<std::uint16_t>(fidx);
                openads::sql::WhereOp op = w.op;
                std::string lit = w.literal;
                bool is_num     = w.is_numeric;
                double num      = w.number;
                return Pred{[fi, op, lit, is_num, num]
                            (openads::engine::Table& t) {
                    auto v = t.read_field(fi);
                    if (!v) return false;
                    int cmp = 0;
                    if (is_num) {
                        double d = v.value().as_double;
                        if      (d < num) cmp = -1;
                        else if (d > num) cmp =  1;
                    } else {
                        cmp = v.value().as_string.compare(lit);
                    }
                    switch (op) {
                        case openads::sql::WhereOp::Eq: return cmp == 0;
                        case openads::sql::WhereOp::Ne: return cmp != 0;
                        case openads::sql::WhereOp::Lt: return cmp <  0;
                        case openads::sql::WhereOp::Gt: return cmp >  0;
                        case openads::sql::WhereOp::Le: return cmp <= 0;
                        case openads::sql::WhereOp::Ge: return cmp >= 0;
                        case openads::sql::WhereOp::Contains: return false;
                    }
                    return false;
                }};
            };
            auto compiled = compile(*del.value().where);
            if (!compiled) return fail(compiled.error());
            tbl->set_filter(std::move(compiled).value());
        }
        std::uint32_t rcount = tbl->record_count();
        for (std::uint32_t r = 1; r <= rcount; ++r) {
            if (auto g = tbl->goto_record(r); !g) continue;
            if (tbl->is_deleted()) continue;
            if (!tbl->passes_filter()) continue;
            (void)tbl->mark_deleted();
        }
        if (auto fl = tbl->flush(); !fl) return fail(fl.error());
        tbl->clear_filter();
        c->close_table(th.value());
        *phCursor = 0;
        return ok();
    }

    if (openads::sql::sql_is_insert(sql)) {
        auto ins = openads::sql::parse_insert(sql);
        if (!ins) return fail(ins.error());
        auto th = c->open_table(ins.value().table,
                                openads::engine::TableType::Cdx,
                                openads::engine::OpenMode::Shared);
        if (!th) return fail(th.error());
        openads::engine::Table* tbl = c->lookup_table(th.value());
        if (!tbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
        if (auto r = tbl->append_record(); !r) return fail(r.error());
        for (std::size_t i = 0; i < ins.value().columns.size(); ++i) {
            std::int32_t fidx =
                tbl->field_index(ins.value().columns[i]);
            if (fidx < 0) {
                return fail(openads::AE_COLUMN_NOT_FOUND,
                            ins.value().columns[i].c_str());
            }
            const auto& v = ins.value().values[i];
            if (v.is_numeric) {
                auto wr = tbl->set_field(static_cast<std::uint16_t>(fidx),
                                         v.number);
                if (!wr) return fail(wr.error());
            } else {
                auto wr = tbl->set_field(static_cast<std::uint16_t>(fidx),
                                         v.text);
                if (!wr) return fail(wr.error());
            }
        }
        if (auto fl = tbl->flush(); !fl) return fail(fl.error());
        c->close_table(th.value());
        *phCursor = 0;
        return ok();
    }

    auto parsed = openads::sql::parse_select(sql);
    if (!parsed) return fail(parsed.error());

    if (parsed.value().inner_join) {
        // M10.14 materialises the join into a temp DBF cursor; M10.20
        // additionally compiles the outer WHERE / ORDER BY against
        // that cursor's merged schema. Aggregate combos still defer.
        if (!parsed.value().aggregates.empty()) {
            return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                        "JOIN + aggregate in same query deferred");
        }

        const auto& j = *parsed.value().inner_join;
        auto& s = state();
        std::lock_guard<std::mutex> lk(s.mu);

        auto lh = c->open_table(parsed.value().table,
                                openads::engine::TableType::Cdx,
                                openads::engine::OpenMode::Read);
        if (!lh) return fail(lh.error());
        auto rh = c->open_table(j.table,
                                openads::engine::TableType::Cdx,
                                openads::engine::OpenMode::Read);
        if (!rh) {
            c->close_table(lh.value());
            return fail(rh.error());
        }
        openads::engine::Table* ltbl = c->lookup_table(lh.value());
        openads::engine::Table* rtbl = c->lookup_table(rh.value());
        if (ltbl == nullptr || rtbl == nullptr) {
            return fail(openads::AE_INTERNAL_ERROR, "join post-open");
        }

        std::int32_t lcol = ltbl->field_index(j.left_column);
        std::int32_t rcol = rtbl->field_index(j.right_column);
        if (lcol < 0) {
            c->close_table(lh.value()); c->close_table(rh.value());
            return fail(openads::AE_COLUMN_NOT_FOUND,
                        j.left_column.c_str());
        }
        if (rcol < 0) {
            c->close_table(lh.value()); c->close_table(rh.value());
            return fail(openads::AE_COLUMN_NOT_FOUND,
                        j.right_column.c_str());
        }

        auto trim_trailing = [](std::string s) {
            while (!s.empty() && s.back() == ' ') s.pop_back();
            return s;
        };

        // INNER / LEFT walk left + lookup right. RIGHT swaps that —
        // walk right + lookup left. FULL walks left first (emitting
        // matched + LEFT-style fillers) and then walks right to emit
        // only the unmatched right rows with a blank left filler.
        // The merged schema's column order (left fields first, then
        // right with `R_` prefix) stays identical regardless of join
        // direction so the cursor exposes the same shape to apps.
        bool walk_right = j.is_right;

        std::unordered_map<std::string, std::vector<std::uint32_t>> probe_map;
        if (walk_right) {
            // Hash the LEFT column (we'll walk the right side and
            // look up each right row's join value in this map).
            std::uint32_t lrc_for_hash = ltbl->record_count();
            for (std::uint32_t r = 1; r <= lrc_for_hash; ++r) {
                if (auto g = ltbl->goto_record(r); !g) continue;
                if (ltbl->is_deleted()) continue;
                auto v = ltbl->read_field(
                    static_cast<std::uint16_t>(lcol));
                if (!v) continue;
                probe_map[trim_trailing(v.value().as_string)].push_back(r);
            }
        } else {
            // Default: hash the RIGHT column (M10.14 + M10.16 path).
            std::uint32_t rrc = rtbl->record_count();
            for (std::uint32_t r = 1; r <= rrc; ++r) {
                if (auto g = rtbl->goto_record(r); !g) continue;
                if (rtbl->is_deleted()) continue;
                auto v = rtbl->read_field(
                    static_cast<std::uint16_t>(rcol));
                if (!v) continue;
                probe_map[trim_trailing(v.value().as_string)].push_back(r);
            }
        }
        // Keep the legacy name `rmap` working — the executor below
        // walks one side and probes the other through this map.
        auto& rmap = probe_map;

        // Build merged schema.
        std::vector<openads::drivers::DbfField> merged;
        const auto& lfields = ltbl->driver()->fields();
        const auto& rfields = rtbl->driver()->fields();
        for (const auto& f : lfields) merged.push_back(f);
        for (auto f : rfields) {
            std::string nm = "R_" + f.name;
            if (nm.size() > 10) nm.resize(10);
            f.name = std::move(nm);
            merged.push_back(f);
        }

        std::uint16_t header_len = static_cast<std::uint16_t>(
            32 + 32 * merged.size() + 1);
        std::uint32_t lrec = ltbl->driver()->record_length();
        std::uint32_t rrec = rtbl->driver()->record_length();
        std::uint32_t merged_rec = 1 + (lrec - 1) + (rrec - 1);
        if (merged_rec > 0xFFFF) {
            c->close_table(lh.value()); c->close_table(rh.value());
            return fail(openads::AE_INTERNAL_ERROR,
                        "joined record exceeds 64 KiB");
        }

        // Lay out file bytes: header + field-desc + records + EOF.
        std::vector<std::uint8_t> file;
        std::array<std::uint8_t, 32> hdr{};
        hdr[0] = 0x03;
        hdr[8]  = static_cast<std::uint8_t>( header_len       & 0xFFu);
        hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
        hdr[10] = static_cast<std::uint8_t>( merged_rec       & 0xFFu);
        hdr[11] = static_cast<std::uint8_t>((merged_rec >> 8) & 0xFFu);
        file.insert(file.end(), hdr.begin(), hdr.end());
        for (const auto& f : merged) {
            std::array<std::uint8_t, 32> fd{};
            std::size_t n = std::min<std::size_t>(f.name.size(), 10);
            std::memcpy(fd.data(), f.name.data(), n);
            fd[11] = static_cast<std::uint8_t>(f.raw_type);
            fd[16] = f.length;
            fd[17] = f.decimals;
            file.insert(file.end(), fd.begin(), fd.end());
        }
        file.push_back(0x0D);

        // Helper: emit one merged record with explicit left/right
        // byte slices. Either side may be null — outer-join fillers
        // pass nullptr for the side that has no match.
        std::uint32_t emitted = 0;
        auto emit_merged = [&](const std::uint8_t* lbytes, std::size_t lsize,
                               const std::uint8_t* rbytes, std::size_t rsize) {
            std::vector<std::uint8_t> mrec(merged_rec, ' ');
            mrec[0] = (lbytes != nullptr && lsize > 0) ? lbytes[0] : ' ';
            if (lbytes != nullptr && lrec > 1 && lsize >= lrec) {
                std::memcpy(mrec.data() + 1, lbytes + 1, lrec - 1);
            }
            if (rbytes != nullptr && rrec > 1 && rsize >= rrec) {
                std::memcpy(mrec.data() + lrec, rbytes + 1, rrec - 1);
            }
            file.insert(file.end(), mrec.begin(), mrec.end());
            ++emitted;
        };

        if (walk_right) {
            // RIGHT OUTER — walk right rows, look up the LEFT hash.
            // Unmatched right rows surface with blank left fields.
            std::uint32_t rrc = rtbl->record_count();
            for (std::uint32_t r = 1; r <= rrc; ++r) {
                if (auto g = rtbl->goto_record(r); !g) continue;
                if (rtbl->is_deleted()) continue;
                auto rv = rtbl->read_field(
                    static_cast<std::uint16_t>(rcol));
                if (!rv) continue;
                auto lit = rmap.find(trim_trailing(rv.value().as_string));
                auto rraw = rtbl->driver()->read_record_raw(r);
                if (!rraw) continue;
                const auto& rbuf = rraw.value();
                if (lit == rmap.end()) {
                    emit_merged(nullptr, 0, rbuf.data(), rbuf.size());
                    continue;
                }
                for (std::uint32_t ll : lit->second) {
                    auto lraw = ltbl->driver()->read_record_raw(ll);
                    if (!lraw) continue;
                    const auto& lbuf = lraw.value();
                    emit_merged(lbuf.data(), lbuf.size(),
                                rbuf.data(), rbuf.size());
                }
            }
        } else {
            // INNER / LEFT / FULL — walk left rows, look up the RIGHT
            // hash. Unmatched left rows surface with blank right
            // fields when is_left or is_full; dropped otherwise.
            std::unordered_set<std::uint32_t> matched_right;
            std::uint32_t lrc = ltbl->record_count();
            for (std::uint32_t l = 1; l <= lrc; ++l) {
                if (auto g = ltbl->goto_record(l); !g) continue;
                if (ltbl->is_deleted()) continue;
                auto lv = ltbl->read_field(
                    static_cast<std::uint16_t>(lcol));
                if (!lv) continue;
                auto rit = rmap.find(trim_trailing(lv.value().as_string));
                auto lraw = ltbl->driver()->read_record_raw(l);
                if (!lraw) continue;
                const auto& lbuf = lraw.value();
                if (rit == rmap.end()) {
                    if (j.is_left || j.is_full) {
                        emit_merged(lbuf.data(), lbuf.size(), nullptr, 0);
                    }
                    continue;
                }
                for (std::uint32_t rr : rit->second) {
                    auto rraw = rtbl->driver()->read_record_raw(rr);
                    if (!rraw) continue;
                    const auto& rbuf = rraw.value();
                    emit_merged(lbuf.data(), lbuf.size(),
                                rbuf.data(), rbuf.size());
                    if (j.is_full) matched_right.insert(rr);
                }
            }
            // FULL OUTER: emit unmatched right rows with blank left.
            if (j.is_full) {
                std::uint32_t rrc = rtbl->record_count();
                for (std::uint32_t r = 1; r <= rrc; ++r) {
                    if (matched_right.find(r) != matched_right.end()) continue;
                    if (auto g = rtbl->goto_record(r); !g) continue;
                    if (rtbl->is_deleted()) continue;
                    auto rraw = rtbl->driver()->read_record_raw(r);
                    if (!rraw) continue;
                    const auto& rbuf = rraw.value();
                    emit_merged(nullptr, 0, rbuf.data(), rbuf.size());
                }
            }
        }
        file.push_back(0x1A);
        // Patch record count (header bytes 4-7).
        file[4] = static_cast<std::uint8_t>( emitted        & 0xFFu);
        file[5] = static_cast<std::uint8_t>((emitted >>  8) & 0xFFu);
        file[6] = static_cast<std::uint8_t>((emitted >> 16) & 0xFFu);
        file[7] = static_cast<std::uint8_t>((emitted >> 24) & 0xFFu);

        c->close_table(lh.value());
        c->close_table(rh.value());

        // Write temp DBF.
        namespace fs = std::filesystem;
        char namebuf[64];
        std::snprintf(namebuf, sizeof(namebuf), "_join_%llx.dbf",
                      static_cast<unsigned long long>(
                          openads::platform::monotonic_nanos()));
        fs::path tmp_dbf = fs::path(c->data_dir()) / namebuf;
        {
            std::ofstream out(tmp_dbf, std::ios::binary);
            if (!out) return fail(openads::AE_INTERNAL_ERROR,
                                  "join temp DBF open failed");
            out.write(reinterpret_cast<const char*>(file.data()),
                      static_cast<std::streamsize>(file.size()));
        }

        std::string rel = tmp_dbf.filename().string();
        auto cth = c->open_table(rel,
                                 openads::engine::TableType::Cdx,
                                 openads::engine::OpenMode::Read);
        if (!cth) return fail(cth.error());
        openads::engine::Table* ctbl = c->lookup_table(cth.value());
        if (!ctbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");

        // M10.20: apply outer WHERE / ORDER BY against the merged
        // cursor's schema (left names verbatim; right names as
        // `R_<orig>`).
        if (parsed.value().where) {
            using Pred = std::function<bool(openads::engine::Table&)>;
            std::function<openads::util::Result<Pred>(
                const openads::sql::WhereExpr&)> compile;
            compile = [&](const openads::sql::WhereExpr& node)
                      -> openads::util::Result<Pred> {
                using Kind = openads::sql::WhereExpr::Kind;
                if (node.kind == Kind::And || node.kind == Kind::Or) {
                    std::vector<Pred> ks;
                    for (auto& cn : node.children) {
                        auto r = compile(*cn);
                        if (!r) return r.error();
                        ks.push_back(std::move(r).value());
                    }
                    bool is_and = (node.kind == Kind::And);
                    return Pred{[ks = std::move(ks), is_and]
                                (openads::engine::Table& t) {
                        if (is_and) {
                            for (auto& k : ks) if (!k(t)) return false;
                            return true;
                        }
                        for (auto& k : ks) if (k(t)) return true;
                        return false;
                    }};
                }
                if (node.kind == Kind::Not) {
                    auto inner = compile(*node.child);
                    if (!inner) return inner.error();
                    return Pred{[p = std::move(inner).value()]
                                (openads::engine::Table& t){return !p(t);}};
                }
                if (node.kind == Kind::Cmp) {
                    const auto& w = node.cmp;
                    std::int32_t fi = ctbl->field_index(w.column);
                    if (fi < 0) {
                        return openads::util::Error{
                            openads::AE_COLUMN_NOT_FOUND, 0,
                            w.column.c_str(), ""};
                    }
                    std::uint16_t f = static_cast<std::uint16_t>(fi);
                    openads::sql::WhereOp op = w.op;
                    std::string lit = w.literal;
                    bool is_num     = w.is_numeric;
                    double num      = w.number;
                    return Pred{[f, op, lit, is_num, num]
                                (openads::engine::Table& t) {
                        auto v = t.read_field(f);
                        if (!v) return false;
                        int cmp = 0;
                        if (is_num) {
                            double d = v.value().as_double;
                            if      (d < num) cmp = -1;
                            else if (d > num) cmp =  1;
                        } else {
                            cmp = v.value().as_string.compare(lit);
                        }
                        switch (op) {
                            case openads::sql::WhereOp::Eq: return cmp == 0;
                            case openads::sql::WhereOp::Ne: return cmp != 0;
                            case openads::sql::WhereOp::Lt: return cmp <  0;
                            case openads::sql::WhereOp::Gt: return cmp >  0;
                            case openads::sql::WhereOp::Le: return cmp <= 0;
                            case openads::sql::WhereOp::Ge: return cmp >= 0;
                            case openads::sql::WhereOp::Contains: return false;
                        }
                        return false;
                    }};
                }
                return openads::util::Error{
                    openads::AE_FUNCTION_NOT_AVAILABLE, 0,
                    "join cursor WHERE supports Cmp/AND/OR/NOT only", ""};
            };
            auto compiled = compile(*parsed.value().where);
            if (!compiled) return fail(compiled.error());
            ctbl->set_filter(std::move(compiled).value());
        }
        if (parsed.value().order_by) {
            const auto& ob = *parsed.value().order_by;
            std::int32_t fi = ctbl->field_index(ob.column);
            if (fi < 0) return fail(openads::AE_COLUMN_NOT_FOUND,
                                    ob.column.c_str());
            std::uint16_t key = static_cast<std::uint16_t>(fi);
            const auto& fdesc = ctbl->field_descriptor(key);
            bool numeric_sort =
                fdesc.type == openads::drivers::DbfFieldType::Numeric ||
                fdesc.type == openads::drivers::DbfFieldType::Float   ||
                fdesc.type == openads::drivers::DbfFieldType::Integer ||
                fdesc.type == openads::drivers::DbfFieldType::Currency||
                fdesc.type == openads::drivers::DbfFieldType::Double;
            std::vector<std::pair<std::uint32_t, std::string>> ent_str;
            std::vector<std::pair<std::uint32_t, double>>      ent_num;
            std::uint32_t crc = ctbl->record_count();
            for (std::uint32_t r = 1; r <= crc; ++r) {
                if (auto g = ctbl->goto_record(r); !g) continue;
                if (ctbl->is_deleted()) continue;
                if (!ctbl->passes_filter()) continue;
                auto v = ctbl->read_field(key);
                if (!v) continue;
                if (numeric_sort) ent_num.push_back({r, v.value().as_double});
                else              ent_str.push_back({r, v.value().as_string});
            }
            std::vector<std::uint32_t> seq;
            if (numeric_sort) {
                std::stable_sort(ent_num.begin(), ent_num.end(),
                    [&](auto& a, auto& b) {
                        return ob.descending ? a.second > b.second
                                             : a.second < b.second;
                    });
                seq.reserve(ent_num.size());
                for (auto& p : ent_num) seq.push_back(p.first);
            } else {
                std::stable_sort(ent_str.begin(), ent_str.end(),
                    [&](auto& a, auto& b) {
                        return ob.descending ? a.second > b.second
                                             : a.second < b.second;
                    });
                seq.reserve(ent_str.size());
                for (auto& p : ent_str) seq.push_back(p.first);
            }
            ctbl->clear_filter();
            ctbl->set_recno_sequence(std::move(seq));
        }

        ADSHANDLE gh = s.registry.register_object(HandleKind::Table, ctbl);
        *phCursor = gh;
        return ok();
    }

    auto th = c->open_table(parsed.value().table,
                            openads::engine::TableType::Cdx,
                            openads::engine::OpenMode::Read);
    if (!th) return fail(th.error());
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    openads::engine::Table* tbl = c->lookup_table(th.value());
    if (!tbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");

    // M10.10: aggregate query — walk matching rows, compute the
    // aggregate accumulators, materialise a 1-row temp DBF with one
    // numeric column per aggregate, and return a cursor on it.
    if (!parsed.value().aggregates.empty()) {
        // Resolve each aggregate's column index up front.
        struct AggSlot {
            openads::sql::Aggregate def;
            std::int32_t            field_index = -1;   // -1 for COUNT(*)
        };
        std::vector<AggSlot> slots;
        slots.reserve(parsed.value().aggregates.size());
        for (auto& a : parsed.value().aggregates) {
            AggSlot slot;
            slot.def = a;
            if (a.kind != openads::sql::AggregateKind::CountStar) {
                slot.field_index = tbl->field_index(a.column);
                if (slot.field_index < 0) {
                    c->close_table(th.value());
                    return fail(openads::AE_COLUMN_NOT_FOUND, a.column.c_str());
                }
            }
            slots.push_back(std::move(slot));
        }

        // Build the WHERE filter (same shape as the SELECT branch
        // below — but the predicate compiles independently here so
        // the aggregate path doesn't depend on that block's lambdas).
        std::function<bool(openads::engine::Table&)> filter;
        if (parsed.value().where) {
            using Pred = std::function<bool(openads::engine::Table&)>;
            std::function<openads::util::Result<Pred>(
                const openads::sql::WhereExpr&)> compile;
            compile = [&](const openads::sql::WhereExpr& node)
                      -> openads::util::Result<Pred> {
                using Kind = openads::sql::WhereExpr::Kind;
                if (node.kind == Kind::And || node.kind == Kind::Or) {
                    std::vector<Pred> ks;
                    for (auto& cn : node.children) {
                        auto r = compile(*cn);
                        if (!r) return r.error();
                        ks.push_back(std::move(r).value());
                    }
                    bool is_and = (node.kind == Kind::And);
                    return Pred{[ks = std::move(ks), is_and]
                                (openads::engine::Table& t) {
                        if (is_and) {
                            for (auto& k : ks) if (!k(t)) return false;
                            return true;
                        }
                        for (auto& k : ks) if (k(t)) return true;
                        return false;
                    }};
                }
                if (node.kind == Kind::Not) {
                    auto inner = compile(*node.child);
                    if (!inner) return inner.error();
                    return Pred{[p = std::move(inner).value()]
                                (openads::engine::Table& t){return !p(t);}};
                }
                const auto& w = node.cmp;
                std::int32_t fidx = tbl->field_index(w.column);
                if (fidx < 0) {
                    return openads::util::Error{
                        openads::AE_COLUMN_NOT_FOUND, 0,
                        w.column.c_str(), ""};
                }
                std::uint16_t fi = static_cast<std::uint16_t>(fidx);
                openads::sql::WhereOp op = w.op;
                std::string lit = w.literal;
                bool is_num     = w.is_numeric;
                double num      = w.number;
                return Pred{[fi, op, lit, is_num, num]
                            (openads::engine::Table& t) {
                    auto v = t.read_field(fi);
                    if (!v) return false;
                    int cmp = 0;
                    if (is_num) {
                        double d = v.value().as_double;
                        if      (d < num) cmp = -1;
                        else if (d > num) cmp =  1;
                    } else {
                        cmp = v.value().as_string.compare(lit);
                    }
                    switch (op) {
                        case openads::sql::WhereOp::Eq: return cmp == 0;
                        case openads::sql::WhereOp::Ne: return cmp != 0;
                        case openads::sql::WhereOp::Lt: return cmp <  0;
                        case openads::sql::WhereOp::Gt: return cmp >  0;
                        case openads::sql::WhereOp::Le: return cmp <= 0;
                        case openads::sql::WhereOp::Ge: return cmp >= 0;
                        case openads::sql::WhereOp::Contains: return false;
                    }
                    return false;
                }};
            };
            auto compiled = compile(*parsed.value().where);
            if (!compiled) {
                c->close_table(th.value());
                return fail(compiled.error());
            }
            filter = std::move(compiled).value();
        }

        // Walk matching rows, accumulate per slot.
        std::vector<double> sum(slots.size(), 0.0);
        std::vector<double> minv(slots.size(),
            std::numeric_limits<double>::infinity());
        std::vector<double> maxv(slots.size(),
            -std::numeric_limits<double>::infinity());
        std::vector<std::uint64_t> count(slots.size(), 0);
        std::uint64_t row_count = 0;
        std::uint32_t rcount = tbl->record_count();
        for (std::uint32_t r = 1; r <= rcount; ++r) {
            if (auto g = tbl->goto_record(r); !g) continue;
            if (tbl->is_deleted()) continue;
            if (filter && !filter(*tbl)) continue;
            ++row_count;
            for (std::size_t i = 0; i < slots.size(); ++i) {
                if (slots[i].def.kind == openads::sql::AggregateKind::CountStar) {
                    ++count[i];
                    continue;
                }
                auto v = tbl->read_field(
                    static_cast<std::uint16_t>(slots[i].field_index));
                if (!v) continue;
                double d = v.value().as_double;
                ++count[i];
                sum[i] += d;
                if (d < minv[i]) minv[i] = d;
                if (d > maxv[i]) maxv[i] = d;
            }
        }
        c->close_table(th.value());

        // Write a 1-row temp DBF with one C(30) field per aggregate
        // and the formatted result in each slot.
        namespace fs = std::filesystem;
        fs::path tmp_dbf = fs::path(c->data_dir());
        char namebuf[64];
        std::snprintf(namebuf, sizeof(namebuf), "_agg_%llx.dbf",
                      static_cast<unsigned long long>(
                          openads::platform::monotonic_nanos()));
        tmp_dbf /= namebuf;
        std::vector<std::uint8_t> file;
        std::array<std::uint8_t, 32> hdr{};
        hdr[0] = 0x03;
        hdr[4] = 1;
        std::uint16_t header_len = static_cast<std::uint16_t>(
            32 + 32 * slots.size() + 1);
        std::uint16_t rec_len = static_cast<std::uint16_t>(
            1 + 30 * slots.size());
        hdr[8]  = static_cast<std::uint8_t>( header_len       & 0xFFu);
        hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
        hdr[10] = static_cast<std::uint8_t>( rec_len          & 0xFFu);
        hdr[11] = static_cast<std::uint8_t>((rec_len    >> 8) & 0xFFu);
        file.insert(file.end(), hdr.begin(), hdr.end());
        for (std::size_t i = 0; i < slots.size(); ++i) {
            std::array<std::uint8_t, 32> fd{};
            char fn[16];
            std::snprintf(fn, sizeof(fn), "COL%zu", i + 1);
            std::strncpy(reinterpret_cast<char*>(fd.data()), fn, 11);
            fd[11] = 'C'; fd[16] = 30;
            file.insert(file.end(), fd.begin(), fd.end());
        }
        file.push_back(0x0D);
        file.push_back(' ');
        for (std::size_t i = 0; i < slots.size(); ++i) {
            char buf[32] = {0};
            switch (slots[i].def.kind) {
                case openads::sql::AggregateKind::CountStar:
                case openads::sql::AggregateKind::Count:
                    std::snprintf(buf, sizeof(buf), "%llu",
                        static_cast<unsigned long long>(
                            slots[i].def.kind ==
                            openads::sql::AggregateKind::CountStar
                                ? row_count : count[i]));
                    break;
                case openads::sql::AggregateKind::Sum:
                    std::snprintf(buf, sizeof(buf), "%.6f", sum[i]);
                    break;
                case openads::sql::AggregateKind::Avg:
                    std::snprintf(buf, sizeof(buf), "%.6f",
                        count[i] ? sum[i] / static_cast<double>(count[i])
                                 : 0.0);
                    break;
                case openads::sql::AggregateKind::Min:
                    if (count[i] == 0) std::strcpy(buf, "0");
                    else std::snprintf(buf, sizeof(buf), "%.6f", minv[i]);
                    break;
                case openads::sql::AggregateKind::Max:
                    if (count[i] == 0) std::strcpy(buf, "0");
                    else std::snprintf(buf, sizeof(buf), "%.6f", maxv[i]);
                    break;
            }
            std::array<std::uint8_t, 30> cell{};
            std::memset(cell.data(), ' ', cell.size());
            std::size_t n = std::min<std::size_t>(std::strlen(buf), 30);
            std::memcpy(cell.data(), buf, n);
            file.insert(file.end(), cell.begin(), cell.end());
        }
        file.push_back(0x1A);
        {
            std::ofstream out(tmp_dbf, std::ios::binary);
            if (!out) return fail(openads::AE_INTERNAL_ERROR,
                                  "aggregate temp DBF: open for write failed");
            out.write(reinterpret_cast<const char*>(file.data()),
                      static_cast<std::streamsize>(file.size()));
            // Explicit close before re-opening for read.
        }

        // Open the temp DBF as the cursor.
        std::string rel = tmp_dbf.filename().string();
        auto cth = c->open_table(rel, openads::engine::TableType::Cdx,
                                 openads::engine::OpenMode::Read);
        if (!cth) return fail(cth.error());
        openads::engine::Table* ctbl = c->lookup_table(cth.value());
        if (!ctbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
        ADSHANDLE gh = s.registry.register_object(HandleKind::Table, ctbl);
        *phCursor = gh;
        return ok();
    }

    // Compile the WHERE expression tree into a row-predicate closure
    // (M10.3). CONTAINS captures a precomputed recno set at compile
    // time; AND / OR / NOT short-circuit during evaluation.
    if (parsed.value().where) {
        // Compiled term per Cmp leaf.
        struct CmpTerm {
            std::uint16_t                       field_index = 0;
            openads::sql::WhereOp               op = openads::sql::WhereOp::Eq;
            std::string                         literal;
            bool                                is_numeric = false;
            double                              number = 0.0;
            std::shared_ptr<std::unordered_set<std::uint32_t>> contains_hits;
        };

        // Compile the AST into a Predicate functor.
        using Pred = std::function<bool(openads::engine::Table&)>;
        std::function<openads::util::Result<Pred>(
            const openads::sql::WhereExpr&)> compile;
        compile = [&](const openads::sql::WhereExpr& node)
                  -> openads::util::Result<Pred> {
            using Kind = openads::sql::WhereExpr::Kind;
            if (node.kind == Kind::And) {
                std::vector<Pred> kids;
                for (auto& cn : node.children) {
                    auto r = compile(*cn);
                    if (!r) return r.error();
                    kids.push_back(std::move(r).value());
                }
                return Pred{[kids = std::move(kids)](openads::engine::Table& t) {
                    for (auto& k : kids) if (!k(t)) return false;
                    return true;
                }};
            }
            if (node.kind == Kind::Or) {
                std::vector<Pred> kids;
                for (auto& cn : node.children) {
                    auto r = compile(*cn);
                    if (!r) return r.error();
                    kids.push_back(std::move(r).value());
                }
                return Pred{[kids = std::move(kids)](openads::engine::Table& t) {
                    for (auto& k : kids) if (k(t)) return true;
                    return false;
                }};
            }
            if (node.kind == Kind::Not) {
                auto inner = compile(*node.child);
                if (!inner) return inner.error();
                return Pred{[p = std::move(inner).value()]
                            (openads::engine::Table& t) { return !p(t); }};
            }
            if (node.kind == Kind::Exists) {
                // M10.17: uncorrelated EXISTS — open the subquery's
                // table, walk all live rows, return constant true if
                // at least one row exists. Subquery's optional WHERE
                // is intentionally NOT compiled here yet (apps can
                // use IN for filtered membership in the meantime).
                if (!node.exists_subquery) {
                    return openads::util::Error{
                        openads::AE_PARSE_ERROR, 0,
                        "EXISTS subquery missing", ""};
                }
                const auto& sq = *node.exists_subquery;
                auto sh = c->open_table(sq.table,
                                        openads::engine::TableType::Cdx,
                                        openads::engine::OpenMode::Read);
                if (!sh) return sh.error();
                openads::engine::Table* stbl = c->lookup_table(sh.value());
                if (stbl == nullptr) {
                    return openads::util::Error{
                        openads::AE_INTERNAL_ERROR, 0,
                        "EXISTS post-open", ""};
                }
                bool any = false;
                std::uint32_t srcount = stbl->record_count();
                for (std::uint32_t r = 1; r <= srcount; ++r) {
                    if (auto g = stbl->goto_record(r); !g) continue;
                    if (stbl->is_deleted()) continue;
                    any = true;
                    break;
                }
                c->close_table(sh.value());
                return Pred{[any](openads::engine::Table&) { return any; }};
            }
            if (node.kind == Kind::In) {
                // M10.15: materialise the IN set at compile time. For
                // a literal list, just lift the strings in. For a
                // subquery, walk its source table inline (no nested
                // ABI dispatch — keeps the lock_guard intact).
                std::int32_t fidx = tbl->field_index(node.in_clause.column);
                if (fidx < 0) {
                    return openads::util::Error{
                        openads::AE_COLUMN_NOT_FOUND, 0,
                        node.in_clause.column.c_str(), ""};
                }
                std::uint16_t fi = static_cast<std::uint16_t>(fidx);
                auto trim_trailing = [](std::string s) {
                    while (!s.empty() && s.back() == ' ') s.pop_back();
                    return s;
                };
                auto set = std::make_shared<std::unordered_set<std::string>>();
                for (auto& lit : node.in_clause.literals) set->insert(lit);
                if (node.in_clause.subquery) {
                    const auto& sq = *node.in_clause.subquery;
                    auto sh = c->open_table(sq.table,
                                            openads::engine::TableType::Cdx,
                                            openads::engine::OpenMode::Read);
                    if (!sh) return sh.error();
                    openads::engine::Table* stbl = c->lookup_table(sh.value());
                    if (stbl == nullptr) {
                        return openads::util::Error{
                            openads::AE_INTERNAL_ERROR, 0,
                            "subquery post-open", ""};
                    }
                    if (sq.projection.empty() && sq.aggregates.empty()) {
                        return openads::util::Error{
                            openads::AE_PARSE_ERROR, 0,
                            "IN subquery must project a single column", ""};
                    }
                    if (!sq.aggregates.empty() ||
                        sq.projection.size() != 1) {
                        c->close_table(sh.value());
                        return openads::util::Error{
                            openads::AE_PARSE_ERROR, 0,
                            "IN subquery must project exactly one column", ""};
                    }
                    std::int32_t scol = stbl->field_index(sq.projection[0]);
                    if (scol < 0) {
                        c->close_table(sh.value());
                        return openads::util::Error{
                            openads::AE_COLUMN_NOT_FOUND, 0,
                            sq.projection[0].c_str(), ""};
                    }
                    std::uint32_t srcount = stbl->record_count();
                    for (std::uint32_t r = 1; r <= srcount; ++r) {
                        if (auto g = stbl->goto_record(r); !g) continue;
                        if (stbl->is_deleted()) continue;
                        // No nested WHERE filter for now — apps that
                        // need it can wrap with an outer SELECT in
                        // a future milestone.
                        auto v = stbl->read_field(
                            static_cast<std::uint16_t>(scol));
                        if (!v) continue;
                        set->insert(trim_trailing(v.value().as_string));
                    }
                    c->close_table(sh.value());
                }
                return Pred{[fi, set, trim_trailing]
                            (openads::engine::Table& t) {
                    auto v = t.read_field(fi);
                    if (!v) return false;
                    return set->find(trim_trailing(v.value().as_string)) !=
                           set->end();
                }};
            }
            // Cmp leaf.
            const auto& w = node.cmp;
            std::int32_t fidx = tbl->field_index(w.column);
            if (fidx < 0) {
                return openads::util::Error{
                    openads::AE_COLUMN_NOT_FOUND, 0,
                    w.column.c_str(), ""};
            }
            CmpTerm term;
            term.field_index = static_cast<std::uint16_t>(fidx);
            term.op          = w.op;
            term.literal     = w.literal;
            term.is_numeric  = w.is_numeric;
            term.number      = w.number;
            if (w.subquery) {
                // M10.18: scalar subquery — materialise once at
                // compile time. Open the subquery's table, walk for
                // the first non-deleted record, read the projection's
                // first column, and use that as the cmp literal.
                const auto& sq = *w.subquery;
                auto sh = c->open_table(sq.table,
                                        openads::engine::TableType::Cdx,
                                        openads::engine::OpenMode::Read);
                if (!sh) return sh.error();
                openads::engine::Table* stbl = c->lookup_table(sh.value());
                if (stbl == nullptr) {
                    return openads::util::Error{
                        openads::AE_INTERNAL_ERROR, 0,
                        "scalar subquery post-open", ""};
                }
                bool outer_is_numeric =
                    tbl->field_descriptor(static_cast<std::uint16_t>(fidx))
                        .type != openads::drivers::DbfFieldType::Character;

                // M10.19 — aggregate scalar subquery
                // (`= (SELECT MAX(x) FROM t)`). Single aggregate slot
                // computes against the inner table; numeric result
                // lands directly in the cmp's number/literal.
                if (sq.aggregates.size() == 1 && sq.projection.empty()) {
                    const auto& a = sq.aggregates[0];
                    std::int32_t scol = -1;
                    if (a.kind != openads::sql::AggregateKind::CountStar) {
                        scol = stbl->field_index(a.column);
                        if (scol < 0) {
                            c->close_table(sh.value());
                            return openads::util::Error{
                                openads::AE_COLUMN_NOT_FOUND, 0,
                                a.column.c_str(), ""};
                        }
                    }
                    std::uint64_t cnt = 0;
                    double sum = 0.0;
                    double minv =  std::numeric_limits<double>::infinity();
                    double maxv = -std::numeric_limits<double>::infinity();
                    std::uint32_t srcount = stbl->record_count();
                    for (std::uint32_t r = 1; r <= srcount; ++r) {
                        if (auto g = stbl->goto_record(r); !g) continue;
                        if (stbl->is_deleted()) continue;
                        if (a.kind == openads::sql::AggregateKind::CountStar) {
                            ++cnt;
                            continue;
                        }
                        auto v = stbl->read_field(
                            static_cast<std::uint16_t>(scol));
                        if (!v) continue;
                        ++cnt;
                        double d = v.value().as_double;
                        sum += d;
                        if (d < minv) minv = d;
                        if (d > maxv) maxv = d;
                    }
                    c->close_table(sh.value());
                    double result = 0.0;
                    switch (a.kind) {
                        case openads::sql::AggregateKind::CountStar:
                        case openads::sql::AggregateKind::Count:
                            result = static_cast<double>(cnt);
                            break;
                        case openads::sql::AggregateKind::Sum: result = sum; break;
                        case openads::sql::AggregateKind::Avg:
                            result = cnt ? sum / static_cast<double>(cnt) : 0.0;
                            break;
                        case openads::sql::AggregateKind::Min:
                            result = cnt ? minv : 0.0; break;
                        case openads::sql::AggregateKind::Max:
                            result = cnt ? maxv : 0.0; break;
                    }
                    term.is_numeric = outer_is_numeric;
                    term.number     = result;
                    char tmp[64];
                    std::snprintf(tmp, sizeof(tmp), "%.17g", result);
                    term.literal = tmp;
                    if (!outer_is_numeric) {
                        // String comparison: drop trailing zeros so
                        // "42.000" compares clean against the column.
                        std::snprintf(tmp, sizeof(tmp), "%g", result);
                        term.literal = tmp;
                    }
                } else if (!sq.projection.empty() && sq.aggregates.empty()) {
                    if (sq.projection.size() != 1) {
                        c->close_table(sh.value());
                        return openads::util::Error{
                            openads::AE_PARSE_ERROR, 0,
                            "scalar subquery must project a single column", ""};
                    }
                    std::int32_t scol = stbl->field_index(sq.projection[0]);
                    if (scol < 0) {
                        c->close_table(sh.value());
                        return openads::util::Error{
                            openads::AE_COLUMN_NOT_FOUND, 0,
                            sq.projection[0].c_str(), ""};
                    }
                    bool found = false;
                    std::uint32_t srcount = stbl->record_count();
                    for (std::uint32_t r = 1; r <= srcount; ++r) {
                        if (auto g = stbl->goto_record(r); !g) continue;
                        if (stbl->is_deleted()) continue;
                        auto v = stbl->read_field(
                            static_cast<std::uint16_t>(scol));
                        if (!v) continue;
                        term.literal    = v.value().as_string;
                        term.is_numeric = outer_is_numeric;
                        term.number     = v.value().as_double;
                        while (!term.literal.empty() &&
                               term.literal.back() == ' ') {
                            term.literal.pop_back();
                        }
                        found = true;
                        break;
                    }
                    c->close_table(sh.value());
                    if (!found) {
                        return Pred{[](openads::engine::Table&) { return false; }};
                    }
                } else {
                    c->close_table(sh.value());
                    return openads::util::Error{
                        openads::AE_PARSE_ERROR, 0,
                        "scalar subquery must project exactly one "
                        "column or one aggregate", ""};
                }
            }
            if (w.op == openads::sql::WhereOp::Contains) {
                namespace fs = std::filesystem;
                fs::path fts_path =
                    fs::path(tbl->path()).replace_extension(".fts");
                auto loaded = openads::engine::Fts::load(fts_path.string());
                if (!loaded) return loaded.error();
                openads::engine::FtsOptions opts;
                auto hits = openads::engine::Fts::search(
                    loaded.value(), w.literal, opts);
                term.contains_hits =
                    std::make_shared<std::unordered_set<std::uint32_t>>(
                        hits.begin(), hits.end());
            }
            return Pred{[term](openads::engine::Table& t) {
                if (term.op == openads::sql::WhereOp::Contains) {
                    if (!term.contains_hits) return false;
                    return term.contains_hits->find(t.recno()) !=
                           term.contains_hits->end();
                }
                auto v = t.read_field(term.field_index);
                if (!v) return false;
                int cmp = 0;
                if (term.is_numeric) {
                    double d = v.value().as_double;
                    if      (d < term.number) cmp = -1;
                    else if (d > term.number) cmp =  1;
                } else {
                    cmp = v.value().as_string.compare(term.literal);
                }
                switch (term.op) {
                    case openads::sql::WhereOp::Eq: return cmp == 0;
                    case openads::sql::WhereOp::Ne: return cmp != 0;
                    case openads::sql::WhereOp::Lt: return cmp <  0;
                    case openads::sql::WhereOp::Gt: return cmp >  0;
                    case openads::sql::WhereOp::Le: return cmp <= 0;
                    case openads::sql::WhereOp::Ge: return cmp >= 0;
                    case openads::sql::WhereOp::Contains: return true;
                }
                return false;
            }};
        };

        auto compiled = compile(*parsed.value().where);
        if (!compiled) return fail(compiled.error());
        tbl->set_filter(std::move(compiled).value());
    }

    // M10.6: ORDER BY <col> [ASC|DESC]. Materialize matching recnos
    // through the WHERE filter (or every live row when none), sort
    // them by the column's value, and install the sequence as the
    // cursor's traversal order.
    if (parsed.value().order_by) {
        const auto& ob = *parsed.value().order_by;
        std::int32_t fidx = tbl->field_index(ob.column);
        if (fidx < 0) {
            return fail(openads::AE_COLUMN_NOT_FOUND, ob.column.c_str());
        }
        std::uint16_t key = static_cast<std::uint16_t>(fidx);

        std::vector<std::uint32_t> matched;
        std::uint32_t rcount = tbl->record_count();
        for (std::uint32_t r = 1; r <= rcount; ++r) {
            if (auto g = tbl->goto_record(r); !g) continue;
            if (tbl->is_deleted()) continue;
            if (!tbl->passes_filter()) continue;
            matched.push_back(r);
        }

        // Sort by the field's typed value. The column type drives
        // numeric vs string compare so VFP I / Y / B sort properly.
        const auto& fdesc = tbl->field_descriptor(key);
        bool numeric_sort =
            fdesc.type == openads::drivers::DbfFieldType::Numeric ||
            fdesc.type == openads::drivers::DbfFieldType::Float   ||
            fdesc.type == openads::drivers::DbfFieldType::Integer ||
            fdesc.type == openads::drivers::DbfFieldType::Currency||
            fdesc.type == openads::drivers::DbfFieldType::Double;

        struct KV {
            std::uint32_t recno;
            std::string   s;
            double        d;
        };
        std::vector<KV> rows;
        rows.reserve(matched.size());
        for (auto r : matched) {
            (void)tbl->goto_record(r);
            auto v = tbl->read_field(key);
            KV kv;
            kv.recno = r;
            if (v) {
                kv.s = v.value().as_string;
                kv.d = v.value().as_double;
            }
            rows.push_back(std::move(kv));
        }
        std::stable_sort(rows.begin(), rows.end(),
            [&](const KV& a, const KV& b) {
                bool less = numeric_sort
                    ? (a.d < b.d)
                    : (a.s <  b.s);
                bool equal = numeric_sort
                    ? (a.d == b.d)
                    : (a.s == b.s);
                if (equal) return false;
                return ob.descending ? !less : less;
            });
        std::vector<std::uint32_t> seq;
        seq.reserve(rows.size());
        for (auto& kv : rows) seq.push_back(kv.recno);
        // ORDER BY supersedes the row filter — the materialised list
        // already excludes WHERE-rejected rows.
        tbl->clear_filter();
        tbl->set_recno_sequence(std::move(seq));
    }

    ADSHANDLE gh = s.registry.register_object(HandleKind::Table, tbl);

    // M10.8: stash the projection (column → underlying field index)
    // for this cursor handle. Empty projection means SELECT * — leave
    // the entry absent so AdsGetNumFields / Name / Type fall through
    // to the table's full schema.
    if (!parsed.value().projection.empty()) {
        std::vector<std::uint16_t> proj;
        proj.reserve(parsed.value().projection.size());
        for (const auto& col : parsed.value().projection) {
            std::int32_t fidx = tbl->field_index(col);
            if (fidx < 0) {
                return fail(openads::AE_COLUMN_NOT_FOUND, col.c_str());
            }
            proj.push_back(static_cast<std::uint16_t>(fidx));
        }
        cursor_projections()[gh] = std::move(proj);
    }

    *phCursor = gh;
    return ok();
}

} // extern "C"
