#include "openads/ace.h"
#include "openads/error.h"

#include "abi/charset.h"
#include "abi/last_error.h"

#include "engine/codepage.h"
#include "engine/fts.h"
#include "engine/index_expr.h"
#include "engine/table.h"

#include "network/client.h"
#include "session/connection.h"
#include "session/handle_registry.h"
#include "drivers/dbf_common.h"
#include "drivers/index_trait.h"
#include "drivers/ntx/ntx_index.h"
#include "drivers/cdx/cdx_driver.h"
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
    // M10.36 — recursive_mutex so UNION dispatch can re-enter
    // AdsExecuteSQLDirect (used to materialise each member's cursor)
    // while still holding the outer lock.
    std::recursive_mutex                                          mu;
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
        case DbfFieldType::Varchar:   return ADS_STRING;        // M11.1
        case DbfFieldType::Varbinary: return ADS_RAW;           // M11.1
        case DbfFieldType::Unknown:   return ADS_FIELD_TYPE_UNKNOWN;
    }
    return ADS_FIELD_TYPE_UNKNOWN;
}

[[maybe_unused]] const openads::drivers::DbfField*
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

// M12.5 — remote-table lookup helper. Returns nullptr when the
// handle isn't a TCP-routed table.
openads::network::RemoteTable* get_remote_table(ADSHANDLE h) {
    auto& s = state();
    return s.registry.lookup<openads::network::RemoteTable>(
        h, HandleKind::RemoteTable);
}

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
    // M12.5 — `tcp://host:port/<data_dir>` routes the connection
    // through the wire client; every Ads* function that recognises
    // the connection handle's RemoteConnection kind dispatches to
    // the server instead of touching a local Connection.
    {
        std::string host, dir;
        std::uint16_t port = 0;
        if (openads::network::parse_tcp_uri(path, host, port, dir)) {
            auto rc = std::make_unique<openads::network::RemoteConnection>();
            if (auto r = rc->connect(host, port, dir); !r) {
                return fail(r.error());
            }
            auto& s = state();
            std::lock_guard<std::recursive_mutex> lk(s.mu);
            Handle h = s.registry.register_object(
                HandleKind::RemoteConnection, rc.get());
            // Keep RemoteConnection alive in a side container.
            static std::unordered_map<Handle,
                std::unique_ptr<openads::network::RemoteConnection>>
                remote_conns;
            remote_conns.emplace(h, std::move(rc));
            *phConnect = h;
            return ok();
        }
    }
    auto opened = Connection::open(path);
    if (!opened) return fail(opened.error());
    auto holder = std::make_unique<Connection>(std::move(opened).value());
    Connection* raw = holder.get();
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Handle h = s.registry.register_object(HandleKind::Connection, raw);
    s.conns.emplace(h, std::move(holder));
    *phConnect = h;
    return ok();
}

