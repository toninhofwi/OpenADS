#include "openads/ace.h"
#include "openads/error.h"

#include "abi/charset.h"
#include "abi/last_error.h"

#include "engine/index_expr.h"
#include "engine/table.h"
#include "session/connection.h"
#include "session/handle_registry.h"
#include "drivers/dbf_common.h"
#include "drivers/index_trait.h"
#include "drivers/ntx/ntx_index.h"
#include "drivers/cdx/cdx_index.h"
#include "sql/parser.h"

#include <algorithm>

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

    // Parse `NAME,Type,Len,Dec;NAME,Type,...` into per-field structs.
    struct FieldOut {
        std::string  name;
        char         type;
        std::uint8_t length;
        std::uint8_t dec;
    };
    std::vector<FieldOut> fields;
    std::string buf;
    for (std::size_t i = 0; i <= defs.size(); ++i) {
        char ch = (i < defs.size()) ? defs[i] : ';';
        if (ch == ';') {
            if (!buf.empty()) {
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
                    if (f.length == 0) f.length = 10;     // sensible default
                    fields.push_back(std::move(f));
                }
                buf.clear();
            }
        } else {
            buf.push_back(ch);
        }
    }
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
        if (t) (void)t->flush();
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
    // a no-op.
    Table* t = s.registry.lookup<Table>(hTable, HandleKind::Table);
    if (t != nullptr) (void)t->flush();
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
    *pusFields = t->field_count();
    return ok();
}

UNSIGNED32 AdsGetFieldName(ADSHANDLE hTable, UNSIGNED16 usFieldNum,
                           UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    if (usFieldNum == 0 || usFieldNum > t->field_count()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "field index out of range");
    }
    const auto& f = t->field_descriptor(usFieldNum - 1);
    openads::abi::copy_to_caller(pucBuf, pusLen, f.name);
    return ok();
}

