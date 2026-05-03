#include "openads/ace.h"
#include "openads/error.h"

#include "abi/charset.h"
#include "abi/last_error.h"

#include "engine/table.h"
#include "session/connection.h"
#include "session/handle_registry.h"
#include "drivers/dbf_common.h"
#include "drivers/index_trait.h"
#include "drivers/ntx/ntx_index.h"
#include "drivers/cdx/cdx_index.h"

#include <algorithm>

#include <cstring>
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
    switch (t) {
        case DbfFieldType::Character: return ADS_FIELD_TYPE_CHAR;
        case DbfFieldType::Numeric:
        case DbfFieldType::Float:     return ADS_FIELD_TYPE_NUMERIC;
        case DbfFieldType::Logical:   return ADS_FIELD_TYPE_LOGICAL;
        case DbfFieldType::Date:      return ADS_FIELD_TYPE_DATE;
        case DbfFieldType::DateTime:  return ADS_FIELD_TYPE_DATETIME;
        case DbfFieldType::Memo:      return ADS_FIELD_TYPE_MEMO;
        case DbfFieldType::Integer:   return ADS_FIELD_TYPE_INTEGER;
        case DbfFieldType::Currency:  return ADS_FIELD_TYPE_CURRENCY;
        case DbfFieldType::Double:    return ADS_FIELD_TYPE_DOUBLE;
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

Table* get_table(ADSHANDLE h) {
    auto& s = state();
    return s.registry.lookup<Table>(h, HandleKind::Table);
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

UNSIGNED32 AdsCloseTable(ADSHANDLE hTable) {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
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
    auto name = openads::abi::to_internal(pucField, 0);
    const auto* f = find_field(t, name);
    if (f == nullptr) return fail(openads::AE_COLUMN_NOT_FOUND, name.c_str());
    *pusType = map_field_type(f->type);
    return ok();
}

UNSIGNED32 AdsGetFieldLength(ADSHANDLE hTable, UNSIGNED8* pucField,
                             UNSIGNED32* pulLen) {
    Table* t = get_table(hTable);
    if (!t || pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto name = openads::abi::to_internal(pucField, 0);
    const auto* f = find_field(t, name);
    if (f == nullptr) return fail(openads::AE_COLUMN_NOT_FOUND, name.c_str());
    *pulLen = f->length;
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
    auto name = openads::abi::to_internal(pucField, 0);
    std::uint16_t idx = 0;
    bool found = false;
    for (std::uint16_t i = 0; i < t->field_count(); ++i) {
        if (t->field_descriptor(i).name == name) { idx = i; found = true; break; }
    }
    if (!found) return fail(openads::AE_COLUMN_NOT_FOUND, name.c_str());
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
    auto name = openads::abi::to_internal(pucField, 0);
    auto idx  = t->field_index(name);
    if (idx < 0) return fail(openads::AE_COLUMN_NOT_FOUND, name.c_str());
    std::string val(reinterpret_cast<const char*>(pucValue), ulLen);
    auto r = t->set_field(static_cast<std::uint16_t>(idx), val);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsSetLogical(ADSHANDLE hTable, UNSIGNED8* pucField,
                         UNSIGNED16 bValue) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto name = openads::abi::to_internal(pucField, 0);
    auto idx  = t->field_index(name);
    if (idx < 0) return fail(openads::AE_COLUMN_NOT_FOUND, name.c_str());
    auto r = t->set_field(static_cast<std::uint16_t>(idx), bValue != 0);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsSetDouble(ADSHANDLE hTable, UNSIGNED8* pucField,
                        double dValue) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto name = openads::abi::to_internal(pucField, 0);
    auto idx  = t->field_index(name);
    if (idx < 0) return fail(openads::AE_COLUMN_NOT_FOUND, name.c_str());
    auto r = t->set_field(static_cast<std::uint16_t>(idx), dValue);
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
struct IndexBinding { Table* table; std::string tag_name; };
std::unordered_map<ADSHANDLE, IndexBinding>& index_bindings() {
    static std::unordered_map<ADSHANDLE, IndexBinding> m;
    return m;
}

ADSHANDLE next_index_handle() {
    static std::uint64_t n = 0x40000000ULL;  // disjoint from table handles
    return ++n;
}

Table* table_for_index(ADSHANDLE hIndex) {
    auto it = index_bindings().find(hIndex);
    if (it == index_bindings().end()) return nullptr;
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

UNSIGNED32 AdsOpenIndex(ADSHANDLE hTable, UNSIGNED8* pucName,
                        ADSHANDLE* phIndex) {
    Table* t = get_table(hTable);
    if (!t || phIndex == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown table or null out");
    }
    auto path = openads::abi::to_internal(pucName, 0);
    auto idx = make_index_for(path);
    if (auto r = idx->open(path, openads::drivers::IndexOpenMode::Shared); !r) {
        return fail(r.error());
    }
    std::string tag_name = idx->name();

    // Drop any prior bindings for this table — set_order destroys the
    // previous order, which would leave older handles dangling.
    auto& m = index_bindings();
    for (auto it = m.begin(); it != m.end(); ) {
        if (it->second.table == t) it = m.erase(it);
        else                       ++it;
    }

    t->set_order(std::move(idx));
    ADSHANDLE h = next_index_handle();
    m[h] = IndexBinding{t, tag_name};
    *phIndex = h;
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
    *pusCount = t->order() ? 1 : 0;
    return ok();
}

UNSIGNED32 AdsGetIndexHandle(ADSHANDLE hTable, UNSIGNED8* pucName,
                             ADSHANDLE* phIndex) {
    Table* t = get_table(hTable);
    if (!t || phIndex == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    auto name = openads::abi::to_internal(pucName, 0);
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
    Table* t = table_for_index(hIndex);
    if (!t || !t->order() || !t->order()->index()) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    }
    openads::abi::copy_to_caller(pucBuf, pusBufLen,
                                 t->order()->index()->expression());
    return ok();
}

UNSIGNED32 AdsGetIndexName(ADSHANDLE hIndex, UNSIGNED8* pucBuf,
                           UNSIGNED16* pusBufLen) {
    Table* t = table_for_index(hIndex);
    if (!t || !t->order() || !t->order()->index()) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    }
    openads::abi::copy_to_caller(pucBuf, pusBufLen,
                                 t->order()->index()->name());
    return ok();
}

UNSIGNED32 AdsSetIndexDirection(ADSHANDLE /*hIndex*/, UNSIGNED16 /*usDir*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "AdsSetIndexDirection deferred");
}

UNSIGNED32 AdsSeek(ADSHANDLE hIndex, UNSIGNED8* pucKey,
                   UNSIGNED16 usOption, UNSIGNED16* pbFound) {
    Table* t = table_for_index(hIndex);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    auto key = openads::abi::to_internal(pucKey, 0);
    bool soft = (usOption & ADS_SOFTSEEK) != 0;
    auto r = t->seek_key(key, soft);
    if (!r) return fail(r.error());
    if (pbFound != nullptr) *pbFound = r.value() ? 1 : 0;
    return ok();
}

UNSIGNED32 AdsSeekLast(ADSHANDLE hIndex, UNSIGNED8* pucKey,
                       UNSIGNED16* pbFound) {
    return AdsSeek(hIndex, pucKey, 0, pbFound);
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

UNSIGNED32 AdsPackTable(ADSHANDLE /*hTable*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "AdsPackTable lands in M4 alongside memo store");
}

UNSIGNED32 AdsZapTable(ADSHANDLE /*hTable*/) {
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

} // extern "C"