UNSIGNED32 AdsDisconnect(ADSHANDLE hConnect) {
    {
        auto& s_local = state();
        std::lock_guard<std::recursive_mutex> lk_local(s_local.mu);
        if (auto* rc = s_local.registry.lookup<openads::network::RemoteConnection>(
                hConnect, HandleKind::RemoteConnection)) {
            rc->disconnect();
            return ok();
        }
    }
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
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
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    // M12.5 — remote connection handle: route through wire client.
    if (auto* rc = s.registry.lookup<openads::network::RemoteConnection>(
            hConnect, HandleKind::RemoteConnection)) {
        auto name = openads::abi::to_internal(pucName, 0);
        auto id = rc->open_table(name);
        if (!id) return fail(id.error());
        static std::unordered_map<Handle,
            std::unique_ptr<openads::network::RemoteTable>> remote_tables;
        auto rt = std::make_unique<openads::network::RemoteTable>();
        rt->conn = rc;
        rt->id   = id.value();
        Handle gh = s.registry.register_object(
            HandleKind::RemoteTable, rt.get());
        remote_tables.emplace(gh, std::move(rt));
        *phTable = gh;
        return ok();
    }
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

// M10.33 — standard SQL LIKE pattern. `%` matches any sequence
// (including empty), `_` matches a single character. Greedy match
// with backtracking — adequate for short DBF cells.
static inline bool sql_like_match(const std::string& s,
                                  const std::string& pat) {
    std::size_t si = 0, pi = 0;
    std::size_t star = std::string::npos, ss = 0;
    while (si < s.size()) {
        if (pi < pat.size() &&
            (pat[pi] == '_' || pat[pi] == s[si])) {
            ++si; ++pi;
        } else if (pi < pat.size() && pat[pi] == '%') {
            star = pi++;
            ss   = si;
        } else if (star != std::string::npos) {
            pi = star + 1;
            si = ++ss;
        } else {
            return false;
        }
    }
    while (pi < pat.size() && pat[pi] == '%') ++pi;
    return pi == pat.size();
}

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
    std::lock_guard<std::recursive_mutex> lk(s.mu);
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
    {
        if (auto* rt = get_remote_table(hTable)) {
            (void)rt->conn->close_table(rt->id);
            return ok();
        }
    }
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
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
    if (auto* rt = get_remote_table(hTable)) {
        auto r = rt->conn->goto_top(rt->id);
        if (!r) return fail(r.error());
        return ok();
    }
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
    if (auto* rt = get_remote_table(hTable)) {
        auto r = rt->conn->skip(rt->id, lRows);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->skip(lRows);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsAtEOF(ADSHANDLE hTable, UNSIGNED16* pbAtEnd) {
    if (auto* rt = get_remote_table(hTable)) {
        if (pbAtEnd == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
        auto r = rt->conn->at_eof(rt->id);
        if (!r) return fail(r.error());
        *pbAtEnd = r.value() ? 1 : 0;
        return ok();
    }
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
    if (auto* rt = get_remote_table(hTable)) {
        if (pulRecordCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
        auto r = rt->conn->record_count(rt->id);
        if (!r) return fail(r.error());
        *pulRecordCount = static_cast<UNSIGNED32>(r.value());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t || pulRecordCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    // M10.31 / M10.32 — when SQL has materialised a traversal sequence
    // (DISTINCT / LIMIT / OFFSET / ORDER BY), report that sequence's
    // length so apps that drive walking by record-count get the
    // post-clause row count.
    if (t->has_recno_sequence()) {
        *pulRecordCount = static_cast<UNSIGNED32>(t->recno_sequence().size());
    } else if (t->has_filter()) {
        // M10.33 — WHERE-filtered cursor without an installed
        // sequence (no ORDER BY / DISTINCT / LIMIT). Count
        // matching live rows on demand so BETWEEN / LIKE / regular
        // predicates surface their cardinality through GetRecordCount.
        std::uint32_t rc = t->record_count();
        std::uint32_t pass = 0;
        for (std::uint32_t r = 1; r <= rc; ++r) {
            if (auto g = t->goto_record(r); !g) continue;
            if (t->is_deleted()) continue;
            if (!t->passes_filter()) continue;
            ++pass;
        }
        *pulRecordCount = pass;
    } else {
        *pulRecordCount = t->record_count();
    }
    return ok();
}

UNSIGNED32 AdsGetField(ADSHANDLE hTable, UNSIGNED8* pucField,
                       UNSIGNED8* pucBuf, UNSIGNED32* pulLen,
                       UNSIGNED16 /*usOption*/) {
    if (auto* rt = get_remote_table(hTable)) {
        if (pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
        auto fname = openads::abi::to_internal(pucField, 0);
        auto r = rt->conn->get_field(rt->id, fname);
        if (!r) return fail(r.error());
        UNSIGNED16 cap = static_cast<UNSIGNED16>(
            *pulLen > 0xFFFFu ? 0xFFFFu : *pulLen);
        UNSIGNED16 cap_inout = cap;
        openads::abi::copy_to_caller(pucBuf, &cap_inout, r.value());
        *pulLen = cap_inout;
        return ok();
    }
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

// extern "C++" silences clang's `-Wreturn-type-c-linkage` warning
// (returning an anonymous-namespace type from inside the surrounding
// extern "C" block isn't ABI-meaningful, but is harmless here since
// `lock_policy` is only called from C++ code in this TU).
extern "C++" LockPolicy& lock_policy() {
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
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    lock_policy().cycle_ms = ulCycle;
    return ok();
}

UNSIGNED32 AdsGetLockCycle(ADSHANDLE /*hConnect*/, UNSIGNED32* pulCycle) {
    if (pulCycle == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    *pulCycle = lock_policy().cycle_ms;
    return ok();
}

UNSIGNED32 AdsSetLockRetryCount(ADSHANDLE /*hConnect*/, UNSIGNED16 usRetryCount) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    lock_policy().retry_count = usRetryCount;
    return ok();
}

UNSIGNED32 AdsGetLockRetryCount(ADSHANDLE /*hConnect*/, UNSIGNED16* pusRetryCount) {
    if (pusRetryCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
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
    std::lock_guard<std::recursive_mutex> lk(s.mu);
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
    std::lock_guard<std::recursive_mutex> lk(s.mu);
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
    std::lock_guard<std::recursive_mutex> lk(s.mu);
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
    std::lock_guard<std::recursive_mutex> lk(s.mu);
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

extern "C++"
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

// M11.2 — convert a plain CDX table to OpenADS-encrypted in place.
// Requires AdsSetEncryptionPassword to have been called on the
// owning connection (located by walking the registry for the
// connection whose tables include this Table*).
UNSIGNED32 AdsEncryptTable(ADSHANDLE hTable) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Table* t = s.registry.lookup<Table>(hTable, HandleKind::Table);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "invalid table handle");
    Connection* owning = nullptr;
    s.registry.for_each_handle([&](Handle, HandleKind k, void* p) {
        if (k != HandleKind::Connection || owning) return;
        auto* cc = static_cast<Connection*>(p);
        if (cc->owns_table_ptr(t)) owning = cc;
    });
    if (!owning) return fail(openads::AE_INVALID_CONNECTION_HANDLE,
                             "table not owned by any connection");
    if (!owning->has_encryption_key()) {
        return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                    "AdsSetEncryptionPassword required first");
    }
    auto* cdx = dynamic_cast<openads::drivers::cdx::CdxDriver*>(t->driver());
    if (!cdx) {
        return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                    "encryption supported on CdxDriver tables only");
    }
    auto r = cdx->encrypt_in_place(owning->encryption_key());
    if (!r) return fail(r.error());
    if (auto fl = t->flush(); !fl) return fail(fl.error());
    return ok();
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
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = c->begin_tx();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsCommitTransaction(ADSHANDLE hConnect) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = c->commit_tx();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsRollbackTransaction(ADSHANDLE hConnect) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = c->rollback_tx();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsInTransaction(ADSHANDLE hConnect, UNSIGNED16* pbInTx) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c || pbInTx == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    *pbInTx = c->in_tx() ? 1 : 0;
    return ok();
}

// M11.2 — set the encryption password on a connection. Affects
// every subsequent table open: encrypted tables (header byte 0xC3)
// transparently decrypt on read / encrypt on write using AES-256-CTR
// keyed off the (zero-padded) password bytes. OpenADS-only format —
// not byte-compatible with SAP ADS encrypted .adt files.
// M11.8 — OEM (CP437) ↔ ANSI (UTF-8 in this build) conversion
// helpers. `pucBuf` is read until a NUL byte. Output is written
// in place into the same buffer (caller must size for worst case
// — UTF-8 may grow up to 3x); `pulLen` carries the input length
// in and the output length out.
UNSIGNED32 AdsConvertOemToAnsi(UNSIGNED8* pucBuf, UNSIGNED32* pulLen) {
    if (pucBuf == nullptr || pulLen == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    auto utf8 = openads::engine::cp437_to_utf8(
        pucBuf, static_cast<std::size_t>(*pulLen));
    std::size_t out_len = utf8.size();
    std::memcpy(pucBuf, utf8.data(), out_len);
    if (out_len < *pulLen) pucBuf[out_len] = '\0';
    *pulLen = static_cast<UNSIGNED32>(out_len);
    return ok();
}

UNSIGNED32 AdsConvertAnsiToOem(UNSIGNED8* pucBuf, UNSIGNED32* pulLen) {
    if (pucBuf == nullptr || pulLen == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    auto cp = openads::engine::utf8_to_cp437(
        reinterpret_cast<const char*>(pucBuf),
        static_cast<std::size_t>(*pulLen));
    std::size_t out_len = cp.size();
    std::memcpy(pucBuf, cp.data(), out_len);
    if (out_len < *pulLen) pucBuf[out_len] = '\0';
    *pulLen = static_cast<UNSIGNED32>(out_len);
    return ok();
}

// M11.7 — set the connection's string-compare collation. Names:
// `binary` (default) or `nocase`. Affects equality / range
// comparisons for Character columns in SQL WHERE.
UNSIGNED32 AdsSetCollation(ADSHANDLE hConnect, UNSIGNED8* pucName) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(
        hConnect, HandleKind::Connection);
    if (!c || pucName == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    auto name = openads::abi::to_internal(pucName, 0);
    std::string upper;
    upper.reserve(name.size());
    for (char ch : name) upper.push_back(static_cast<char>(
        std::toupper(static_cast<unsigned char>(ch))));
    if (upper == "BINARY") {
        c->set_collation(Connection::Collation::Binary);
    } else if (upper == "NOCASE") {
        c->set_collation(Connection::Collation::NoCase);
    } else {
        return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                    "unknown collation name (expected BINARY / NOCASE)");
    }
    return ok();
}

UNSIGNED32 AdsSetEncryptionPassword(ADSHANDLE hConnect,
                                    UNSIGNED8* pucPassword) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(
        hConnect, HandleKind::Connection);
    if (!c || pucPassword == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    auto pw = openads::abi::to_internal(pucPassword, 0);
    c->set_encryption_password(pw);
    return ok();
}

UNSIGNED32 AdsCreateSavepoint(ADSHANDLE hConnect, UNSIGNED8* pucName) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c || pucName == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    auto name = openads::abi::to_internal(pucName, 0);
    auto r = c->create_savepoint(name);
    if (!r) return fail(r.error());
    return ok();
}

// M11.3 — release a savepoint without rolling back. The work done
// since CreateSavepoint stays part of the enclosing transaction.
UNSIGNED32 AdsReleaseSavepoint(ADSHANDLE hConnect, UNSIGNED8* pucName) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c || pucName == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    auto name = openads::abi::to_internal(pucName, 0);
    auto r = c->release_savepoint(name);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsRollbackTransaction80(ADSHANDLE hConnect, UNSIGNED8* pucSavepoint) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
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
    std::lock_guard<std::recursive_mutex> lk(s.mu);
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
    std::lock_guard<std::recursive_mutex> lk(s.mu);
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
    std::lock_guard<std::recursive_mutex> lk(s.mu);
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
    std::lock_guard<std::recursive_mutex> lk(s.mu);
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
    std::lock_guard<std::recursive_mutex> lk(s.mu);
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
    std::lock_guard<std::recursive_mutex> lk(s.mu);
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
    std::lock_guard<std::recursive_mutex> lk(s.mu);
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

        // M10.42 — CREATE TABLE t AS SELECT ...: recursively run the
        // inner SELECT, build the new table's schema from the result
        // cursor's projected fields, then walk + insert each row.
        if (!ct.value().select_sql.empty()) {
            std::vector<UNSIGNED8> selbuf(ct.value().select_sql.size() + 1);
            std::memcpy(selbuf.data(),
                        ct.value().select_sql.c_str(),
                        ct.value().select_sql.size() + 1);
            ADSHANDLE srcCur = 0;
            UNSIGNED32 rrc =
                AdsExecuteSQLDirect(hStatement, selbuf.data(), &srcCur);
            if (rrc != 0) return rrc;
            std::lock_guard<std::recursive_mutex> lk2(s.mu);
            openads::engine::Table* src =
                s.registry.lookup<openads::engine::Table>(
                    srcCur, HandleKind::Table);
            if (!src) {
                return fail(openads::AE_INTERNAL_ERROR,
                            "CTAS inner cursor lookup");
            }
            // Build schema from inner cursor (projection-aware).
            const auto* proj = projection_for(srcCur);
            std::vector<openads::drivers::DbfField> schema;
            if (proj) {
                for (auto idx : *proj) schema.push_back(src->field_descriptor(idx));
            } else {
                std::uint16_t nf = src->field_count();
                for (std::uint16_t k = 0; k < nf; ++k) {
                    schema.push_back(src->field_descriptor(k));
                }
            }
            // Build NAME,Type,Len,Dec;… from schema.
            auto type_name = [](char raw) -> const char* {
                switch (raw) {
                    case 'C': return "Character";
                    case 'N': return "Numeric";
                    case 'D': return "Date";
                    case 'L': return "Logical";
                    case 'M': return "Memo";
                    case 'F': return "Float";
                    case 'I': return "Integer";
                    case 'Y': return "Currency";
                    case 'B': return "Double";
                    case 'V': return "Varchar";
                    case 'Q': return "Varbinary";
                }
                return "Character";
            };
            std::string defs;
            for (auto& fd : schema) {
                if (!defs.empty()) defs.push_back(';');
                defs += fd.name;
                defs.push_back(',');
                defs += type_name(static_cast<char>(fd.raw_type));
                if (fd.length > 0) {
                    defs.push_back(',');
                    defs += std::to_string(fd.length);
                }
                if (fd.decimals > 0) {
                    defs.push_back(',');
                    defs += std::to_string(fd.decimals);
                }
            }
            std::vector<UNSIGNED8> name_buf(ct.value().table.size() + 1, 0);
            std::memcpy(name_buf.data(), ct.value().table.data(),
                        ct.value().table.size());
            std::vector<UNSIGNED8> def_buf(defs.size() + 1, 0);
            std::memcpy(def_buf.data(), defs.data(), defs.size());
            ADSHANDLE conn_h = 0;
            s.registry.for_each_handle([&](Handle h, HandleKind k, void* p) {
                if (k != HandleKind::Connection) return;
                if (static_cast<Connection*>(p) == c) conn_h = h;
            });
            if (conn_h == 0) {
                AdsCloseTable(srcCur);
                return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
            }
            ADSHANDLE hTable = 0;
            UNSIGNED32 rc = AdsCreateTable(conn_h, name_buf.data(), nullptr,
                                           ADS_CDX, 0, 0, 0, 0,
                                           def_buf.data(), &hTable);
            if (rc != openads::AE_SUCCESS) {
                AdsCloseTable(srcCur);
                return rc;
            }
            openads::engine::Table* tgt =
                s.registry.lookup<openads::engine::Table>(
                    hTable, HandleKind::Table);
            if (!tgt) {
                AdsCloseTable(srcCur);
                AdsCloseTable(hTable);
                return fail(openads::AE_INTERNAL_ERROR, "CTAS post-create");
            }
            // Walk source rows.
            std::vector<std::uint32_t> recnos;
            if (src->has_recno_sequence()) {
                recnos = src->recno_sequence();
            } else {
                std::uint32_t rcount = src->record_count();
                for (std::uint32_t r = 1; r <= rcount; ++r) {
                    if (auto g = src->goto_record(r); !g) continue;
                    if (src->is_deleted()) continue;
                    if (!src->passes_filter()) continue;
                    recnos.push_back(r);
                }
            }
            // Pre-resolve src column → tgt column by name match.
            std::vector<std::uint16_t> src_cols(schema.size());
            std::vector<std::uint16_t> tgt_cols(schema.size());
            for (std::size_t i = 0; i < schema.size(); ++i) {
                src_cols[i] = proj ? (*proj)[i] : static_cast<std::uint16_t>(i);
                std::int32_t fi = tgt->field_index(schema[i].name);
                if (fi < 0) {
                    AdsCloseTable(srcCur);
                    AdsCloseTable(hTable);
                    return fail(openads::AE_INTERNAL_ERROR,
                                "CTAS target field missing");
                }
                tgt_cols[i] = static_cast<std::uint16_t>(fi);
            }
            for (std::uint32_t r : recnos) {
                if (auto g = src->goto_record(r); !g) continue;
                if (auto ar = tgt->append_record(); !ar) {
                    AdsCloseTable(srcCur);
                    AdsCloseTable(hTable);
                    return fail(ar.error());
                }
                for (std::size_t i = 0; i < schema.size(); ++i) {
                    auto v = src->read_field(src_cols[i]);
                    std::string sv = v ? v.value().as_string : std::string();
                    auto wr = tgt->set_field(tgt_cols[i], sv);
                    if (!wr) {
                        AdsCloseTable(srcCur);
                        AdsCloseTable(hTable);
                        return fail(wr.error());
                    }
                }
            }
            (void)tgt->flush();
            AdsCloseTable(srcCur);
            AdsCloseTable(hTable);
            *phCursor = 0;
            return ok();
        }

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

    // M11.4 — `CREATE PROCEDURE <name> AS '<dll_path>::<symbol>'`.
    // Loads the DLL, resolves the symbol, registers the proc on the
    // connection. Returns no cursor.
    if (openads::sql::sql_is_create_procedure(sql)) {
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        auto cp = openads::sql::parse_create_procedure(sql);
        if (!cp) return fail(cp.error());
        auto rr = c->register_procedure(cp.value().name,
                                        cp.value().dll_path,
                                        cp.value().symbol);
        if (!rr) return fail(rr.error());
        *phCursor = 0;
        return ok();
    }

    // M11.4 — `EXECUTE PROCEDURE <name>(<arg>, ...)`. Packs args by
    // 0x1F separators, calls the proc's C entry point, materialises
    // the proc's result string in a 1-row temp DBF (column RESULT
    // C(255)) and returns it as the cursor.
    if (openads::sql::sql_is_execute_procedure(sql)) {
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        auto ep = openads::sql::parse_execute_procedure(sql);
        if (!ep) return fail(ep.error());
        std::string packed;
        for (std::size_t i = 0; i < ep.value().args.size(); ++i) {
            if (i != 0) packed.push_back('\x1f');
            packed.append(ep.value().args[i].text);
        }
        auto out = c->execute_procedure(ep.value().name, packed);
        if (!out) return fail(out.error());
        std::string& s_out = out.value();

        // Build a 1-row temp DBF with one C(255) column = "RESULT".
        namespace fs = std::filesystem;
        char nb[64];
        std::snprintf(nb, sizeof(nb), "_call_%llx.dbf",
                      static_cast<unsigned long long>(
                          openads::platform::monotonic_nanos()));
        fs::path dbf = fs::path(c->data_dir()) / nb;
        std::vector<std::uint8_t> file;
        std::array<std::uint8_t, 32> hdr{};
        hdr[0] = 0x03;
        hdr[4] = 1;
        std::uint16_t hl = 32 + 32 + 1;
        std::uint16_t rl = 1 + 255;
        hdr[8]  = static_cast<std::uint8_t>( hl       & 0xFFu);
        hdr[9]  = static_cast<std::uint8_t>((hl >> 8) & 0xFFu);
        hdr[10] = static_cast<std::uint8_t>( rl       & 0xFFu);
        hdr[11] = static_cast<std::uint8_t>((rl >> 8) & 0xFFu);
        file.insert(file.end(), hdr.begin(), hdr.end());
        std::array<std::uint8_t, 32> fd{};
        std::strncpy(reinterpret_cast<char*>(fd.data()), "RESULT", 11);
        fd[11] = 'C'; fd[16] = 255;
        file.insert(file.end(), fd.begin(), fd.end());
        file.push_back(0x0D);
        file.push_back(' ');
        for (std::size_t i = 0; i < 255; ++i) {
            file.push_back(i < s_out.size()
                ? static_cast<std::uint8_t>(s_out[i]) : ' ');
        }
        file.push_back(0x1A);
        {
            std::ofstream f(dbf, std::ios::binary);
            if (!f) return fail(openads::AE_INTERNAL_ERROR,
                "EXECUTE PROCEDURE temp DBF open failed");
            f.write(reinterpret_cast<const char*>(file.data()),
                    static_cast<std::streamsize>(file.size()));
        }
        std::string rel = dbf.filename().string();
        auto th = c->open_table(rel, openads::engine::TableType::Cdx,
                                openads::engine::OpenMode::Read);
        if (!th) return fail(th.error());
        openads::engine::Table* tbl = c->lookup_table(th.value());
        if (!tbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
        ADSHANDLE gh = s.registry.register_object(HandleKind::Table, tbl);
        *phCursor = gh;
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
                        default: return false;
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
                        default: return false;
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

        // M10.41 — INSERT INTO t (cols) SELECT ...: recursively
        // execute the inner SELECT, walk its cursor, append one
        // target row per source row mapping the inner cursor's
        // projected columns to `ins.columns` positionally.
        if (!ins.value().select_sql.empty()) {
            std::vector<UNSIGNED8> selbuf(ins.value().select_sql.size() + 1);
            std::memcpy(selbuf.data(),
                        ins.value().select_sql.c_str(),
                        ins.value().select_sql.size() + 1);
            ADSHANDLE srcCur = 0;
            UNSIGNED32 rrc =
                AdsExecuteSQLDirect(hStatement, selbuf.data(), &srcCur);
            if (rrc != 0) {
                c->close_table(th.value());
                return rrc;
            }
            auto& s2 = state();
            std::lock_guard<std::recursive_mutex> lk2(s2.mu);
            openads::engine::Table* src =
                s2.registry.lookup<openads::engine::Table>(
                    srcCur, HandleKind::Table);
            if (!src) {
                c->close_table(th.value());
                return fail(openads::AE_INTERNAL_ERROR,
                            "INSERT...SELECT inner cursor lookup");
            }
            // Resolve inner cursor's projected column indices.
            const auto* proj = projection_for(srcCur);
            std::vector<std::uint16_t> src_cols;
            if (proj) {
                src_cols = *proj;
            } else {
                std::uint16_t nf = src->field_count();
                src_cols.reserve(nf);
                for (std::uint16_t k = 0; k < nf; ++k) src_cols.push_back(k);
            }
            if (src_cols.size() != ins.value().columns.size()) {
                AdsCloseTable(srcCur);
                c->close_table(th.value());
                return fail(openads::AE_PARSE_ERROR,
                    "INSERT INTO ... SELECT: column count mismatch");
            }
            // Pre-resolve target column indices.
            std::vector<std::uint16_t> tgt_cols;
            tgt_cols.reserve(ins.value().columns.size());
            for (const auto& cn : ins.value().columns) {
                std::int32_t fi = tbl->field_index(cn);
                if (fi < 0) {
                    AdsCloseTable(srcCur);
                    c->close_table(th.value());
                    return fail(openads::AE_COLUMN_NOT_FOUND, cn.c_str());
                }
                tgt_cols.push_back(static_cast<std::uint16_t>(fi));
            }
            // Walk source rows.
            std::vector<std::uint32_t> recnos;
            if (src->has_recno_sequence()) {
                recnos = src->recno_sequence();
            } else {
                std::uint32_t rcount = src->record_count();
                for (std::uint32_t r = 1; r <= rcount; ++r) {
                    if (auto g = src->goto_record(r); !g) continue;
                    if (src->is_deleted()) continue;
                    if (!src->passes_filter()) continue;
                    recnos.push_back(r);
                }
            }
            for (std::uint32_t r : recnos) {
                if (auto g = src->goto_record(r); !g) continue;
                if (auto ar = tbl->append_record(); !ar) {
                    AdsCloseTable(srcCur);
                    c->close_table(th.value());
                    return fail(ar.error());
                }
                for (std::size_t i = 0; i < src_cols.size(); ++i) {
                    auto v = src->read_field(src_cols[i]);
                    std::string sv = v ? v.value().as_string : std::string();
                    auto wr = tbl->set_field(tgt_cols[i], sv);
                    if (!wr) {
                        AdsCloseTable(srcCur);
                        c->close_table(th.value());
                        return fail(wr.error());
                    }
                }
            }
            if (auto fl = tbl->flush(); !fl) {
                AdsCloseTable(srcCur);
                c->close_table(th.value());
                return fail(fl.error());
            }
            AdsCloseTable(srcCur);
            c->close_table(th.value());
            *phCursor = 0;
            return ok();
        }

        // M10.52 — multi-row VALUES path. When `rows` is non-empty,
        // append + populate one record per tuple; otherwise fall
        // back to the single-row `values` path.
        auto write_one = [&](const std::vector<openads::sql::InsertLiteral>&
                             vals) -> openads::util::Result<std::monostate>
        {
            if (auto r = tbl->append_record(); !r) return r.error();
            for (std::size_t i = 0; i < ins.value().columns.size(); ++i) {
                std::int32_t fidx =
                    tbl->field_index(ins.value().columns[i]);
                if (fidx < 0) {
                    return openads::util::Error{
                        openads::AE_COLUMN_NOT_FOUND, 0,
                        ins.value().columns[i].c_str(), ""};
                }
                const auto& v = vals[i];
                if (v.is_numeric) {
                    auto wr = tbl->set_field(
                        static_cast<std::uint16_t>(fidx), v.number);
                    if (!wr) return wr.error();
                } else {
                    auto wr = tbl->set_field(
                        static_cast<std::uint16_t>(fidx), v.text);
                    if (!wr) return wr.error();
                }
            }
            return std::monostate{};
        };
        if (!ins.value().rows.empty()) {
            for (auto& row : ins.value().rows) {
                auto r = write_one(row);
                if (!r) return fail(r.error());
            }
        } else {
            auto r = write_one(ins.value().values);
            if (!r) return fail(r.error());
        }
        if (auto fl = tbl->flush(); !fl) return fail(fl.error());
        c->close_table(th.value());
        *phCursor = 0;
        return ok();
    }

    // M10.26 — top-level `UNION [ALL]` between SELECTs. Each member
    // must currently be a `SELECT * FROM <t> [WHERE ...]` form (no
    // joins, aggregates, projection lists, GROUP BY, or ORDER BY
    // inside members — those compose with UNION in a follow-up).
    // First member's schema is reused for the merged cursor.
    {
        struct UnionPart { std::string sql_text; bool all = false; };
        std::vector<UnionPart> uparts;
        {
            std::size_t start = 0;
            int depth = 0;
            bool in_q = false;
            bool prev_all = false;
            auto kw_at = [&](std::size_t i, const char* kw) {
                std::size_t L = std::strlen(kw);
                if (i + L > sql.size()) return false;
                for (std::size_t k = 0; k < L; ++k)
                    if (std::toupper(static_cast<unsigned char>(sql[i+k])) !=
                        kw[k]) return false;
                bool lb = (i == 0) ||
                    (!std::isalnum(static_cast<unsigned char>(sql[i-1])) &&
                     sql[i-1] != '_');
                bool rb = (i + L == sql.size()) ||
                    (!std::isalnum(static_cast<unsigned char>(sql[i+L])) &&
                     sql[i+L] != '_');
                return lb && rb;
            };
            for (std::size_t i = 0; i < sql.size(); ) {
                char ch = sql[i];
                if (in_q) {
                    if (ch == '\'') in_q = false;
                    ++i; continue;
                }
                if (ch == '\'') { in_q = true; ++i; continue; }
                if (ch == '(')  { ++depth; ++i; continue; }
                if (ch == ')')  { --depth; ++i; continue; }
                if (depth == 0 && kw_at(i, "UNION")) {
                    uparts.push_back({sql.substr(start, i - start), prev_all});
                    i += 5;
                    while (i < sql.size() &&
                           std::isspace(static_cast<unsigned char>(sql[i]))) ++i;
                    prev_all = false;
                    if (kw_at(i, "ALL")) {
                        prev_all = true;
                        i += 3;
                        while (i < sql.size() &&
                               std::isspace(static_cast<unsigned char>(sql[i]))) ++i;
                    }
                    start = i;
                    continue;
                }
                ++i;
            }
            if (start < sql.size()) {
                uparts.push_back({sql.substr(start), prev_all});
            }
        }

        if (uparts.size() > 1) {
            auto& s = state();
            std::lock_guard<std::recursive_mutex> lk(s.mu);

            // M10.36 — every UNION member runs through the full
            // SELECT-execute pipeline as a recursive call to
            // AdsExecuteSQLDirect (allowed by the recursive_mutex on
            // s.mu). Members may now carry JOIN, GROUP BY, aggregates,
            // CASE WHEN, DISTINCT, LIMIT — anything a plain SELECT
            // accepts. The first member's cursor schema (whatever the
            // pipeline produces — temp DBF for joins/aggregates,
            // source schema for SELECT *) drives the merged schema;
            // later members align by column name against it.
            //
            // Last member's ORDER BY still becomes the merged sort
            // (M10.28 semantics). We capture it from a parse, then
            // let the recursive call run as-is — its sort is
            // overwritten by the final post-merge stable_sort below.
            std::optional<openads::sql::OrderBy> final_order;
            {
                auto p = openads::sql::parse_select(uparts.back().sql_text);
                if (p && p.value().order_by) {
                    final_order = *p.value().order_by;
                }
            }

            // The first member's `all` flag is meaningless (it has no
            // UNION keyword preceding it); only the join-flags between
            // members decide whether dedup applies.
            bool any_distinct = false;
            for (std::size_t i = 1; i < uparts.size(); ++i)
                if (!uparts[i].all) any_distinct = true;
            std::unordered_set<std::string> seen;

            std::vector<openads::drivers::DbfField> schema;
            std::uint32_t rec_len = 0;
            std::vector<std::vector<std::uint8_t>> rows;

            for (std::size_t mi = 0; mi < uparts.size(); ++mi) {
                // Recurse into the full SELECT executor for this
                // member. The member SQL goes through every dispatch
                // (UNION, JOIN, GROUP BY, aggregate, CASE, ...) and
                // lands as a registered cursor handle we can walk.
                std::vector<UNSIGNED8> mbuf(uparts[mi].sql_text.size() + 1);
                std::memcpy(mbuf.data(), uparts[mi].sql_text.c_str(),
                            uparts[mi].sql_text.size() + 1);
                ADSHANDLE memberCur = 0;
                UNSIGNED32 rrc =
                    AdsExecuteSQLDirect(hStatement, mbuf.data(), &memberCur);
                if (rrc != 0) return rrc;
                openads::engine::Table* mt =
                    s.registry.lookup<openads::engine::Table>(
                        memberCur, HandleKind::Table);
                if (!mt) {
                    return fail(openads::AE_INTERNAL_ERROR,
                                "UNION member cursor lookup failed");
                }

                if (mi == 0) {
                    auto* proj = projection_for(memberCur);
                    if (proj) {
                        schema.reserve(proj->size());
                        for (auto idx : *proj) {
                            schema.push_back(mt->field_descriptor(idx));
                        }
                    } else {
                        std::uint16_t nf = mt->field_count();
                        schema.reserve(nf);
                        for (std::uint16_t k = 0; k < nf; ++k) {
                            schema.push_back(mt->field_descriptor(k));
                        }
                    }
                    rec_len = 1;
                    for (auto& fd : schema) rec_len += fd.length;
                }

                std::vector<std::int32_t> col_src(schema.size(), -1);
                auto* proj_m = projection_for(memberCur);
                if (proj_m) {
                    if (proj_m->size() != schema.size()) {
                        AdsCloseTable(memberCur);
                        return fail(openads::AE_PARSE_ERROR,
                            "UNION member projection column count differs");
                    }
                    for (std::size_t i = 0; i < schema.size(); ++i) {
                        col_src[i] = static_cast<std::int32_t>((*proj_m)[i]);
                    }
                } else {
                    for (std::size_t i = 0; i < schema.size(); ++i) {
                        col_src[i] = mt->field_index(schema[i].name);
                    }
                }

                std::vector<std::uint32_t> recnos;
                if (mt->has_recno_sequence()) {
                    recnos = mt->recno_sequence();
                } else {
                    std::uint32_t rc = mt->record_count();
                    for (std::uint32_t r = 1; r <= rc; ++r) {
                        if (auto g = mt->goto_record(r); !g) continue;
                        if (mt->is_deleted()) continue;
                        if (!mt->passes_filter()) continue;
                        recnos.push_back(r);
                    }
                }
                for (std::uint32_t r : recnos) {
                    if (auto g = mt->goto_record(r); !g) continue;
                    std::vector<std::uint8_t> rec(rec_len);
                    rec[0] = ' ';
                    std::size_t off = 1;
                    for (std::size_t i = 0; i < schema.size(); ++i) {
                        std::string sval;
                        if (col_src[i] >= 0) {
                            auto v = mt->read_field(
                                static_cast<std::uint16_t>(col_src[i]));
                            if (v) sval = v.value().as_string;
                        }
                        std::uint8_t L = schema[i].length;
                        for (std::uint8_t k = 0; k < L; ++k) {
                            rec[off + k] = k < sval.size()
                                ? static_cast<std::uint8_t>(sval[k]) : ' ';
                        }
                        off += L;
                    }
                    if (any_distinct) {
                        std::string key(
                            reinterpret_cast<const char*>(rec.data()),
                            rec.size());
                        if (!seen.insert(std::move(key)).second) continue;
                    }
                    rows.push_back(std::move(rec));
                }
                AdsCloseTable(memberCur);
            }

            std::uint16_t nfields = static_cast<std::uint16_t>(schema.size());

            // Pre-build merged DBF header.
            std::vector<std::uint8_t> file;
            std::array<std::uint8_t, 32> hdr{};
            hdr[0] = 0x03;
            std::uint16_t header_len = static_cast<std::uint16_t>(
                32 + 32 * nfields + 1);
            hdr[8]  = static_cast<std::uint8_t>( header_len       & 0xFFu);
            hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
            hdr[10] = static_cast<std::uint8_t>( rec_len          & 0xFFu);
            hdr[11] = static_cast<std::uint8_t>((rec_len    >> 8) & 0xFFu);
            file.insert(file.end(), hdr.begin(), hdr.end());
            for (auto& fd : schema) {
                std::array<std::uint8_t, 32> bytes{};
                std::strncpy(reinterpret_cast<char*>(bytes.data()),
                             fd.name.c_str(), 11);
                bytes[11] = static_cast<std::uint8_t>(fd.raw_type);
                bytes[16] = fd.length;
                bytes[17] = fd.decimals;
                file.insert(file.end(), bytes.begin(), bytes.end());
            }
            file.push_back(0x0D);

            // M10.28 — apply ORDER BY (from last member) to merged rows.
            if (final_order) {
                std::int32_t fi = -1;
                std::uint16_t off = 1;
                std::uint16_t flen = 0;
                bool numeric = false;
                for (std::size_t i = 0; i < schema.size(); ++i) {
                    if (schema[i].name == final_order->column) {
                        fi   = static_cast<std::int32_t>(i);
                        flen = schema[i].length;
                        numeric =
                            schema[i].type == openads::drivers::DbfFieldType::Numeric ||
                            schema[i].type == openads::drivers::DbfFieldType::Float   ||
                            schema[i].type == openads::drivers::DbfFieldType::Integer ||
                            schema[i].type == openads::drivers::DbfFieldType::Currency||
                            schema[i].type == openads::drivers::DbfFieldType::Double;
                        break;
                    }
                    off += schema[i].length;
                }
                if (fi < 0) {
                    return fail(openads::AE_COLUMN_NOT_FOUND,
                                final_order->column.c_str());
                }
                bool desc = final_order->descending;
                std::stable_sort(rows.begin(), rows.end(),
                    [&](const std::vector<std::uint8_t>& a,
                        const std::vector<std::uint8_t>& b) {
                        std::string ka(
                            reinterpret_cast<const char*>(a.data() + off), flen);
                        std::string kb(
                            reinterpret_cast<const char*>(b.data() + off), flen);
                        bool less;
                        if (numeric) {
                            double da = std::strtod(ka.c_str(), nullptr);
                            double db = std::strtod(kb.c_str(), nullptr);
                            if (da == db) return false;
                            less = da < db;
                        } else {
                            if (ka == kb) return false;
                            less = ka < kb;
                        }
                        return desc ? !less : less;
                    });
            }

            // Materialise rows into the file buffer.
            for (auto& rec : rows) {
                file.insert(file.end(), rec.begin(), rec.end());
            }

            file.push_back(0x1A);
            std::uint32_t emitted = static_cast<std::uint32_t>(rows.size());
            file[4] = static_cast<std::uint8_t>( emitted        & 0xFFu);
            file[5] = static_cast<std::uint8_t>((emitted >>  8) & 0xFFu);
            file[6] = static_cast<std::uint8_t>((emitted >> 16) & 0xFFu);
            file[7] = static_cast<std::uint8_t>((emitted >> 24) & 0xFFu);

            namespace fs = std::filesystem;
            char nb[64];
            std::snprintf(nb, sizeof(nb), "_uni_%llx.dbf",
                          static_cast<unsigned long long>(
                              openads::platform::monotonic_nanos()));
            fs::path uni_dbf = fs::path(c->data_dir()) / nb;
            {
                std::ofstream out(uni_dbf, std::ios::binary);
                if (!out) return fail(openads::AE_INTERNAL_ERROR,
                    "union temp DBF: open for write failed");
                out.write(reinterpret_cast<const char*>(file.data()),
                          static_cast<std::streamsize>(file.size()));
            }
            std::string rel = uni_dbf.filename().string();
            auto uth = c->open_table(rel, openads::engine::TableType::Cdx,
                                     openads::engine::OpenMode::Read);
            if (!uth) return fail(uth.error());
            openads::engine::Table* utbl = c->lookup_table(uth.value());
            if (!utbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
            ADSHANDLE gh = s.registry.register_object(HandleKind::Table, utbl);
            *phCursor = gh;
            return ok();
        }
    }

    auto parsed = openads::sql::parse_select(sql);
    if (!parsed) return fail(parsed.error());

    if (parsed.value().inner_join) {
        // M10.14 materialises the join into a temp DBF cursor; M10.20
        // additionally compiles the outer WHERE / ORDER BY against
        // that cursor's merged schema; M10.23 runs aggregates over
        // that merged cursor when the projection is `agg(...)` instead
        // of a column list.
        const auto& j = *parsed.value().inner_join;
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);

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
                        default: return false;
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
            // M10.37 — multi-column ORDER BY against the joined cursor.
            struct SortKey {
                std::uint16_t field_index;
                bool          descending;
                bool          numeric;
            };
            std::vector<SortKey> sks;
            auto add_sk = [&](const openads::sql::OrderBy& ob)
                -> openads::util::Result<std::monostate>
            {
                std::int32_t fi = ctbl->field_index(ob.column);
                if (fi < 0) return openads::util::Error{
                    openads::AE_COLUMN_NOT_FOUND, 0,
                    ob.column.c_str(), ""};
                const auto& fd = ctbl->field_descriptor(
                    static_cast<std::uint16_t>(fi));
                SortKey k;
                k.field_index = static_cast<std::uint16_t>(fi);
                k.descending  = ob.descending;
                k.numeric =
                    fd.type == openads::drivers::DbfFieldType::Numeric ||
                    fd.type == openads::drivers::DbfFieldType::Float   ||
                    fd.type == openads::drivers::DbfFieldType::Integer ||
                    fd.type == openads::drivers::DbfFieldType::Currency||
                    fd.type == openads::drivers::DbfFieldType::Double;
                sks.push_back(k);
                return std::monostate{};
            };
            if (auto r = add_sk(*parsed.value().order_by); !r)
                return fail(r.error());
            for (auto& obx : parsed.value().order_by_extra) {
                if (auto r = add_sk(obx); !r) return fail(r.error());
            }
            struct Row {
                std::uint32_t            recno;
                std::vector<std::string> s;
                std::vector<double>      d;
            };
            std::vector<Row> rows;
            std::uint32_t crc = ctbl->record_count();
            for (std::uint32_t r = 1; r <= crc; ++r) {
                if (auto g = ctbl->goto_record(r); !g) continue;
                if (ctbl->is_deleted()) continue;
                if (!ctbl->passes_filter()) continue;
                Row row;
                row.recno = r;
                row.s.resize(sks.size());
                row.d.resize(sks.size());
                for (std::size_t i = 0; i < sks.size(); ++i) {
                    auto v = ctbl->read_field(sks[i].field_index);
                    if (v) {
                        row.s[i] = v.value().as_string;
                        row.d[i] = v.value().as_double;
                    }
                }
                rows.push_back(std::move(row));
            }
            std::stable_sort(rows.begin(), rows.end(),
                [&](const Row& a, const Row& b) {
                    for (std::size_t i = 0; i < sks.size(); ++i) {
                        bool less, equal;
                        if (sks[i].numeric) {
                            less  = a.d[i] <  b.d[i];
                            equal = a.d[i] == b.d[i];
                        } else {
                            less  = a.s[i] <  b.s[i];
                            equal = a.s[i] == b.s[i];
                        }
                        if (equal) continue;
                        return sks[i].descending ? !less : less;
                    }
                    return false;
                });
            std::vector<std::uint32_t> seq;
            seq.reserve(rows.size());
            for (auto& row : rows) seq.push_back(row.recno);
            ctbl->clear_filter();
            ctbl->set_recno_sequence(std::move(seq));
        }

        // M10.34 — GROUP BY across JOIN. Same shape as the plain-table
        // grouped path (M10.25) but reads from the merged cursor.
        if (!parsed.value().group_by.empty() &&
            !parsed.value().aggregates.empty()) {
            struct AggSlot {
                openads::sql::Aggregate def;
                std::int32_t            field_index = -1;
            };
            std::vector<AggSlot> slots;
            slots.reserve(parsed.value().aggregates.size());
            for (auto& a : parsed.value().aggregates) {
                AggSlot slot;
                slot.def = a;
                if (a.kind != openads::sql::AggregateKind::CountStar) {
                    slot.field_index = ctbl->field_index(a.column);
                    if (slot.field_index < 0) {
                        c->close_table(cth.value());
                        return fail(openads::AE_COLUMN_NOT_FOUND,
                                    a.column.c_str());
                    }
                }
                slots.push_back(std::move(slot));
            }

            struct GBCol {
                std::uint16_t field_index;
                std::uint8_t  length;
                std::string   name;
                std::uint8_t  raw_type;
            };
            std::vector<GBCol> gbs;
            gbs.reserve(parsed.value().group_by.size());
            for (auto& gname : parsed.value().group_by) {
                std::int32_t fi = ctbl->field_index(gname);
                if (fi < 0) {
                    c->close_table(cth.value());
                    return fail(openads::AE_COLUMN_NOT_FOUND, gname.c_str());
                }
                const auto& fd = ctbl->field_descriptor(
                    static_cast<std::uint16_t>(fi));
                GBCol gc;
                gc.field_index = static_cast<std::uint16_t>(fi);
                gc.length      = fd.length;
                gc.name        = gname;
                gc.raw_type    = static_cast<std::uint8_t>(fd.raw_type);
                gbs.push_back(std::move(gc));
            }

            auto resolve_slot = [&](const openads::sql::HavingCmp& ha)
                                  -> std::int32_t {
                for (std::size_t i = 0; i < slots.size(); ++i) {
                    if (slots[i].def.kind == ha.agg.kind &&
                        slots[i].def.column == ha.agg.column) {
                        return static_cast<std::int32_t>(i);
                    }
                }
                if (ha.agg.kind == openads::sql::AggregateKind::CountStar) {
                    for (std::size_t i = 0; i < slots.size(); ++i) {
                        if (slots[i].def.kind ==
                            openads::sql::AggregateKind::CountStar) {
                            return static_cast<std::int32_t>(i);
                        }
                    }
                }
                return -1;
            };
            if (parsed.value().having) {
                std::function<openads::util::Result<std::monostate>(
                    const openads::sql::HavingExpr&)> validate;
                validate = [&](const openads::sql::HavingExpr& n)
                            -> openads::util::Result<std::monostate> {
                    using K = openads::sql::HavingExpr::Kind;
                    if (n.kind == K::And || n.kind == K::Or) {
                        for (auto& cn : n.children) {
                            auto r = validate(*cn);
                            if (!r) return r.error();
                        }
                        return std::monostate{};
                    }
                    if (n.kind == K::Not) return validate(*n.child);
                    if (resolve_slot(n.cmp) < 0) {
                        return openads::util::Error{
                            openads::AE_PARSE_ERROR, 0,
                            "HAVING aggregate must match one in projection",
                            ""};
                    }
                    return std::monostate{};
                };
                auto vr = validate(*parsed.value().having);
                if (!vr) {
                    c->close_table(cth.value());
                    return fail(vr.error());
                }
            }

            struct GroupAcc {
                std::vector<std::string>   key_parts;
                std::vector<double>        sum;
                std::vector<double>        minv;
                std::vector<double>        maxv;
                std::vector<std::uint64_t> count;
                std::uint64_t              row_count = 0;
            };
            std::unordered_map<std::string, GroupAcc> groups;
            std::vector<std::string> insertion_order;
            std::uint32_t crc3 = ctbl->record_count();
            for (std::uint32_t r = 1; r <= crc3; ++r) {
                if (auto g = ctbl->goto_record(r); !g) continue;
                if (ctbl->is_deleted()) continue;
                if (!ctbl->passes_filter()) continue;
                std::string key;
                std::vector<std::string> parts;
                parts.reserve(gbs.size());
                for (auto& g : gbs) {
                    auto v = ctbl->read_field(g.field_index);
                    std::string raw = v ? v.value().as_string : std::string();
                    if (raw.size() < g.length)
                        raw.append(g.length - raw.size(), ' ');
                    else if (raw.size() > g.length) raw.resize(g.length);
                    parts.push_back(raw);
                    key.append(raw);
                    key.push_back('\x1f');
                }
                auto git = groups.find(key);
                if (git == groups.end()) {
                    GroupAcc acc;
                    acc.key_parts = std::move(parts);
                    acc.sum.assign(slots.size(), 0.0);
                    acc.minv.assign(slots.size(),
                        std::numeric_limits<double>::infinity());
                    acc.maxv.assign(slots.size(),
                        -std::numeric_limits<double>::infinity());
                    acc.count.assign(slots.size(), 0);
                    git = groups.emplace(key, std::move(acc)).first;
                    insertion_order.push_back(key);
                }
                auto& acc = git->second;
                ++acc.row_count;
                for (std::size_t i = 0; i < slots.size(); ++i) {
                    if (slots[i].def.kind ==
                        openads::sql::AggregateKind::CountStar) {
                        ++acc.count[i]; continue;
                    }
                    auto v = ctbl->read_field(
                        static_cast<std::uint16_t>(slots[i].field_index));
                    if (!v) continue;
                    double d = v.value().as_double;
                    ++acc.count[i];
                    acc.sum[i] += d;
                    if (d < acc.minv[i]) acc.minv[i] = d;
                    if (d > acc.maxv[i]) acc.maxv[i] = d;
                }
            }
            c->close_table(cth.value());

            auto agg_at = [&](const GroupAcc& acc, std::size_t si) -> double {
                using K = openads::sql::AggregateKind;
                switch (slots[si].def.kind) {
                    case K::CountStar: return static_cast<double>(acc.row_count);
                    case K::Count:     return static_cast<double>(acc.count[si]);
                    case K::Sum:       return acc.sum[si];
                    case K::Avg:
                        return acc.count[si]
                            ? acc.sum[si] / static_cast<double>(acc.count[si])
                            : 0.0;
                    case K::Min:
                        return acc.count[si] ? acc.minv[si] : 0.0;
                    case K::Max:
                        return acc.count[si] ? acc.maxv[si] : 0.0;
                }
                return 0.0;
            };
            std::function<bool(const openads::sql::HavingExpr&,
                               const GroupAcc&)> eval_having;
            eval_having = [&](const openads::sql::HavingExpr& n,
                              const GroupAcc& acc) -> bool {
                using K = openads::sql::HavingExpr::Kind;
                if (n.kind == K::And) {
                    for (auto& cn : n.children)
                        if (!eval_having(*cn, acc)) return false;
                    return true;
                }
                if (n.kind == K::Or) {
                    for (auto& cn : n.children)
                        if (eval_having(*cn, acc)) return true;
                    return false;
                }
                if (n.kind == K::Not) return !eval_having(*n.child, acc);
                std::int32_t si = resolve_slot(n.cmp);
                if (si < 0) return false;
                double v   = agg_at(acc, static_cast<std::size_t>(si));
                double rhs = n.cmp.num;
                switch (n.cmp.op) {
                    case openads::sql::WhereOp::Eq: return v == rhs;
                    case openads::sql::WhereOp::Ne: return v != rhs;
                    case openads::sql::WhereOp::Lt: return v <  rhs;
                    case openads::sql::WhereOp::Gt: return v >  rhs;
                    case openads::sql::WhereOp::Le: return v <= rhs;
                    case openads::sql::WhereOp::Ge: return v >= rhs;
                    default: return false;
                }
            };

            namespace fs = std::filesystem;
            char namebuf4[64];
            std::snprintf(namebuf4, sizeof(namebuf4), "_jgrp_%llx.dbf",
                          static_cast<unsigned long long>(
                              openads::platform::monotonic_nanos()));
            fs::path grp_dbf = fs::path(c->data_dir()) / namebuf4;
            std::vector<std::uint8_t> jg_file;
            std::array<std::uint8_t, 32> jg_hdr{};
            jg_hdr[0] = 0x03;
            std::uint16_t jg_hlen = static_cast<std::uint16_t>(
                32 + 32 * (gbs.size() + slots.size()) + 1);
            std::uint32_t jg_rlen = 1;
            for (auto& g : gbs) jg_rlen += g.length;
            jg_rlen += 30u * static_cast<std::uint32_t>(slots.size());
            jg_hdr[8]  = static_cast<std::uint8_t>( jg_hlen       & 0xFFu);
            jg_hdr[9]  = static_cast<std::uint8_t>((jg_hlen >> 8) & 0xFFu);
            jg_hdr[10] = static_cast<std::uint8_t>( jg_rlen       & 0xFFu);
            jg_hdr[11] = static_cast<std::uint8_t>((jg_rlen >> 8) & 0xFFu);
            jg_file.insert(jg_file.end(), jg_hdr.begin(), jg_hdr.end());
            for (auto& g : gbs) {
                std::array<std::uint8_t, 32> fd{};
                std::strncpy(reinterpret_cast<char*>(fd.data()),
                             g.name.c_str(), 11);
                fd[11] = g.raw_type ? g.raw_type : 'C';
                fd[16] = g.length;
                jg_file.insert(jg_file.end(), fd.begin(), fd.end());
            }
            for (std::size_t i = 0; i < slots.size(); ++i) {
                std::array<std::uint8_t, 32> fd{};
                char fn[16];
                std::snprintf(fn, sizeof(fn), "COL%zu", i + 1);
                std::strncpy(reinterpret_cast<char*>(fd.data()), fn, 11);
                fd[11] = 'C'; fd[16] = 30;
                jg_file.insert(jg_file.end(), fd.begin(), fd.end());
            }
            jg_file.push_back(0x0D);

            std::uint32_t jg_emitted = 0;
            for (auto& key : insertion_order) {
                auto& acc = groups[key];
                if (parsed.value().having) {
                    if (!eval_having(*parsed.value().having, acc)) continue;
                }
                jg_file.push_back(' ');
                for (std::size_t i = 0; i < gbs.size(); ++i) {
                    const std::string& kp = acc.key_parts[i];
                    for (std::uint8_t b = 0; b < gbs[i].length; ++b) {
                        jg_file.push_back(b < kp.size()
                            ? static_cast<std::uint8_t>(kp[b]) : ' ');
                    }
                }
                for (std::size_t i = 0; i < slots.size(); ++i) {
                    char buf[32] = {0};
                    using K = openads::sql::AggregateKind;
                    switch (slots[i].def.kind) {
                        case K::CountStar:
                            std::snprintf(buf, sizeof(buf), "%llu",
                                static_cast<unsigned long long>(acc.row_count));
                            break;
                        case K::Count:
                            std::snprintf(buf, sizeof(buf), "%llu",
                                static_cast<unsigned long long>(acc.count[i]));
                            break;
                        case K::Sum:
                            std::snprintf(buf, sizeof(buf), "%.6f", acc.sum[i]);
                            break;
                        case K::Avg:
                            std::snprintf(buf, sizeof(buf), "%.6f",
                                acc.count[i]
                                    ? acc.sum[i] /
                                        static_cast<double>(acc.count[i])
                                    : 0.0);
                            break;
                        case K::Min:
                            if (acc.count[i] == 0) std::strcpy(buf, "0");
                            else std::snprintf(buf, sizeof(buf), "%.6f",
                                               acc.minv[i]);
                            break;
                        case K::Max:
                            if (acc.count[i] == 0) std::strcpy(buf, "0");
                            else std::snprintf(buf, sizeof(buf), "%.6f",
                                               acc.maxv[i]);
                            break;
                    }
                    std::array<std::uint8_t, 30> cell{};
                    std::memset(cell.data(), ' ', cell.size());
                    std::size_t n = std::min<std::size_t>(std::strlen(buf), 30);
                    std::memcpy(cell.data(), buf, n);
                    jg_file.insert(jg_file.end(), cell.begin(), cell.end());
                }
                ++jg_emitted;
            }
            jg_file.push_back(0x1A);
            jg_file[4] = static_cast<std::uint8_t>( jg_emitted        & 0xFFu);
            jg_file[5] = static_cast<std::uint8_t>((jg_emitted >>  8) & 0xFFu);
            jg_file[6] = static_cast<std::uint8_t>((jg_emitted >> 16) & 0xFFu);
            jg_file[7] = static_cast<std::uint8_t>((jg_emitted >> 24) & 0xFFu);
            {
                std::ofstream out(grp_dbf, std::ios::binary);
                if (!out) return fail(openads::AE_INTERNAL_ERROR,
                    "join+group temp DBF: open for write failed");
                out.write(reinterpret_cast<const char*>(jg_file.data()),
                          static_cast<std::streamsize>(jg_file.size()));
            }
            std::string rel4 = grp_dbf.filename().string();
            auto gth = c->open_table(rel4, openads::engine::TableType::Cdx,
                                     openads::engine::OpenMode::Read);
            if (!gth) return fail(gth.error());
            openads::engine::Table* gtbl = c->lookup_table(gth.value());
            if (!gtbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
            ADSHANDLE gh = s.registry.register_object(HandleKind::Table, gtbl);
            *phCursor = gh;
            return ok();
        }

        // M10.23 — JOIN + aggregate. Walk the merged cursor (already
        // filtered by the outer WHERE) and replace it with a 1-row
        // aggregate temp DBF before registering the user-visible
        // handle.
        if (!parsed.value().aggregates.empty()) {
            struct AggSlot {
                openads::sql::Aggregate def;
                std::int32_t            field_index = -1;
            };
            std::vector<AggSlot> slots;
            slots.reserve(parsed.value().aggregates.size());
            for (auto& a : parsed.value().aggregates) {
                AggSlot slot;
                slot.def = a;
                if (a.kind != openads::sql::AggregateKind::CountStar) {
                    slot.field_index = ctbl->field_index(a.column);
                    if (slot.field_index < 0) {
                        c->close_table(cth.value());
                        return fail(openads::AE_COLUMN_NOT_FOUND, a.column.c_str());
                    }
                }
                slots.push_back(std::move(slot));
            }

            std::vector<double> sum(slots.size(), 0.0);
            std::vector<double> minv(slots.size(),
                std::numeric_limits<double>::infinity());
            std::vector<double> maxv(slots.size(),
                -std::numeric_limits<double>::infinity());
            std::vector<std::uint64_t> count(slots.size(), 0);
            std::uint64_t row_count = 0;
            std::uint32_t crc2 = ctbl->record_count();
            for (std::uint32_t r = 1; r <= crc2; ++r) {
                if (auto g = ctbl->goto_record(r); !g) continue;
                if (ctbl->is_deleted()) continue;
                if (!ctbl->passes_filter()) continue;
                ++row_count;
                for (std::size_t i = 0; i < slots.size(); ++i) {
                    if (slots[i].def.kind == openads::sql::AggregateKind::CountStar) {
                        ++count[i]; continue;
                    }
                    auto v = ctbl->read_field(
                        static_cast<std::uint16_t>(slots[i].field_index));
                    if (!v) continue;
                    double d = v.value().as_double;
                    ++count[i];
                    sum[i] += d;
                    if (d < minv[i]) minv[i] = d;
                    if (d > maxv[i]) maxv[i] = d;
                }
            }
            c->close_table(cth.value());

            namespace fs = std::filesystem;
            char namebuf2[64];
            std::snprintf(namebuf2, sizeof(namebuf2), "_jagg_%llx.dbf",
                          static_cast<unsigned long long>(
                              openads::platform::monotonic_nanos()));
            fs::path agg_dbf = fs::path(c->data_dir()) / namebuf2;
            std::vector<std::uint8_t> agg_file;
            std::array<std::uint8_t, 32> agg_hdr{};
            agg_hdr[0] = 0x03;
            agg_hdr[4] = 1;
            std::uint16_t agg_hlen = static_cast<std::uint16_t>(
                32 + 32 * slots.size() + 1);
            std::uint16_t agg_rlen = static_cast<std::uint16_t>(
                1 + 30 * slots.size());
            agg_hdr[8]  = static_cast<std::uint8_t>( agg_hlen       & 0xFFu);
            agg_hdr[9]  = static_cast<std::uint8_t>((agg_hlen >> 8) & 0xFFu);
            agg_hdr[10] = static_cast<std::uint8_t>( agg_rlen       & 0xFFu);
            agg_hdr[11] = static_cast<std::uint8_t>((agg_rlen >> 8) & 0xFFu);
            agg_file.insert(agg_file.end(), agg_hdr.begin(), agg_hdr.end());
            for (std::size_t i = 0; i < slots.size(); ++i) {
                std::array<std::uint8_t, 32> fd{};
                char fn[16];
                std::snprintf(fn, sizeof(fn), "COL%zu", i + 1);
                std::strncpy(reinterpret_cast<char*>(fd.data()), fn, 11);
                fd[11] = 'C'; fd[16] = 30;
                agg_file.insert(agg_file.end(), fd.begin(), fd.end());
            }
            agg_file.push_back(0x0D);
            agg_file.push_back(' ');
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
                agg_file.insert(agg_file.end(), cell.begin(), cell.end());
            }
            agg_file.push_back(0x1A);
            {
                std::ofstream out(agg_dbf, std::ios::binary);
                if (!out) return fail(openads::AE_INTERNAL_ERROR,
                                      "join+agg temp DBF: open for write failed");
                out.write(reinterpret_cast<const char*>(agg_file.data()),
                          static_cast<std::streamsize>(agg_file.size()));
            }
            std::string rel2 = agg_dbf.filename().string();
            auto ath = c->open_table(rel2, openads::engine::TableType::Cdx,
                                     openads::engine::OpenMode::Read);
            if (!ath) return fail(ath.error());
            openads::engine::Table* atbl = c->lookup_table(ath.value());
            if (!atbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
            ADSHANDLE gh = s.registry.register_object(HandleKind::Table, atbl);
            *phCursor = gh;
            return ok();
        }

        ADSHANDLE gh = s.registry.register_object(HandleKind::Table, ctbl);
        *phCursor = gh;
        return ok();
    }

    // M10.46 — derived table: `FROM (SELECT ...)`. Recursively run
    // the inner SELECT first; the resulting cursor's underlying
    // engine::Table becomes the source for the outer clauses.
    auto& s = state();
    openads::engine::Table* tbl = nullptr;
    ADSHANDLE derived_cur = 0;
    if (!parsed.value().derived_sql.empty()) {
        std::vector<UNSIGNED8> selbuf(parsed.value().derived_sql.size() + 1);
        std::memcpy(selbuf.data(),
                    parsed.value().derived_sql.c_str(),
                    parsed.value().derived_sql.size() + 1);
        UNSIGNED32 rrc =
            AdsExecuteSQLDirect(hStatement, selbuf.data(), &derived_cur);
        if (rrc != 0) return rrc;
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        tbl = s.registry.lookup<openads::engine::Table>(
            derived_cur, HandleKind::Table);
        if (!tbl) return fail(openads::AE_INTERNAL_ERROR,
                              "derived table cursor lookup");
        // continue, lock_guard scoped to whole function below by
        // dropping out — we want to hold lock through registration.
        // Since `lk` would die at end of this `if` block, re-take.
    }
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Handle table_handle = 0;
    if (!tbl) {
        auto th = c->open_table(parsed.value().table,
                                openads::engine::TableType::Cdx,
                                openads::engine::OpenMode::Read);
        if (!th) return fail(th.error());
        table_handle = th.value();
        tbl = c->lookup_table(table_handle);
        if (!tbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
    }
    (void)table_handle;

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
                    if (table_handle != 0) c->close_table(table_handle);
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
                        default: return false;
                    }
                    return false;
                }};
            };
            auto compiled = compile(*parsed.value().where);
            if (!compiled) {
                if (table_handle != 0) c->close_table(table_handle);
                return fail(compiled.error());
            }
            filter = std::move(compiled).value();
        }

        // M10.25 — `GROUP BY <col>[, <col>...] [HAVING <agg> op num]`.
        // Walk matching rows, hash by group-key tuple, accumulate per
        // group, then emit one row per group (passing HAVING) into a
        // multi-row temp DBF cursor. Schema: original group-by
        // columns (preserving type + length) followed by COL1..COLn
        // C(30) cells for each aggregate.
        if (!parsed.value().group_by.empty()) {
            struct GBCol {
                std::uint16_t field_index;
                std::uint8_t  length;
                std::string   name;
                std::uint8_t  raw_type;
            };
            std::vector<GBCol> gbs;
            gbs.reserve(parsed.value().group_by.size());
            for (auto& gname : parsed.value().group_by) {
                std::int32_t fi = tbl->field_index(gname);
                if (fi < 0) {
                    if (table_handle != 0) c->close_table(table_handle);
                    return fail(openads::AE_COLUMN_NOT_FOUND, gname.c_str());
                }
                const auto& fd = tbl->field_descriptor(
                    static_cast<std::uint16_t>(fi));
                GBCol gc;
                gc.field_index = static_cast<std::uint16_t>(fi);
                gc.length      = fd.length;
                gc.name        = gname;
                gc.raw_type    = static_cast<std::uint8_t>(fd.raw_type);
                gbs.push_back(std::move(gc));
            }

            // M10.30 — HAVING is now a boolean tree of HavingCmp leaves.
            // Resolve each leaf to a slot at compile time (validation
            // only); per-group evaluation re-walks the tree.
            auto resolve_slot = [&](const openads::sql::HavingCmp& ha)
                                  -> std::int32_t {
                for (std::size_t i = 0; i < slots.size(); ++i) {
                    if (slots[i].def.kind == ha.agg.kind &&
                        slots[i].def.column == ha.agg.column) {
                        return static_cast<std::int32_t>(i);
                    }
                }
                if (ha.agg.kind == openads::sql::AggregateKind::CountStar) {
                    for (std::size_t i = 0; i < slots.size(); ++i) {
                        if (slots[i].def.kind ==
                            openads::sql::AggregateKind::CountStar) {
                            return static_cast<std::int32_t>(i);
                        }
                    }
                }
                return -1;
            };
            if (parsed.value().having) {
                std::function<openads::util::Result<std::monostate>(
                    const openads::sql::HavingExpr&)> validate;
                validate = [&](const openads::sql::HavingExpr& n)
                            -> openads::util::Result<std::monostate> {
                    using K = openads::sql::HavingExpr::Kind;
                    if (n.kind == K::And || n.kind == K::Or) {
                        for (auto& cn : n.children) {
                            auto r = validate(*cn);
                            if (!r) return r.error();
                        }
                        return std::monostate{};
                    }
                    if (n.kind == K::Not) return validate(*n.child);
                    if (resolve_slot(n.cmp) < 0) {
                        return openads::util::Error{
                            openads::AE_PARSE_ERROR, 0,
                            "HAVING aggregate must match one in projection",
                            ""};
                    }
                    return std::monostate{};
                };
                auto vr = validate(*parsed.value().having);
                if (!vr) {
                    if (table_handle != 0) c->close_table(table_handle);
                    return fail(vr.error());
                }
            }

            struct GroupAcc {
                std::vector<std::string>   key_parts;
                std::vector<double>        sum;
                std::vector<double>        minv;
                std::vector<double>        maxv;
                std::vector<std::uint64_t> count;
                std::uint64_t              row_count = 0;
            };
            std::unordered_map<std::string, GroupAcc> groups;
            std::vector<std::string> insertion_order;
            std::uint32_t rcount = tbl->record_count();
            for (std::uint32_t r = 1; r <= rcount; ++r) {
                if (auto g = tbl->goto_record(r); !g) continue;
                if (tbl->is_deleted()) continue;
                if (filter && !filter(*tbl)) continue;
                std::string key;
                std::vector<std::string> parts;
                parts.reserve(gbs.size());
                for (auto& g : gbs) {
                    auto v = tbl->read_field(g.field_index);
                    std::string raw = v ? v.value().as_string : std::string();
                    if (raw.size() < g.length) raw.append(g.length - raw.size(), ' ');
                    else if (raw.size() > g.length) raw.resize(g.length);
                    parts.push_back(raw);
                    key.append(raw);
                    key.push_back('\x1f');
                }
                auto git = groups.find(key);
                if (git == groups.end()) {
                    GroupAcc acc;
                    acc.key_parts = std::move(parts);
                    acc.sum.assign(slots.size(), 0.0);
                    acc.minv.assign(slots.size(),
                        std::numeric_limits<double>::infinity());
                    acc.maxv.assign(slots.size(),
                        -std::numeric_limits<double>::infinity());
                    acc.count.assign(slots.size(), 0);
                    git = groups.emplace(key, std::move(acc)).first;
                    insertion_order.push_back(key);
                }
                auto& acc = git->second;
                ++acc.row_count;
                for (std::size_t i = 0; i < slots.size(); ++i) {
                    if (slots[i].def.kind ==
                        openads::sql::AggregateKind::CountStar) {
                        ++acc.count[i];
                        continue;
                    }
                    auto v = tbl->read_field(
                        static_cast<std::uint16_t>(slots[i].field_index));
                    if (!v) continue;
                    double d = v.value().as_double;
                    ++acc.count[i];
                    acc.sum[i] += d;
                    if (d < acc.minv[i]) acc.minv[i] = d;
                    if (d > acc.maxv[i]) acc.maxv[i] = d;
                }
            }
            if (table_handle != 0) c->close_table(table_handle);

            auto agg_at = [&](const GroupAcc& acc, std::size_t si) -> double {
                using K = openads::sql::AggregateKind;
                switch (slots[si].def.kind) {
                    case K::CountStar: return static_cast<double>(acc.row_count);
                    case K::Count:     return static_cast<double>(acc.count[si]);
                    case K::Sum:       return acc.sum[si];
                    case K::Avg:
                        return acc.count[si]
                            ? acc.sum[si] / static_cast<double>(acc.count[si])
                            : 0.0;
                    case K::Min:
                        return acc.count[si] ? acc.minv[si] : 0.0;
                    case K::Max:
                        return acc.count[si] ? acc.maxv[si] : 0.0;
                }
                return 0.0;
            };

            namespace fs = std::filesystem;
            char namebuf3[64];
            std::snprintf(namebuf3, sizeof(namebuf3), "_grp_%llx.dbf",
                          static_cast<unsigned long long>(
                              openads::platform::monotonic_nanos()));
            fs::path grp_dbf = fs::path(c->data_dir()) / namebuf3;
            std::vector<std::uint8_t> file;
            std::array<std::uint8_t, 32> hdr{};
            hdr[0] = 0x03;
            std::uint16_t header_len = static_cast<std::uint16_t>(
                32 + 32 * (gbs.size() + slots.size()) + 1);
            std::uint32_t rec_len = 1;
            for (auto& g : gbs) rec_len += g.length;
            rec_len += 30u * static_cast<std::uint32_t>(slots.size());
            hdr[8]  = static_cast<std::uint8_t>( header_len       & 0xFFu);
            hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
            hdr[10] = static_cast<std::uint8_t>( rec_len          & 0xFFu);
            hdr[11] = static_cast<std::uint8_t>((rec_len    >> 8) & 0xFFu);
            file.insert(file.end(), hdr.begin(), hdr.end());

            for (auto& g : gbs) {
                std::array<std::uint8_t, 32> fd{};
                std::strncpy(reinterpret_cast<char*>(fd.data()),
                             g.name.c_str(), 11);
                fd[11] = g.raw_type ? g.raw_type : 'C';
                fd[16] = g.length;
                file.insert(file.end(), fd.begin(), fd.end());
            }
            for (std::size_t i = 0; i < slots.size(); ++i) {
                std::array<std::uint8_t, 32> fd{};
                char fn[16];
                std::snprintf(fn, sizeof(fn), "COL%zu", i + 1);
                std::strncpy(reinterpret_cast<char*>(fd.data()), fn, 11);
                fd[11] = 'C'; fd[16] = 30;
                file.insert(file.end(), fd.begin(), fd.end());
            }
            file.push_back(0x0D);

            std::function<bool(const openads::sql::HavingExpr&,
                               const GroupAcc&)> eval_having;
            eval_having = [&](const openads::sql::HavingExpr& n,
                              const GroupAcc& acc) -> bool {
                using K = openads::sql::HavingExpr::Kind;
                if (n.kind == K::And) {
                    for (auto& cn : n.children)
                        if (!eval_having(*cn, acc)) return false;
                    return true;
                }
                if (n.kind == K::Or) {
                    for (auto& cn : n.children)
                        if (eval_having(*cn, acc)) return true;
                    return false;
                }
                if (n.kind == K::Not) return !eval_having(*n.child, acc);
                std::int32_t si = resolve_slot(n.cmp);
                if (si < 0) return false;
                double v   = agg_at(acc, static_cast<std::size_t>(si));
                double rhs = n.cmp.num;
                switch (n.cmp.op) {
                    case openads::sql::WhereOp::Eq: return v == rhs;
                    case openads::sql::WhereOp::Ne: return v != rhs;
                    case openads::sql::WhereOp::Lt: return v <  rhs;
                    case openads::sql::WhereOp::Gt: return v >  rhs;
                    case openads::sql::WhereOp::Le: return v <= rhs;
                    case openads::sql::WhereOp::Ge: return v >= rhs;
                    default: return false;
                }
            };

            std::uint32_t emitted = 0;
            for (auto& key : insertion_order) {
                auto& acc = groups[key];
                if (parsed.value().having) {
                    if (!eval_having(*parsed.value().having, acc)) continue;
                }
                file.push_back(' ');
                for (std::size_t i = 0; i < gbs.size(); ++i) {
                    const std::string& kp = acc.key_parts[i];
                    for (std::uint8_t b = 0; b < gbs[i].length; ++b) {
                        file.push_back(b < kp.size()
                            ? static_cast<std::uint8_t>(kp[b]) : ' ');
                    }
                }
                for (std::size_t i = 0; i < slots.size(); ++i) {
                    char buf[32] = {0};
                    using K = openads::sql::AggregateKind;
                    switch (slots[i].def.kind) {
                        case K::CountStar:
                            std::snprintf(buf, sizeof(buf), "%llu",
                                static_cast<unsigned long long>(acc.row_count));
                            break;
                        case K::Count:
                            std::snprintf(buf, sizeof(buf), "%llu",
                                static_cast<unsigned long long>(acc.count[i]));
                            break;
                        case K::Sum:
                            std::snprintf(buf, sizeof(buf), "%.6f", acc.sum[i]);
                            break;
                        case K::Avg:
                            std::snprintf(buf, sizeof(buf), "%.6f",
                                acc.count[i]
                                    ? acc.sum[i] /
                                        static_cast<double>(acc.count[i])
                                    : 0.0);
                            break;
                        case K::Min:
                            if (acc.count[i] == 0) std::strcpy(buf, "0");
                            else std::snprintf(buf, sizeof(buf), "%.6f",
                                               acc.minv[i]);
                            break;
                        case K::Max:
                            if (acc.count[i] == 0) std::strcpy(buf, "0");
                            else std::snprintf(buf, sizeof(buf), "%.6f",
                                               acc.maxv[i]);
                            break;
                    }
                    std::array<std::uint8_t, 30> cell{};
                    std::memset(cell.data(), ' ', cell.size());
                    std::size_t n = std::min<std::size_t>(std::strlen(buf), 30);
                    std::memcpy(cell.data(), buf, n);
                    file.insert(file.end(), cell.begin(), cell.end());
                }
                ++emitted;
            }
            file.push_back(0x1A);
            file[4] = static_cast<std::uint8_t>( emitted        & 0xFFu);
            file[5] = static_cast<std::uint8_t>((emitted >>  8) & 0xFFu);
            file[6] = static_cast<std::uint8_t>((emitted >> 16) & 0xFFu);
            file[7] = static_cast<std::uint8_t>((emitted >> 24) & 0xFFu);
            {
                std::ofstream out(grp_dbf, std::ios::binary);
                if (!out) return fail(openads::AE_INTERNAL_ERROR,
                                      "group-by temp DBF: open for write failed");
                out.write(reinterpret_cast<const char*>(file.data()),
                          static_cast<std::streamsize>(file.size()));
            }
            std::string rel3 = grp_dbf.filename().string();
            auto gth = c->open_table(rel3, openads::engine::TableType::Cdx,
                                     openads::engine::OpenMode::Read);
            if (!gth) return fail(gth.error());
            openads::engine::Table* gtbl = c->lookup_table(gth.value());
            if (!gtbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
            ADSHANDLE gh = s.registry.register_object(HandleKind::Table, gtbl);
            *phCursor = gh;
            return ok();
        }

        // M10.54 — compile each slot's optional FILTER. Subset:
        // Cmp / AND / OR / NOT (full WHERE support left to follow-up).
        using AggPred = std::function<bool(openads::engine::Table&)>;
        std::vector<AggPred> slot_preds(slots.size());
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (!parsed.value().aggregates[i].filter) continue;
            std::function<openads::util::Result<AggPred>(
                const openads::sql::WhereExpr&)> cf;
            cf = [&](const openads::sql::WhereExpr& n)
                  -> openads::util::Result<AggPred> {
                using K = openads::sql::WhereExpr::Kind;
                if (n.kind == K::And || n.kind == K::Or) {
                    std::vector<AggPred> ks;
                    for (auto& cn : n.children) {
                        auto r = cf(*cn);
                        if (!r) return r.error();
                        ks.push_back(std::move(r).value());
                    }
                    bool is_and = (n.kind == K::And);
                    return AggPred{[ks = std::move(ks), is_and]
                                   (openads::engine::Table& t) {
                        if (is_and) {
                            for (auto& k : ks) if (!k(t)) return false;
                            return true;
                        }
                        for (auto& k : ks) if (k(t)) return true;
                        return false;
                    }};
                }
                if (n.kind == K::Not) {
                    auto inner = cf(*n.child);
                    if (!inner) return inner.error();
                    return AggPred{[p = std::move(inner).value()]
                                   (openads::engine::Table& t)
                                   { return !p(t); }};
                }
                if (n.kind != K::Cmp) {
                    return openads::util::Error{
                        openads::AE_FUNCTION_NOT_AVAILABLE, 0,
                        "aggregate FILTER supports Cmp/AND/OR/NOT only", ""};
                }
                const auto& w = n.cmp;
                std::int32_t fi = tbl->field_index(w.column);
                if (fi < 0) return openads::util::Error{
                    openads::AE_COLUMN_NOT_FOUND, 0, w.column.c_str(), ""};
                std::uint16_t f = static_cast<std::uint16_t>(fi);
                openads::sql::WhereOp op = w.op;
                std::string lit = w.literal;
                bool is_num = w.is_numeric;
                double num = w.number;
                return AggPred{[f, op, lit, is_num, num]
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
                        default: return false;
                    }
                }};
            };
            auto p = cf(*parsed.value().aggregates[i].filter);
            if (!p) return fail(p.error());
            slot_preds[i] = std::move(p).value();
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
                if (slot_preds[i] && !slot_preds[i](*tbl)) continue;
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
        if (table_handle != 0) c->close_table(table_handle);

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
                    // M10.54 — when this slot has a FILTER, count[i]
                    // already excludes filter-failing rows; use it
                    // even for CountStar.
                    std::snprintf(buf, sizeof(buf), "%llu",
                        static_cast<unsigned long long>(
                            slots[i].def.kind ==
                                openads::sql::AggregateKind::CountStar
                                ? (slot_preds[i] ? count[i] : row_count)
                                : count[i]));
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
            // M10.33 — BETWEEN upper bound.
            std::string                         literal2;
            double                              number2 = 0.0;
            // M11.7 — case-insensitive ASCII compare when set.
            bool                                nocase = false;
        };
        bool conn_nocase =
            (c->collation() == Connection::Collation::NoCase);
        auto to_lower_ascii = [](std::string s) {
            for (auto& ch : s) {
                if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32);
            }
            return s;
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
                // M10.17 / M10.24 — EXISTS / NOT EXISTS. Honors
                // subquery's WHERE (M10.24); when that WHERE has
                // outer-column references (e.g.
                // `EXISTS (SELECT * FROM b WHERE b.x = a.y)`), the
                // predicate re-evaluates per outer row, binding outer
                // values from the live `tbl` cursor.
                if (!node.exists_subquery) {
                    return openads::util::Error{
                        openads::AE_PARSE_ERROR, 0,
                        "EXISTS subquery missing", ""};
                }
                // Move ownership of the subquery into a shared_ptr so
                // the captured WhereExpr* outlives the parsed
                // SelectStmt (which is local to AdsExecuteSQLDirect).
                auto sub = std::shared_ptr<openads::sql::SelectStmt>(
                    const_cast<openads::sql::WhereExpr&>(node)
                        .exists_subquery.release());
                openads::engine::Table* outer_tbl = tbl;
                std::string sub_table = sub->table;
                return Pred{[c, sub, outer_tbl, sub_table]
                            (openads::engine::Table&) -> bool {
                    auto sh = c->open_table(sub_table,
                                            openads::engine::TableType::Cdx,
                                            openads::engine::OpenMode::Read);
                    if (!sh) return false;
                    openads::engine::Table* stbl = c->lookup_table(sh.value());
                    if (!stbl) { c->close_table(sh.value()); return false; }
                    auto trim = [](std::string s) {
                        while (!s.empty() && s.back() == ' ') s.pop_back();
                        return s;
                    };
                    std::function<bool(const openads::sql::WhereExpr&)> evalw;
                    evalw = [&](const openads::sql::WhereExpr& n) -> bool {
                        using K = openads::sql::WhereExpr::Kind;
                        if (n.kind == K::And) {
                            for (auto& cn : n.children)
                                if (!evalw(*cn)) return false;
                            return true;
                        }
                        if (n.kind == K::Or) {
                            for (auto& cn : n.children)
                                if (evalw(*cn)) return true;
                            return false;
                        }
                        if (n.kind == K::Not) return !evalw(*n.child);
                        if (n.kind != K::Cmp) return false;
                        const auto& w = n.cmp;
                        std::int32_t fi = stbl->field_index(w.column);
                        if (fi < 0) return false;
                        auto v = stbl->read_field(
                            static_cast<std::uint16_t>(fi));
                        if (!v) return false;
                        int cmp = 0;
                        if (w.is_outer_ref) {
                            std::int32_t ofi =
                                outer_tbl->field_index(w.outer_column);
                            if (ofi < 0) return false;
                            auto ov = outer_tbl->read_field(
                                static_cast<std::uint16_t>(ofi));
                            if (!ov) return false;
                            cmp = trim(v.value().as_string)
                                      .compare(trim(ov.value().as_string));
                        } else if (w.is_numeric) {
                            double d = v.value().as_double;
                            if      (d < w.number) cmp = -1;
                            else if (d > w.number) cmp =  1;
                        } else {
                            cmp = trim(v.value().as_string).compare(w.literal);
                        }
                        switch (w.op) {
                            case openads::sql::WhereOp::Eq: return cmp == 0;
                            case openads::sql::WhereOp::Ne: return cmp != 0;
                            case openads::sql::WhereOp::Lt: return cmp <  0;
                            case openads::sql::WhereOp::Gt: return cmp >  0;
                            case openads::sql::WhereOp::Le: return cmp <= 0;
                            case openads::sql::WhereOp::Ge: return cmp >= 0;
                            default: return false;
                        }
                    };
                    bool any = false;
                    std::uint32_t srcount = stbl->record_count();
                    for (std::uint32_t r = 1; r <= srcount; ++r) {
                        if (auto g = stbl->goto_record(r); !g) continue;
                        if (stbl->is_deleted()) continue;
                        if (!sub->where) { any = true; break; }
                        if (evalw(*sub->where)) { any = true; break; }
                    }
                    c->close_table(sh.value());
                    return any;
                }};
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
                    // M10.35 — detect correlation in subquery's WHERE.
                    bool correlated = false;
                    if (node.in_clause.subquery->where) {
                        std::function<void(const openads::sql::WhereExpr&)>
                            scan;
                        scan = [&](const openads::sql::WhereExpr& n) {
                            using K = openads::sql::WhereExpr::Kind;
                            if (correlated) return;
                            if (n.kind == K::And || n.kind == K::Or) {
                                for (auto& cn : n.children) scan(*cn);
                                return;
                            }
                            if (n.kind == K::Not) { scan(*n.child); return; }
                            if (n.kind == K::Cmp && n.cmp.is_outer_ref)
                                correlated = true;
                        };
                        scan(*node.in_clause.subquery->where);
                    }
                    if (correlated) {
                        auto sub = std::shared_ptr<openads::sql::SelectStmt>(
                            const_cast<openads::sql::InClause&>(
                                node.in_clause).subquery.release());
                        openads::engine::Table* outer_tbl = tbl;
                        std::string sub_table = sub->table;
                        return Pred{[c, sub, outer_tbl, fi, sub_table,
                                     trim_trailing]
                                    (openads::engine::Table&) -> bool {
                            auto sh = c->open_table(
                                sub_table,
                                openads::engine::TableType::Cdx,
                                openads::engine::OpenMode::Read);
                            if (!sh) return false;
                            openads::engine::Table* stbl =
                                c->lookup_table(sh.value());
                            if (!stbl) {
                                c->close_table(sh.value()); return false;
                            }
                            if (sub->projection.size() != 1 ||
                                !sub->aggregates.empty()) {
                                c->close_table(sh.value()); return false;
                            }
                            std::int32_t scol =
                                stbl->field_index(sub->projection[0]);
                            if (scol < 0) {
                                c->close_table(sh.value()); return false;
                            }
                            auto trim = trim_trailing;
                            std::function<bool(const openads::sql::WhereExpr&)>
                                evalw;
                            evalw = [&](const openads::sql::WhereExpr& n)
                                    -> bool {
                                using K = openads::sql::WhereExpr::Kind;
                                if (n.kind == K::And) {
                                    for (auto& cn : n.children)
                                        if (!evalw(*cn)) return false;
                                    return true;
                                }
                                if (n.kind == K::Or) {
                                    for (auto& cn : n.children)
                                        if (evalw(*cn)) return true;
                                    return false;
                                }
                                if (n.kind == K::Not) return !evalw(*n.child);
                                if (n.kind != K::Cmp) return false;
                                const auto& wn = n.cmp;
                                std::int32_t sfi =
                                    stbl->field_index(wn.column);
                                if (sfi < 0) return false;
                                auto v = stbl->read_field(
                                    static_cast<std::uint16_t>(sfi));
                                if (!v) return false;
                                int cmp = 0;
                                if (wn.is_outer_ref) {
                                    std::int32_t ofi =
                                        outer_tbl->field_index(wn.outer_column);
                                    if (ofi < 0) return false;
                                    auto ov = outer_tbl->read_field(
                                        static_cast<std::uint16_t>(ofi));
                                    if (!ov) return false;
                                    cmp = trim(v.value().as_string)
                                              .compare(trim(ov.value().as_string));
                                } else if (wn.is_numeric) {
                                    double d = v.value().as_double;
                                    if      (d < wn.number) cmp = -1;
                                    else if (d > wn.number) cmp =  1;
                                } else {
                                    cmp = trim(v.value().as_string)
                                              .compare(wn.literal);
                                }
                                switch (wn.op) {
                                    case openads::sql::WhereOp::Eq: return cmp == 0;
                                    case openads::sql::WhereOp::Ne: return cmp != 0;
                                    case openads::sql::WhereOp::Lt: return cmp <  0;
                                    case openads::sql::WhereOp::Gt: return cmp >  0;
                                    case openads::sql::WhereOp::Le: return cmp <= 0;
                                    case openads::sql::WhereOp::Ge: return cmp >= 0;
                                    default: return false;
                                }
                            };
                            auto ov = outer_tbl->read_field(fi);
                            if (!ov) {
                                c->close_table(sh.value()); return false;
                            }
                            std::string outer_v =
                                trim(ov.value().as_string);
                            bool any = false;
                            std::uint32_t srcount = stbl->record_count();
                            for (std::uint32_t r = 1; r <= srcount; ++r) {
                                if (auto g = stbl->goto_record(r); !g)
                                    continue;
                                if (stbl->is_deleted()) continue;
                                if (sub->where && !evalw(*sub->where))
                                    continue;
                                auto sv = stbl->read_field(
                                    static_cast<std::uint16_t>(scol));
                                if (!sv) continue;
                                if (trim(sv.value().as_string) == outer_v) {
                                    any = true; break;
                                }
                            }
                            c->close_table(sh.value());
                            return any;
                        }};
                    }
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
            term.literal2    = w.literal2;
            term.number2     = w.number2;
            // M11.7 — stamp collation onto the term when the
            // connection is in nocase mode and the cmp involves
            // string operands.
            if (conn_nocase && !w.is_numeric) {
                term.nocase   = true;
                term.literal  = to_lower_ascii(term.literal);
                term.literal2 = to_lower_ascii(term.literal2);
            }
            if (w.subquery) {
                // M10.29 — correlated scalar subquery. If the
                // subquery's WHERE references an outer column, we
                // re-evaluate the subquery per outer row instead of
                // materialising a single value at compile time.
                bool correlated = false;
                if (w.subquery->where) {
                    std::function<void(const openads::sql::WhereExpr&)> scan;
                    scan = [&](const openads::sql::WhereExpr& n) {
                        using K = openads::sql::WhereExpr::Kind;
                        if (correlated) return;
                        if (n.kind == K::And || n.kind == K::Or) {
                            for (auto& cn : n.children) scan(*cn);
                            return;
                        }
                        if (n.kind == K::Not) { scan(*n.child); return; }
                        if (n.kind == K::Cmp && n.cmp.is_outer_ref)
                            correlated = true;
                    };
                    scan(*w.subquery->where);
                }
                if (correlated) {
                    auto sub = std::shared_ptr<openads::sql::SelectStmt>(
                        const_cast<openads::sql::WhereCmp&>(w)
                            .subquery.release());
                    openads::engine::Table* outer_tbl = tbl;
                    std::uint16_t outer_field =
                        static_cast<std::uint16_t>(fidx);
                    bool outer_is_numeric_local =
                        tbl->field_descriptor(outer_field).type !=
                            openads::drivers::DbfFieldType::Character;
                    openads::sql::WhereOp op_local = w.op;
                    std::string sub_table = sub->table;
                    return Pred{[c, sub, outer_tbl, outer_field,
                                 outer_is_numeric_local, op_local, sub_table]
                                (openads::engine::Table&) -> bool {
                        auto sh = c->open_table(
                            sub_table,
                            openads::engine::TableType::Cdx,
                            openads::engine::OpenMode::Read);
                        if (!sh) return false;
                        openads::engine::Table* stbl =
                            c->lookup_table(sh.value());
                        if (!stbl) {
                            c->close_table(sh.value()); return false;
                        }
                        auto trim = [](std::string s) {
                            while (!s.empty() && s.back() == ' ')
                                s.pop_back();
                            return s;
                        };
                        std::function<bool(const openads::sql::WhereExpr&)>
                            evalw;
                        evalw = [&](const openads::sql::WhereExpr& n) -> bool {
                            using K = openads::sql::WhereExpr::Kind;
                            if (n.kind == K::And) {
                                for (auto& cn : n.children)
                                    if (!evalw(*cn)) return false;
                                return true;
                            }
                            if (n.kind == K::Or) {
                                for (auto& cn : n.children)
                                    if (evalw(*cn)) return true;
                                return false;
                            }
                            if (n.kind == K::Not) return !evalw(*n.child);
                            if (n.kind != K::Cmp) return false;
                            const auto& wn = n.cmp;
                            std::int32_t fi = stbl->field_index(wn.column);
                            if (fi < 0) return false;
                            auto v = stbl->read_field(
                                static_cast<std::uint16_t>(fi));
                            if (!v) return false;
                            int cmp = 0;
                            if (wn.is_outer_ref) {
                                std::int32_t ofi =
                                    outer_tbl->field_index(wn.outer_column);
                                if (ofi < 0) return false;
                                auto ov = outer_tbl->read_field(
                                    static_cast<std::uint16_t>(ofi));
                                if (!ov) return false;
                                cmp = trim(v.value().as_string)
                                          .compare(trim(ov.value().as_string));
                            } else if (wn.is_numeric) {
                                double d = v.value().as_double;
                                if      (d < wn.number) cmp = -1;
                                else if (d > wn.number) cmp =  1;
                            } else {
                                cmp = trim(v.value().as_string)
                                          .compare(wn.literal);
                            }
                            switch (wn.op) {
                                case openads::sql::WhereOp::Eq: return cmp == 0;
                                case openads::sql::WhereOp::Ne: return cmp != 0;
                                case openads::sql::WhereOp::Lt: return cmp <  0;
                                case openads::sql::WhereOp::Gt: return cmp >  0;
                                case openads::sql::WhereOp::Le: return cmp <= 0;
                                case openads::sql::WhereOp::Ge: return cmp >= 0;
                                default: return false;
                            }
                        };
                        double scalar_num = 0.0;
                        std::string scalar_str;
                        bool found = false;
                        std::uint32_t srcount = stbl->record_count();
                        if (sub->aggregates.size() == 1 &&
                            sub->projection.empty()) {
                            const auto& a = sub->aggregates[0];
                            std::int32_t scol = -1;
                            if (a.kind !=
                                openads::sql::AggregateKind::CountStar) {
                                scol = stbl->field_index(a.column);
                                if (scol < 0) {
                                    c->close_table(sh.value()); return false;
                                }
                            }
                            std::uint64_t cnt = 0;
                            double sum  = 0.0;
                            double minv =  std::numeric_limits<double>::infinity();
                            double maxv = -std::numeric_limits<double>::infinity();
                            for (std::uint32_t r = 1; r <= srcount; ++r) {
                                if (auto g = stbl->goto_record(r); !g) continue;
                                if (stbl->is_deleted()) continue;
                                if (sub->where && !evalw(*sub->where)) continue;
                                if (a.kind ==
                                    openads::sql::AggregateKind::CountStar) {
                                    ++cnt; continue;
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
                            switch (a.kind) {
                                case openads::sql::AggregateKind::CountStar:
                                case openads::sql::AggregateKind::Count:
                                    scalar_num = static_cast<double>(cnt); break;
                                case openads::sql::AggregateKind::Sum:
                                    scalar_num = sum; break;
                                case openads::sql::AggregateKind::Avg:
                                    scalar_num = cnt
                                        ? sum / static_cast<double>(cnt)
                                        : 0.0; break;
                                case openads::sql::AggregateKind::Min:
                                    scalar_num = cnt ? minv : 0.0; break;
                                case openads::sql::AggregateKind::Max:
                                    scalar_num = cnt ? maxv : 0.0; break;
                            }
                            found = true;
                            char tmp[64];
                            std::snprintf(tmp, sizeof(tmp),
                                          "%.17g", scalar_num);
                            scalar_str = tmp;
                        } else if (sub->projection.size() == 1 &&
                                   sub->aggregates.empty()) {
                            std::int32_t scol =
                                stbl->field_index(sub->projection[0]);
                            if (scol < 0) {
                                c->close_table(sh.value()); return false;
                            }
                            for (std::uint32_t r = 1; r <= srcount; ++r) {
                                if (auto g = stbl->goto_record(r); !g) continue;
                                if (stbl->is_deleted()) continue;
                                if (sub->where && !evalw(*sub->where)) continue;
                                auto v = stbl->read_field(
                                    static_cast<std::uint16_t>(scol));
                                if (!v) continue;
                                scalar_str = v.value().as_string;
                                scalar_num = v.value().as_double;
                                while (!scalar_str.empty() &&
                                       scalar_str.back() == ' ')
                                    scalar_str.pop_back();
                                found = true;
                                break;
                            }
                        } else {
                            c->close_table(sh.value());
                            return false;
                        }
                        c->close_table(sh.value());
                        if (!found) return false;
                        auto ov = outer_tbl->read_field(outer_field);
                        if (!ov) return false;
                        int cmp = 0;
                        if (outer_is_numeric_local) {
                            double d = ov.value().as_double;
                            if      (d < scalar_num) cmp = -1;
                            else if (d > scalar_num) cmp =  1;
                        } else {
                            std::string os = ov.value().as_string;
                            while (!os.empty() && os.back() == ' ')
                                os.pop_back();
                            cmp = os.compare(scalar_str);
                        }
                        switch (op_local) {
                            case openads::sql::WhereOp::Eq: return cmp == 0;
                            case openads::sql::WhereOp::Ne: return cmp != 0;
                            case openads::sql::WhereOp::Lt: return cmp <  0;
                            case openads::sql::WhereOp::Gt: return cmp >  0;
                            case openads::sql::WhereOp::Le: return cmp <= 0;
                            case openads::sql::WhereOp::Ge: return cmp >= 0;
                            default: return false;
                        }
                    }};
                }
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
                auto maybe_lower = [&](std::string s) {
                    if (!term.nocase) return s;
                    for (auto& ch : s) {
                        if (ch >= 'A' && ch <= 'Z')
                            ch = static_cast<char>(ch + 32);
                    }
                    return s;
                };
                if (term.op == openads::sql::WhereOp::Between) {
                    if (term.is_numeric) {
                        double d = v.value().as_double;
                        return d >= term.number && d <= term.number2;
                    }
                    auto sv = maybe_lower(v.value().as_string);
                    return sv.compare(term.literal)  >= 0 &&
                           sv.compare(term.literal2) <= 0;
                }
                if (term.op == openads::sql::WhereOp::Like) {
                    auto sv = maybe_lower(v.value().as_string);
                    while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                    return sql_like_match(sv, term.literal);
                }
                if (term.op == openads::sql::WhereOp::IsNull ||
                    term.op == openads::sql::WhereOp::IsNotNull) {
                    // M10.44 / M11.6 — prefer the VFP NULL bitmap
                    // when the field is nullable; otherwise treat
                    // an all-blanks character cell as NULL.
                    bool null_ish = t.is_field_null(term.field_index);
                    if (!null_ish) {
                        auto sv = v.value().as_string;
                        while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                        null_ish = sv.empty();
                    }
                    return term.op == openads::sql::WhereOp::IsNull
                        ? null_ish : !null_ish;
                }
                int cmp = 0;
                if (term.is_numeric) {
                    double d = v.value().as_double;
                    if      (d < term.number) cmp = -1;
                    else if (d > term.number) cmp =  1;
                } else {
                    cmp = maybe_lower(v.value().as_string).compare(term.literal);
                }
                switch (term.op) {
                    case openads::sql::WhereOp::Eq: return cmp == 0;
                    case openads::sql::WhereOp::Ne: return cmp != 0;
                    case openads::sql::WhereOp::Lt: return cmp <  0;
                    case openads::sql::WhereOp::Gt: return cmp >  0;
                    case openads::sql::WhereOp::Le: return cmp <= 0;
                    case openads::sql::WhereOp::Ge: return cmp >= 0;
                    case openads::sql::WhereOp::Contains: return true;
                    default: return false;
                }
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
        // M10.6 / M10.37 — ORDER BY one column, with cascading
        // additional columns for ties (M10.37).
        struct SortKey {
            std::uint16_t field_index;
            bool          descending;
            bool          numeric;
        };
        std::vector<SortKey> sks;
        auto add_sort_key = [&](const openads::sql::OrderBy& ob)
            -> openads::util::Result<std::monostate>
        {
            std::int32_t fidx = tbl->field_index(ob.column);
            if (fidx < 0) {
                return openads::util::Error{
                    openads::AE_COLUMN_NOT_FOUND, 0,
                    ob.column.c_str(), ""};
            }
            const auto& fd = tbl->field_descriptor(
                static_cast<std::uint16_t>(fidx));
            SortKey k;
            k.field_index = static_cast<std::uint16_t>(fidx);
            k.descending  = ob.descending;
            k.numeric =
                fd.type == openads::drivers::DbfFieldType::Numeric ||
                fd.type == openads::drivers::DbfFieldType::Float   ||
                fd.type == openads::drivers::DbfFieldType::Integer ||
                fd.type == openads::drivers::DbfFieldType::Currency||
                fd.type == openads::drivers::DbfFieldType::Double;
            sks.push_back(k);
            return std::monostate{};
        };
        if (auto r = add_sort_key(*parsed.value().order_by); !r)
            return fail(r.error());
        for (auto& ob : parsed.value().order_by_extra) {
            if (auto r = add_sort_key(ob); !r) return fail(r.error());
        }

        std::vector<std::uint32_t> matched;
        std::uint32_t rcount = tbl->record_count();
        for (std::uint32_t r = 1; r <= rcount; ++r) {
            if (auto g = tbl->goto_record(r); !g) continue;
            if (tbl->is_deleted()) continue;
            if (!tbl->passes_filter()) continue;
            matched.push_back(r);
        }

        struct Row {
            std::uint32_t              recno;
            std::vector<std::string>   s;
            std::vector<double>        d;
        };
        std::vector<Row> rows;
        rows.reserve(matched.size());
        for (auto r : matched) {
            (void)tbl->goto_record(r);
            Row row;
            row.recno = r;
            row.s.resize(sks.size());
            row.d.resize(sks.size());
            for (std::size_t i = 0; i < sks.size(); ++i) {
                auto v = tbl->read_field(sks[i].field_index);
                if (v) {
                    row.s[i] = v.value().as_string;
                    row.d[i] = v.value().as_double;
                }
            }
            rows.push_back(std::move(row));
        }
        std::stable_sort(rows.begin(), rows.end(),
            [&](const Row& a, const Row& b) {
                for (std::size_t i = 0; i < sks.size(); ++i) {
                    bool less, equal;
                    if (sks[i].numeric) {
                        less  = a.d[i] <  b.d[i];
                        equal = a.d[i] == b.d[i];
                    } else {
                        less  = a.s[i] <  b.s[i];
                        equal = a.s[i] == b.s[i];
                    }
                    if (equal) continue;
                    return sks[i].descending ? !less : less;
                }
                return false;
            });
        std::vector<std::uint32_t> seq;
        seq.reserve(rows.size());
        for (auto& row : rows) seq.push_back(row.recno);
        tbl->clear_filter();
        tbl->set_recno_sequence(std::move(seq));
    }

    // M10.31 — DISTINCT. M10.32 — LIMIT [OFFSET]. Both operate on the
    // post-WHERE / post-ORDER-BY traversal sequence; if neither
    // ORDER BY nor a recno_sequence is present yet, walk the
    // filtered cursor to materialise one first.
    bool need_seq_post = parsed.value().distinct ||
                         parsed.value().limit  >= 0 ||
                         parsed.value().offset > 0;
    if (need_seq_post) {
        std::vector<std::uint32_t> seq;
        if (tbl->has_recno_sequence()) {
            seq = tbl->recno_sequence();
        } else {
            std::uint32_t rcount = tbl->record_count();
            seq.reserve(rcount);
            for (std::uint32_t r = 1; r <= rcount; ++r) {
                if (auto g = tbl->goto_record(r); !g) continue;
                if (tbl->is_deleted()) continue;
                if (!tbl->passes_filter()) continue;
                seq.push_back(r);
            }
        }
        if (parsed.value().distinct) {
            std::vector<std::uint16_t> proj_indices;
            if (parsed.value().projection.empty()) {
                std::uint16_t nf = tbl->field_count();
                proj_indices.reserve(nf);
                for (std::uint16_t i = 0; i < nf; ++i) proj_indices.push_back(i);
            } else {
                for (auto& cn : parsed.value().projection) {
                    std::int32_t fi = tbl->field_index(cn);
                    if (fi < 0) return fail(openads::AE_COLUMN_NOT_FOUND,
                                            cn.c_str());
                    proj_indices.push_back(static_cast<std::uint16_t>(fi));
                }
            }
            std::unordered_set<std::string> seen;
            std::vector<std::uint32_t> dedup;
            dedup.reserve(seq.size());
            for (auto r : seq) {
                if (auto g = tbl->goto_record(r); !g) continue;
                std::string key;
                for (auto fi : proj_indices) {
                    auto v = tbl->read_field(fi);
                    if (v) key.append(v.value().as_string);
                    key.push_back('\x1f');
                }
                if (seen.insert(std::move(key)).second) dedup.push_back(r);
            }
            seq = std::move(dedup);
        }
        std::int64_t off = parsed.value().offset > 0 ? parsed.value().offset : 0;
        if (off > static_cast<std::int64_t>(seq.size())) {
            seq.clear();
        } else if (off > 0) {
            seq.erase(seq.begin(), seq.begin() +
                static_cast<std::vector<std::uint32_t>::difference_type>(off));
        }
        if (parsed.value().limit >= 0 &&
            static_cast<std::size_t>(parsed.value().limit) < seq.size()) {
            seq.resize(static_cast<std::size_t>(parsed.value().limit));
        }
        tbl->clear_filter();
        tbl->set_recno_sequence(std::move(seq));
    }

    // M10.38 — projection contains a CASE expression. Materialise the
    // post-WHERE / post-ORDER-BY / post-DISTINCT / post-LIMIT row set
    // into a temp DBF whose schema mirrors the projection list (CASE
    // items become C(30); regular columns preserve source type +
    // length), evaluating each row's CASE branches inline.
    auto starts_with = [](const std::string& s, const char* pre) {
        std::size_t L = std::strlen(pre);
        return s.size() >= L && std::memcmp(s.data(), pre, L) == 0;
    };
    bool has_synth = false;
    for (auto& p : parsed.value().projection) {
        if (starts_with(p, "$CASE_") || starts_with(p, "$FN_") ||
            starts_with(p, "$ARITH_") || starts_with(p, "$WIN_")) {
            has_synth = true; break;
        }
    }
    if (has_synth) {
        struct OutCol {
            std::string  name;
            char         raw_type = 'C';
            std::uint8_t length   = 0;
            std::int32_t src_field = -1;
            std::int32_t case_idx  = -1;
            std::int32_t fn_idx    = -1;
            std::int32_t arith_idx = -1;
            std::int32_t arith_lhs_field = -1;
            std::int32_t arith_rhs_field = -1;
            std::int32_t win_idx   = -1;
        };
        std::vector<OutCol> outs;
        outs.reserve(parsed.value().projection.size());
        for (std::size_t i = 0; i < parsed.value().projection.size(); ++i) {
            const auto& p = parsed.value().projection[i];
            OutCol o;
            if (starts_with(p, "$CASE_")) {
                std::size_t idx = std::stoul(p.substr(6));
                o.case_idx = static_cast<std::int32_t>(idx);
                const auto& ce = parsed.value().case_items[idx];
                if (!ce.alias.empty()) o.name = ce.alias;
                else {
                    char nm[16];
                    std::snprintf(nm, sizeof(nm), "CASE%zu", idx + 1);
                    o.name = nm;
                }
                o.raw_type = 'C';
                o.length   = 30;
            } else if (starts_with(p, "$FN_")) {
                std::size_t idx = std::stoul(p.substr(4));
                o.fn_idx = static_cast<std::int32_t>(idx);
                const auto& fc = parsed.value().fn_items[idx];
                using K = openads::sql::ScalarFnKind;
                bool single_col = (fc.kind == K::Upper ||
                                   fc.kind == K::Lower ||
                                   fc.kind == K::Len   ||
                                   fc.kind == K::Trim  ||
                                   fc.kind == K::Ltrim ||
                                   fc.kind == K::Rtrim);
                if (single_col) {
                    std::int32_t fi = tbl->field_index(fc.column);
                    if (fi < 0) return fail(openads::AE_COLUMN_NOT_FOUND,
                                            fc.column.c_str());
                    o.src_field = fi;
                    if (!fc.alias.empty()) o.name = fc.alias;
                    else o.name = fc.column;
                    o.raw_type = 'C';
                    if (fc.kind == K::Len) {
                        o.length = 10;
                    } else {
                        const auto& fd = tbl->field_descriptor(
                            static_cast<std::uint16_t>(fi));
                        o.length = fd.length ? fd.length : 30;
                    }
                } else {
                    // M10.43 / M10.45 — multi-arg fns. Width = generous
                    // default; alias drives the column name; no
                    // pre-resolved src_field (per-arg lookups happen
                    // at row-eval time).
                    if (!fc.alias.empty()) o.name = fc.alias;
                    else {
                        char nm[16];
                        std::snprintf(nm, sizeof(nm), "EXPR%zu", idx + 1);
                        o.name = nm;
                    }
                    o.raw_type = 'C';
                    o.length   = (fc.kind == K::DateAdd) ? 8
                               : (fc.kind == K::DateDiff) ? 12
                               : 64;
                }
            } else if (starts_with(p, "$WIN_")) {
                std::size_t idx = std::stoul(p.substr(5));
                o.win_idx = static_cast<std::int32_t>(idx);
                const auto& wf = parsed.value().window_items[idx];
                if (!wf.alias.empty()) o.name = wf.alias;
                else {
                    char nm[16];
                    std::snprintf(nm, sizeof(nm), "RN%zu", idx + 1);
                    o.name = nm;
                }
                o.raw_type = 'C';
                o.length   = 10;
            } else if (starts_with(p, "$ARITH_")) {
                std::size_t idx = std::stoul(p.substr(7));
                o.arith_idx = static_cast<std::int32_t>(idx);
                const auto& ae = parsed.value().arith_items[idx];
                std::int32_t lhs = tbl->field_index(ae.lhs_column);
                if (lhs < 0) return fail(openads::AE_COLUMN_NOT_FOUND,
                                         ae.lhs_column.c_str());
                o.arith_lhs_field = lhs;
                if (!ae.rhs_is_literal) {
                    std::int32_t rhs = tbl->field_index(ae.rhs_column);
                    if (rhs < 0) return fail(openads::AE_COLUMN_NOT_FOUND,
                                             ae.rhs_column.c_str());
                    o.arith_rhs_field = rhs;
                }
                if (!ae.alias.empty()) o.name = ae.alias;
                else {
                    char nm[16];
                    std::snprintf(nm, sizeof(nm), "EXPR%zu", idx + 1);
                    o.name = nm;
                }
                o.raw_type = 'C';
                o.length   = 30;
            } else {
                std::int32_t fi = tbl->field_index(p);
                if (fi < 0) return fail(openads::AE_COLUMN_NOT_FOUND,
                                        p.c_str());
                const auto& fd = tbl->field_descriptor(
                    static_cast<std::uint16_t>(fi));
                o.name      = fd.name;
                o.raw_type  = static_cast<char>(fd.raw_type);
                o.length    = fd.length;
                o.src_field = fi;
            }
            outs.push_back(std::move(o));
        }

        // Compile each CASE branch's condition against tbl.
        using CondPred = std::function<bool(openads::engine::Table&)>;
        std::function<openads::util::Result<CondPred>(
            const openads::sql::WhereExpr&)> compile_cond;
        compile_cond = [&](const openads::sql::WhereExpr& node)
            -> openads::util::Result<CondPred>
        {
            using K = openads::sql::WhereExpr::Kind;
            if (node.kind == K::And || node.kind == K::Or) {
                std::vector<CondPred> ks;
                for (auto& cn : node.children) {
                    auto r = compile_cond(*cn);
                    if (!r) return r.error();
                    ks.push_back(std::move(r).value());
                }
                bool is_and = (node.kind == K::And);
                return CondPred{[ks = std::move(ks), is_and]
                                (openads::engine::Table& t) {
                    if (is_and) {
                        for (auto& k : ks) if (!k(t)) return false;
                        return true;
                    }
                    for (auto& k : ks) if (k(t)) return true;
                    return false;
                }};
            }
            if (node.kind == K::Not) {
                auto inner = compile_cond(*node.child);
                if (!inner) return inner.error();
                return CondPred{[p = std::move(inner).value()]
                                (openads::engine::Table& t)
                                { return !p(t); }};
            }
            if (node.kind != K::Cmp) {
                return openads::util::Error{
                    openads::AE_FUNCTION_NOT_AVAILABLE, 0,
                    "CASE WHEN supports Cmp / AND / OR / NOT only", ""};
            }
            const auto& w = node.cmp;
            std::int32_t fi = tbl->field_index(w.column);
            if (fi < 0) return openads::util::Error{
                openads::AE_COLUMN_NOT_FOUND, 0,
                w.column.c_str(), ""};
            std::uint16_t f = static_cast<std::uint16_t>(fi);
            openads::sql::WhereOp op = w.op;
            std::string lit = w.literal;
            std::string lit2 = w.literal2;
            bool is_num = w.is_numeric;
            double num  = w.number;
            double num2 = w.number2;
            return CondPred{[f, op, lit, lit2, is_num, num, num2]
                            (openads::engine::Table& t) {
                auto v = t.read_field(f);
                if (!v) return false;
                if (op == openads::sql::WhereOp::Between) {
                    if (is_num) {
                        double d = v.value().as_double;
                        return d >= num && d <= num2;
                    }
                    auto& sv = v.value().as_string;
                    return sv.compare(lit)  >= 0 &&
                           sv.compare(lit2) <= 0;
                }
                if (op == openads::sql::WhereOp::Like) {
                    auto sv = v.value().as_string;
                    while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                    return sql_like_match(sv, lit);
                }
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
                    default: return false;
                }
            }};
        };
        struct CompiledCase {
            std::vector<CondPred>           branch_preds;
            std::vector<std::string>        branch_values;
            bool                            has_else = false;
            std::string                     else_value;
        };
        std::vector<CompiledCase> ccases;
        ccases.reserve(parsed.value().case_items.size());
        for (auto& ce : parsed.value().case_items) {
            CompiledCase cc;
            for (auto& br : ce.branches) {
                auto p = compile_cond(*br.cond);
                if (!p) return fail(p.error());
                cc.branch_preds.push_back(std::move(p).value());
                cc.branch_values.push_back(br.then_value.text);
            }
            cc.has_else  = ce.has_else;
            cc.else_value = ce.has_else ? ce.else_value.text : std::string();
            ccases.push_back(std::move(cc));
        }

        // Build the row list — honor any installed recno_sequence,
        // else walk the filtered cursor.
        std::vector<std::uint32_t> walk_seq;
        if (tbl->has_recno_sequence()) {
            walk_seq = tbl->recno_sequence();
        } else {
            std::uint32_t rcount = tbl->record_count();
            for (std::uint32_t r = 1; r <= rcount; ++r) {
                if (auto g = tbl->goto_record(r); !g) continue;
                if (tbl->is_deleted()) continue;
                if (!tbl->passes_filter()) continue;
                walk_seq.push_back(r);
            }
        }

        // M10.49 / M10.50 — pre-compute window values per row when
        // any window items appear in the projection. For each
        // window slot, group rows by PARTITION BY key, sort within
        // each group by ORDER BY (if any), then assign values per
        // kind (ROW_NUMBER / RANK / DENSE_RANK).
        std::unordered_map<std::uint32_t, std::vector<std::string>>
            window_vals;
        if (!parsed.value().window_items.empty()) {
            window_vals.reserve(walk_seq.size());
            for (std::size_t wi = 0;
                 wi < parsed.value().window_items.size(); ++wi) {
                const auto& wf = parsed.value().window_items[wi];
                struct Entry {
                    std::uint32_t recno;
                    std::string   pkey;
                    std::string   okey;
                };
                std::vector<Entry> ents;
                ents.reserve(walk_seq.size());
                for (auto r : walk_seq) {
                    if (auto g = tbl->goto_record(r); !g) continue;
                    Entry e;
                    e.recno = r;
                    for (auto& pc : wf.partition_by) {
                        std::int32_t fi = tbl->field_index(pc);
                        if (fi >= 0) {
                            auto v = tbl->read_field(
                                static_cast<std::uint16_t>(fi));
                            if (v) e.pkey += v.value().as_string;
                        }
                        e.pkey.push_back('\x1f');
                    }
                    if (wf.order_by) {
                        std::int32_t fi =
                            tbl->field_index(wf.order_by->column);
                        if (fi >= 0) {
                            auto v = tbl->read_field(
                                static_cast<std::uint16_t>(fi));
                            if (v) e.okey = v.value().as_string;
                        }
                    }
                    ents.push_back(std::move(e));
                }
                std::stable_sort(ents.begin(), ents.end(),
                    [&](const Entry& a, const Entry& b) {
                        if (a.pkey != b.pkey) return a.pkey < b.pkey;
                        if (wf.order_by) {
                            return wf.order_by->descending
                                ? a.okey > b.okey
                                : a.okey < b.okey;
                        }
                        return false;
                    });
                std::string prev_pk;
                std::string prev_ok;
                bool prev_ok_set = false;
                std::uint32_t pos       = 0;
                std::uint32_t rank_now  = 0;
                std::uint32_t dense_now = 0;
                for (auto& e : ents) {
                    if (e.pkey != prev_pk) {
                        pos = 0; rank_now = 0; dense_now = 0;
                        prev_ok_set = false;
                        prev_pk = e.pkey;
                    }
                    ++pos;
                    bool tied = wf.order_by && prev_ok_set &&
                                e.okey == prev_ok;
                    if (!tied) {
                        rank_now = pos;
                        ++dense_now;
                    }
                    prev_ok = e.okey;
                    prev_ok_set = true;
                    std::string val;
                    switch (wf.kind) {
                        case openads::sql::WindowFnKind::RowNumber:
                            val = std::to_string(pos); break;
                        case openads::sql::WindowFnKind::Rank:
                            val = std::to_string(rank_now); break;
                        case openads::sql::WindowFnKind::DenseRank:
                            val = std::to_string(dense_now); break;
                    }
                    auto& slot = window_vals[e.recno];
                    if (slot.size() <
                        parsed.value().window_items.size()) {
                        slot.resize(
                            parsed.value().window_items.size());
                    }
                    slot[wi] = std::move(val);
                }
            }
        }

        // Build temp DBF.
        namespace fs = std::filesystem;
        char nb[64];
        std::snprintf(nb, sizeof(nb), "_case_%llx.dbf",
                      static_cast<unsigned long long>(
                          openads::platform::monotonic_nanos()));
        fs::path dbf = fs::path(c->data_dir()) / nb;
        std::vector<std::uint8_t> file;
        std::array<std::uint8_t, 32> hdr{};
        hdr[0] = 0x03;
        std::uint16_t hl = static_cast<std::uint16_t>(
            32 + 32 * outs.size() + 1);
        std::uint32_t rl = 1;
        for (auto& o : outs) rl += o.length;
        hdr[8]  = static_cast<std::uint8_t>( hl       & 0xFFu);
        hdr[9]  = static_cast<std::uint8_t>((hl >> 8) & 0xFFu);
        hdr[10] = static_cast<std::uint8_t>( rl       & 0xFFu);
        hdr[11] = static_cast<std::uint8_t>((rl >> 8) & 0xFFu);
        file.insert(file.end(), hdr.begin(), hdr.end());
        for (auto& o : outs) {
            std::array<std::uint8_t, 32> fd{};
            std::strncpy(reinterpret_cast<char*>(fd.data()),
                         o.name.c_str(), 11);
            fd[11] = static_cast<std::uint8_t>(o.raw_type);
            fd[16] = o.length;
            file.insert(file.end(), fd.begin(), fd.end());
        }
        file.push_back(0x0D);

        std::uint32_t emitted = 0;
        auto trim_left = [](std::string s) {
            std::size_t i = 0;
            while (i < s.size() && s[i] == ' ') ++i;
            return s.substr(i);
        };
        auto trim_right = [](std::string s) {
            while (!s.empty() && s.back() == ' ') s.pop_back();
            return s;
        };
        auto trim_both = [&](std::string s) {
            return trim_left(trim_right(std::move(s)));
        };
        for (std::uint32_t r : walk_seq) {
            if (auto g = tbl->goto_record(r); !g) continue;
            file.push_back(' ');
            for (auto& o : outs) {
                std::string val;
                bool from_synth = false;
                if (o.case_idx >= 0) {
                    from_synth = true;
                    const auto& cc =
                        ccases[static_cast<std::size_t>(o.case_idx)];
                    val = cc.has_else ? cc.else_value : "";
                    for (std::size_t bi = 0; bi < cc.branch_preds.size(); ++bi) {
                        if (cc.branch_preds[bi](*tbl)) {
                            val = cc.branch_values[bi];
                            break;
                        }
                    }
                } else if (o.fn_idx >= 0) {
                    from_synth = true;
                    const auto& fc = parsed.value().fn_items[
                        static_cast<std::size_t>(o.fn_idx)];
                    std::string raw;
                    if (o.src_field >= 0) {
                        auto v = tbl->read_field(
                            static_cast<std::uint16_t>(o.src_field));
                        if (v) raw = v.value().as_string;
                    }
                    using K = openads::sql::ScalarFnKind;
                    switch (fc.kind) {
                        case K::Upper:
                            for (auto& ch : raw)
                                ch = static_cast<char>(std::toupper(
                                    static_cast<unsigned char>(ch)));
                            val = std::move(raw);
                            break;
                        case K::Lower:
                            for (auto& ch : raw)
                                ch = static_cast<char>(std::tolower(
                                    static_cast<unsigned char>(ch)));
                            val = std::move(raw);
                            break;
                        case K::Len: {
                            std::string trimmed = trim_right(std::move(raw));
                            char buf[16];
                            std::snprintf(buf, sizeof(buf), "%zu",
                                          trimmed.size());
                            val = buf;
                            break;
                        }
                        case K::Trim:  val = trim_both(std::move(raw));  break;
                        case K::Ltrim: val = trim_left(std::move(raw));  break;
                        case K::Rtrim: val = trim_right(std::move(raw)); break;
                        case K::Substr:
                        case K::Concat:
                        case K::Replace:
                        case K::DateDiff:
                        case K::DateAdd:
                        case K::NullIf:
                        case K::Coalesce:
                        case K::IfNull: {
                            // M10.43 / M10.45 — multi-arg fns. Resolve
                            // each arg as either a column read (with
                            // trailing-space trim for Char-typed slots)
                            // or the parsed literal.
                            auto arg_str = [&](const openads::sql::ScalarFnArg& a)
                                -> std::string {
                                if (!a.is_column) return a.text;
                                std::int32_t fi = tbl->field_index(a.column);
                                if (fi < 0) return std::string();
                                auto fv = tbl->read_field(
                                    static_cast<std::uint16_t>(fi));
                                if (!fv) return std::string();
                                std::string s = fv.value().as_string;
                                while (!s.empty() && s.back() == ' ')
                                    s.pop_back();
                                return s;
                            };
                            auto arg_num = [&](const openads::sql::ScalarFnArg& a)
                                -> double {
                                if (!a.is_column) return a.number;
                                std::int32_t fi = tbl->field_index(a.column);
                                if (fi < 0) return 0.0;
                                auto fv = tbl->read_field(
                                    static_cast<std::uint16_t>(fi));
                                return fv ? fv.value().as_double : 0.0;
                            };
                            if (fc.kind == K::Substr && fc.args.size() >= 2) {
                                std::string src = arg_str(fc.args[0]);
                                long start = static_cast<long>(
                                    arg_num(fc.args[1]));      // 1-based
                                long len = (fc.args.size() >= 3)
                                    ? static_cast<long>(arg_num(fc.args[2]))
                                    : static_cast<long>(src.size());
                                if (start < 1) start = 1;
                                std::size_t s0 = static_cast<std::size_t>(start - 1);
                                if (s0 >= src.size()) val.clear();
                                else {
                                    std::size_t take =
                                        std::min<std::size_t>(
                                            len < 0 ? 0 : (std::size_t)len,
                                            src.size() - s0);
                                    val = src.substr(s0, take);
                                }
                            } else if (fc.kind == K::Concat) {
                                for (auto& a : fc.args) val += arg_str(a);
                            } else if (fc.kind == K::Replace &&
                                       fc.args.size() == 3) {
                                std::string src = arg_str(fc.args[0]);
                                std::string oldp = arg_str(fc.args[1]);
                                std::string newp = arg_str(fc.args[2]);
                                if (!oldp.empty()) {
                                    std::size_t i = 0;
                                    while ((i = src.find(oldp, i)) !=
                                           std::string::npos) {
                                        src.replace(i, oldp.size(), newp);
                                        i += newp.size();
                                    }
                                }
                                val = std::move(src);
                            } else if (fc.kind == K::DateDiff &&
                                       fc.args.size() == 2) {
                                // M10.45 — DATEDIFF on YYYYMMDD strings:
                                // returns days_a - days_b via Julian day.
                                auto julian = [](const std::string& s) -> long {
                                    if (s.size() < 8) return 0;
                                    int y = std::atoi(s.substr(0, 4).c_str());
                                    int m = std::atoi(s.substr(4, 2).c_str());
                                    int d = std::atoi(s.substr(6, 2).c_str());
                                    long a = (14 - m) / 12;
                                    long y2 = y + 4800 - a;
                                    long m2 = m + 12 * a - 3;
                                    return d + (153 * m2 + 2) / 5 + 365 * y2 +
                                           y2 / 4 - y2 / 100 + y2 / 400 - 32045;
                                };
                                long ja = julian(arg_str(fc.args[0]));
                                long jb = julian(arg_str(fc.args[1]));
                                char buf[32];
                                std::snprintf(buf, sizeof(buf), "%ld",
                                              ja - jb);
                                val = buf;
                            } else if (fc.kind == K::NullIf &&
                                       fc.args.size() == 2) {
                                // M10.53 — NULLIF(a, b): NULL if
                                // equal, else a. Empty string =
                                // NULL by convention.
                                std::string a = arg_str(fc.args[0]);
                                std::string b = arg_str(fc.args[1]);
                                val = (a == b) ? std::string() : a;
                            } else if (fc.kind == K::Coalesce) {
                                // M10.53 — first non-empty arg wins.
                                for (auto& a : fc.args) {
                                    auto cs = arg_str(a);
                                    if (!cs.empty()) {
                                        val = std::move(cs); break;
                                    }
                                }
                            } else if (fc.kind == K::IfNull &&
                                       fc.args.size() == 2) {
                                // M10.53 — IFNULL(expr, default).
                                std::string a = arg_str(fc.args[0]);
                                val = a.empty() ? arg_str(fc.args[1]) : a;
                            } else if (fc.kind == K::DateAdd &&
                                       fc.args.size() == 2) {
                                // M10.45 — add N days to YYYYMMDD.
                                std::string ds = arg_str(fc.args[0]);
                                if (ds.size() < 8) { val = ds; break; }
                                int y = std::atoi(ds.substr(0, 4).c_str());
                                int mo_in = std::atoi(ds.substr(4, 2).c_str());
                                int d = std::atoi(ds.substr(6, 2).c_str());
                                long n = static_cast<long>(arg_num(fc.args[1]));
                                long aa = (14 - mo_in) / 12;
                                long y2 = y + 4800 - aa;
                                long m2 = mo_in + 12 * aa - 3;
                                long jdn = d + (153 * m2 + 2) / 5 + 365 * y2 +
                                           y2 / 4 - y2 / 100 + y2 / 400 - 32045;
                                jdn += n;
                                long la = jdn + 32044;
                                long b  = (4 * la + 3) / 146097;
                                long c2 = la - (146097 * b) / 4;
                                long d2 = (4 * c2 + 3) / 1461;
                                long e  = c2 - (1461 * d2) / 4;
                                long mn = (5 * e + 2) / 153;
                                int  day = static_cast<int>(
                                    e - (153 * mn + 2) / 5 + 1);
                                int  mo  = static_cast<int>(
                                    mn + 3 - 12 * (mn / 10));
                                int  yr  = static_cast<int>(
                                    100 * b + d2 - 4800 + mn / 10);
                                char buf[16];
                                std::snprintf(buf, sizeof(buf),
                                              "%04d%02d%02d", yr, mo, day);
                                val = buf;
                            } else {
                                val.clear();
                            }
                            break;
                        }
                    }
                } else if (o.win_idx >= 0) {
                    from_synth = true;
                    auto wit = window_vals.find(r);
                    if (wit != window_vals.end() &&
                        static_cast<std::size_t>(o.win_idx) <
                            wit->second.size() &&
                        !wit->second[
                            static_cast<std::size_t>(o.win_idx)].empty()) {
                        val = wit->second[
                            static_cast<std::size_t>(o.win_idx)];
                    } else {
                        char buf[16];
                        std::snprintf(buf, sizeof(buf), "%u",
                                      static_cast<unsigned>(emitted + 1));
                        val = buf;
                    }
                } else if (o.arith_idx >= 0) {
                    from_synth = true;
                    const auto& ae = parsed.value().arith_items[
                        static_cast<std::size_t>(o.arith_idx)];
                    auto lv = tbl->read_field(
                        static_cast<std::uint16_t>(o.arith_lhs_field));
                    double a = lv ? lv.value().as_double : 0.0;
                    double b = 0.0;
                    if (ae.rhs_is_literal) b = ae.rhs_number;
                    else {
                        auto rv = tbl->read_field(
                            static_cast<std::uint16_t>(o.arith_rhs_field));
                        b = rv ? rv.value().as_double : 0.0;
                    }
                    double res = 0.0;
                    using AO = openads::sql::ArithOp;
                    switch (ae.op) {
                        case AO::Add: res = a + b; break;
                        case AO::Sub: res = a - b; break;
                        case AO::Mul: res = a * b; break;
                        case AO::Div: res = (b != 0.0) ? a / b : 0.0; break;
                    }
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%g", res);
                    val = buf;
                }
                if (from_synth) {
                    if (val.size() > o.length) val.resize(o.length);
                    for (std::uint8_t k = 0; k < o.length; ++k) {
                        file.push_back(k < val.size()
                            ? static_cast<std::uint8_t>(val[k]) : ' ');
                    }
                } else {
                    auto v = tbl->read_field(
                        static_cast<std::uint16_t>(o.src_field));
                    std::string raw = v ? v.value().as_string : std::string();
                    for (std::uint8_t k = 0; k < o.length; ++k) {
                        file.push_back(k < raw.size()
                            ? static_cast<std::uint8_t>(raw[k]) : ' ');
                    }
                }
            }
            ++emitted;
        }
        file.push_back(0x1A);
        file[4] = static_cast<std::uint8_t>( emitted        & 0xFFu);
        file[5] = static_cast<std::uint8_t>((emitted >>  8) & 0xFFu);
        file[6] = static_cast<std::uint8_t>((emitted >> 16) & 0xFFu);
        file[7] = static_cast<std::uint8_t>((emitted >> 24) & 0xFFu);
        {
            std::ofstream out(dbf, std::ios::binary);
            if (!out) return fail(openads::AE_INTERNAL_ERROR,
                "case temp DBF open for write failed");
            out.write(reinterpret_cast<const char*>(file.data()),
                      static_cast<std::streamsize>(file.size()));
        }
        std::string rel = dbf.filename().string();
        auto cth = c->open_table(rel, openads::engine::TableType::Cdx,
                                 openads::engine::OpenMode::Read);
        if (!cth) return fail(cth.error());
        openads::engine::Table* ctbl = c->lookup_table(cth.value());
        if (!ctbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
        ADSHANDLE gh_case = s.registry.register_object(HandleKind::Table, ctbl);
        *phCursor = gh_case;
        return ok();
    }

    // M10.46 — when this query was a derived-table outer SELECT,
    // reuse the inner cursor's existing handle so the user-visible
    // cursor isn't a stale alias of an already-registered Table*.
    ADSHANDLE gh = (derived_cur != 0)
        ? derived_cur
        : s.registry.register_object(HandleKind::Table, tbl);

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