UNSIGNED32 AdsGetFieldType(ADSHANDLE hTable, UNSIGNED8* pucField,
                           UNSIGNED16* pusType) {
    Table* t = get_table(hTable);
    if (!t || pusType == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
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
    if (!resolve_field_index(t, pucField, &idx)) {
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
    if (!resolve_field_index(t, pucField, &idx)) {
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
    if (!resolve_field_index(t, pucField, &idx)) {
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

UNSIGNED32 AdsGetMemoDataType(ADSHANDLE hTable, UNSIGNED8* /*pucField*/,
                              UNSIGNED16* pusType) {
    Table* t = get_table(hTable);
    if (!t || pusType == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    // Always report ADS_STRING — OpenADS' memo store is plain text;
    // binary memos land when an FPT block carries a binary type tag.
    *pusType = static_cast<UNSIGNED16>(ADS_STRING);
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

UNSIGNED32 AdsLockRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->lock_record_excl(ulRecord);
    if (!r) return fail(r.error());
    return ok();
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
    auto r = t->lock_table_excl();
    if (!r) return fail(r.error());
    return ok();
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

// Real-ACE 4-arg signature: opens an index FILE, registers one handle
// per tag, and writes the handles into ahIndex[] / *pu16ArrayLen.
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

    // Drop any prior bindings for this table.
    auto& m   = index_bindings();
    auto& act = active_binding_for();
    for (auto it = m.begin(); it != m.end(); ) {
        if (it->second.table == t) it = m.erase(it);
        else                       ++it;
    }
    act.erase(t);
    t->clear_order();

    // Enumerate tags. CDX exposes list_tags; NTX has only its single
    // tag, which open() reports via name().
    std::vector<std::string> tags;
    if (path_ends_with_ci(path, ".cdx")) {
        auto r = openads::drivers::cdx::CdxIndex::list_tags(path);
        if (!r) return fail(r.error());
        tags = std::move(r).value();
    }
    if (tags.empty()) {
        // NTX or empty CDX: open once via the legacy path.
        auto idx = make_index_for(path);
        if (auto r = idx->open(path, openads::drivers::IndexOpenMode::Shared); !r) {
            return fail(r.error());
        }
        std::string tag_name = idx->name();
        t->set_order(std::move(idx));
        ADSHANDLE h = next_index_handle();
        m[h] = IndexBinding{t, tag_name, nullptr};
        act[t] = h;
        ahIndex[0] = h;
        if (pu16ArrayLen != nullptr) *pu16ArrayLen = 1;
        return ok();
    }

    // CDX with one or more tags: open each by name. The first tag's
    // IIndex moves into Table::order_ (becomes default order); the
    // rest stay parked in their bindings until activate_binding swaps
    // them in.
    UNSIGNED16 cap = (pu16ArrayLen != nullptr && *pu16ArrayLen > 0)
                   ? *pu16ArrayLen : 1;
    UNSIGNED16 count = 0;
    t->clear_extra_index_views();
    for (const auto& name : tags) {
        if (count >= cap) break;
        auto sub = std::make_unique<openads::drivers::cdx::CdxIndex>();
        if (auto r = sub->open_named(path,
                          openads::drivers::IndexOpenMode::Shared,
                          name); !r) {
            return fail(r.error());
        }
        ADSHANDLE h = next_index_handle();
        if (count == 0) {
            // First tag becomes the active order on the table.
            t->set_order(std::move(sub));
            m[h] = IndexBinding{t, name, nullptr};
            act[t] = h;
        } else {
            // Parked: register as an extra view so set_field syncs it
            // even when it isn't the active order.
            openads::drivers::IIndex* raw = sub.get();
            m[h] = IndexBinding{t, name, std::move(sub)};
            t->register_extra_index_view(raw);
        }
        ahIndex[count++] = h;
    }
    if (pu16ArrayLen != nullptr) *pu16ArrayLen = count;
    return ok();
}

UNSIGNED32 AdsCloseIndex(ADSHANDLE hIndex) {
    auto& m = index_bindings();
    auto it = m.find(hIndex);
    if (it == m.end()) return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    if (it->second.table) it->second.table->clear_order();
    m.erase(it);
    return ok();
}

UNSIGNED32 AdsCloseAllIndexes(ADSHANDLE hTable) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto& m = index_bindings();
    for (auto it = m.begin(); it != m.end(); ) {
        if (it->second.table == t) it = m.erase(it);
        else                       ++it;
    }
    t->clear_order();
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
        m[h] = IndexBinding{t, tag, nullptr};
        act[t] = h;
    } else {
        t->register_extra_index_view(idx_view);
        m[h] = IndexBinding{t, tag, std::move(idx_owner)};
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

    auto& m = index_bindings();
    for (auto it = m.begin(); it != m.end(); ) {
        if (it->second.table == t) it = m.erase(it);
        else                       ++it;
    }
    t->set_order(std::move(idx));
    ADSHANDLE h = next_index_handle();
    m[h] = IndexBinding{t, tag};
    *phIndex = h;
    return ok();
}

UNSIGNED32 AdsDeleteIndex(ADSHANDLE hIndex) {
    return AdsCloseIndex(hIndex);
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

UNSIGNED32 AdsSetIndexDirection(ADSHANDLE /*hIndex*/, UNSIGNED16 /*usDir*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "AdsSetIndexDirection deferred");
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
    auto parsed = openads::sql::parse_select(sql);
    if (!parsed) return fail(parsed.error());

    auto th = c->open_table(parsed.value().table,
                            openads::engine::TableType::Cdx,
                            openads::engine::OpenMode::Read);
    if (!th) return fail(th.error());
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    openads::engine::Table* tbl = c->lookup_table(th.value());
    if (!tbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");

    // Compile WHERE clauses (AND-joined) into a row predicate.
    if (!parsed.value().where.empty()) {
        struct Term {
            std::uint16_t         field_index;
            openads::sql::WhereOp op;
            std::string           literal;
        };
        std::vector<Term> terms;
        terms.reserve(parsed.value().where.size());
        for (const auto& w : parsed.value().where) {
            std::int32_t fidx = tbl->field_index(w.column);
            if (fidx < 0) {
                return fail(openads::AE_COLUMN_NOT_FOUND, w.column.c_str());
            }
            terms.push_back({static_cast<std::uint16_t>(fidx), w.op, w.literal});
        }
        tbl->set_filter([terms = std::move(terms)](openads::engine::Table& t) {
            for (const auto& term : terms) {
                auto v = t.read_field(term.field_index);
                if (!v) return false;
                int cmp = v.value().as_string.compare(term.literal);
                bool ok = false;
                switch (term.op) {
                    case openads::sql::WhereOp::Eq: ok = (cmp == 0); break;
                    case openads::sql::WhereOp::Ne: ok = (cmp != 0); break;
                    case openads::sql::WhereOp::Lt: ok = (cmp <  0); break;
                    case openads::sql::WhereOp::Gt: ok = (cmp >  0); break;
                    case openads::sql::WhereOp::Le: ok = (cmp <= 0); break;
                    case openads::sql::WhereOp::Ge: ok = (cmp >= 0); break;
                }
                if (!ok) return false;
            }
            return true;
        });
    }

    ADSHANDLE gh = s.registry.register_object(HandleKind::Table, tbl);
    *phCursor = gh;
    return ok();
}

} // extern "C"
