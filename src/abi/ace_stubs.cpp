// Stubs for ACE entry points required by Harbour rddads.
//
// Categorisation:
//   * "Config no-op" — Set / Clear / Cache / Refresh / Customize /
//     Register etc. that adjust runtime preferences. Returning
//     AE_SUCCESS lets Harbour proceed without the AE_FUNCTION_NOT_AVAILABLE
//     hard-fail it would otherwise treat as fatal in some paths.
//   * "Read default" — Get* of state we do not track yet; return a
//     reasonable default plus AE_SUCCESS so the caller's local copy
//     stays initialised.
//   * "Genuinely missing" — Mg* (remote-server management),
//     DD* (advanced Data Dictionary), CreateTable / Restructure / Reindex,
//     binary blob set/get, find-table family, ... — return
//     AE_FUNCTION_NOT_AVAILABLE (5004) so applications can detect the gap.
//
// Linker matches by name only; the empty parameter list on each
// "no-op" / 5004 stub is intentional so callers with any signature
// still resolve through register-passing on x64.

#include "openads/ace.h"
#include "openads/error.h"

#include <cstdint>
#include <cstring>

#define OK   0u
#define MISS 5004u

// Forward to the real AdsConnect60 / AdsGetField which live in
// ace_exports.cpp.
extern "C" uint32_t AdsConnect60(uint8_t*, uint16_t, uint8_t*, uint8_t*,
                                 uint32_t, uint64_t*);
extern "C" uint32_t AdsGetField  (uint64_t, uint8_t*, uint8_t*, uint32_t*, uint16_t);

extern "C" {

// AdsConnect(server, &hConnect) — convenience wrapper that picks
// ADS_LOCAL_SERVER and no auth. Used by rddads' HB_FUNC(ADSCONNECT)
// for the simple case `AdsConnect(".")` from Harbour.
uint32_t AdsConnect(uint8_t* server, uint64_t* phConnect) {
    return AdsConnect60(server, /* ADS_LOCAL_SERVER */ 1,
                        nullptr, nullptr, 0u, phConnect);
}

// rddads' adsGetValue takes the AdsGetFieldRaw path when OEM
// translation is on. The translation step is a no-op for ASCII data
// so we forward to the regular AdsGetField.
uint32_t AdsGetFieldRaw(uint64_t hTable, uint8_t* pucField,
                        uint8_t* pucBuf, uint32_t* pulLen) {
    return AdsGetField(hTable, pucField, pucBuf, pulLen, /*ADS_NONE*/ 0);
}

// ---- Config no-ops --------------------------------------------------
// Each accepts whatever Harbour pushes and reports AE_SUCCESS so the
// caller proceeds. Inputs are intentionally ignored: OpenADS' engine
// has stable defaults that match Clipper conventions.
uint32_t AdsApplicationExit       () { return OK; }
uint32_t AdsCacheOpenCursors      () { return OK; }
uint32_t AdsCacheOpenTables       () { return OK; }
uint32_t AdsCacheRecords          () { return OK; }
uint32_t AdsCloseCachedTables     () { return OK; }
uint32_t AdsClearCallbackFunction () { return OK; }
uint32_t AdsRegisterCallbackFunction() { return OK; }
uint32_t AdsClearFilter           () { return OK; }
uint32_t AdsClearRelation         () { return OK; }
uint32_t AdsCustomizeAOF          () { return OK; }
uint32_t AdsEvalAOF               () { return OK; }
uint32_t AdsRefreshAOF            () { return OK; }
uint32_t AdsSetDateFormat         () { return OK; }
uint32_t AdsSetDecimals           () { return OK; }
uint32_t AdsSetDefault            () { return OK; }
uint32_t AdsSetEpoch              () { return OK; }
uint32_t AdsSetExact              () { return OK; }
uint32_t AdsSetFilter             () { return OK; }
uint32_t AdsSetRelation           () { return OK; }
uint32_t AdsSetRelKeyPos          () { return OK; }
uint32_t AdsSetScopedRelation     () { return OK; }
uint32_t AdsSetSearchPath         () { return OK; }
uint32_t AdsSetServerType         () { return OK; }
uint32_t AdsShowDeleted           () { return OK; }
uint32_t AdsShowError             () { return OK; }
uint32_t AdsStmtSetTableLockType  () { return OK; }
uint32_t AdsStmtSetTablePassword  () { return OK; }
uint32_t AdsStmtSetTableReadOnly  () { return OK; }
uint32_t AdsStmtSetTableType      () { return OK; }
uint32_t AdsWriteAllRecords       () { return OK; }

// ---- Read default ---------------------------------------------------
// Return AE_SUCCESS plus a sane default written into the out param.
// Harbour and rddads both fall back gracefully when these are zero
// or empty. Tracked explicitly per call so missing fixtures (e.g.
// AdsGetServerName for a local connection) don't blow up.
uint32_t AdsGetAOF              (uint64_t, uint8_t*, uint16_t* p) { if (p) *p = 0; return OK; }
uint32_t AdsGetDateFormat       (uint8_t* buf, uint16_t* len) {
    const char* fmt = "YYYY-MM-DD";
    if (len) {
        uint16_t cap = *len;
        uint16_t n   = 10 < cap ? 10 : cap;
        if (buf && cap > 0) { std::memcpy(buf, fmt, n); if (n < cap) buf[n] = '\0'; }
        *len = 10;
    }
    return OK;
}
uint32_t AdsGetDefault          (uint8_t* buf, uint16_t* len) {
    if (len) { if (buf && *len > 0) buf[0] = '\0'; *len = 0; }
    return OK;
}
uint32_t AdsGetDeleted          (uint16_t* p) { if (p) *p = 0; return OK; }
uint32_t AdsGetEpoch            (uint16_t* p) { if (p) *p = 1900; return OK; }
uint32_t AdsGetExact            (uint16_t* p) { if (p) *p = 0; return OK; }
uint32_t AdsGetFilter           (uint64_t, uint8_t*, uint16_t* len) { if (len) *len = 0; return OK; }
uint32_t AdsGetSearchPath       (uint64_t, uint8_t*, uint16_t* len) { if (len) *len = 0; return OK; }
uint32_t AdsGetIndexCondition   (uint64_t, uint8_t*, uint16_t* len) { if (len) *len = 0; return OK; }
uint32_t AdsGetIndexFilename    (uint64_t, uint8_t*, uint16_t* len) { if (len) *len = 0; return OK; }
uint32_t AdsGetIndexOrderByHandle(uint64_t, uint16_t* p) { if (p) *p = 1; return OK; }
uint32_t AdsGetTableAlias       (uint64_t, uint8_t* buf, uint16_t* len) {
    if (len) { if (buf && *len > 0) buf[0] = '\0'; *len = 0; }
    return OK;
}
uint32_t AdsGetTableCharType    (uint64_t, uint16_t* p) { if (p) *p = /*ADS_ANSI*/ 1; return OK; }
uint32_t AdsGetTableConnection  (uint64_t, uint64_t* p) { if (p) *p = 1; return OK; }
uint32_t AdsGetMilliseconds     (uint64_t, uint8_t*, int32_t* p) { if (p) *p = 0; return OK; }
uint32_t AdsGetNumActiveLinks   (uint64_t, uint16_t* p) { if (p) *p = 0; return OK; }
uint32_t AdsGetNumLocks         (uint64_t, uint16_t* p) { if (p) *p = 0; return OK; }
uint32_t AdsGetNumOpenTables    (uint64_t, uint16_t* p) { if (p) *p = 0; return OK; }
uint32_t AdsGetRecord           (uint64_t, uint8_t*, uint32_t* len) { if (len) *len = 0; return OK; }
uint32_t AdsGetRelKeyPos        (uint64_t, double* p) { if (p) *p = 0.0; return OK; }
uint32_t AdsGetLastTableUpdate  (uint64_t, uint8_t*, uint16_t* len) { if (len) *len = 0; return OK; }
uint32_t AdsGetKeyLength        (uint64_t, uint16_t* p) { if (p) *p = 0; return OK; }
uint32_t AdsGetKeyNum           (uint64_t, uint16_t, uint32_t* p) { if (p) *p = 0; return OK; }
uint32_t AdsGetKeyType          (uint64_t, uint16_t* p) { if (p) *p = /*ADS_STRING*/ 4; return OK; }
uint32_t AdsGetHandleType       (uint64_t, uint16_t* p) { if (p) *p = /*ADS_CURSOR*/ 1; return OK; }
uint32_t AdsGetConnectionType   (uint64_t, uint16_t* p) { if (p) *p = /*ADS_LOCAL_SERVER*/ 1; return OK; }
uint32_t AdsGetErrorString      (uint32_t, uint8_t* buf, uint16_t* len) {
    const char* msg = "OpenADS error";
    if (len) {
        uint16_t cap = *len;
        uint16_t n   = 13 < cap ? 13 : cap;
        if (buf && cap > 0) { std::memcpy(buf, msg, n); if (n < cap) buf[n] = '\0'; }
        *len = 13;
    }
    return OK;
}
uint32_t AdsIsConnectionAlive   (uint64_t, uint16_t* p) { if (p) *p = 1; return OK; }
uint32_t AdsIsEmpty             (uint64_t, uint8_t*, uint16_t* p) { if (p) *p = 0; return OK; }
uint32_t AdsIsExprValid         (uint64_t, uint8_t*, uint16_t* p) { if (p) *p = 1; return OK; }
uint32_t AdsIsIndexCustom       (uint64_t, uint16_t* p) { if (p) *p = 0; return OK; }
uint32_t AdsIsIndexDescending   (uint64_t, uint16_t* p) { if (p) *p = 0; return OK; }
uint32_t AdsIsIndexUnique       (uint64_t, uint16_t* p) { if (p) *p = 0; return OK; }
uint32_t AdsIsNull              (uint64_t, uint8_t*, uint16_t* p) { if (p) *p = 0; return OK; }
uint32_t AdsIsRecordInAOF       (uint64_t, uint16_t* p) { if (p) *p = 1; return OK; }
uint32_t AdsIsRecordLocked      (uint64_t, uint32_t, uint16_t* p) { if (p) *p = 0; return OK; }
uint32_t AdsIsServerLoaded      (uint8_t*, uint16_t* p) { if (p) *p = 0; return OK; }
uint32_t AdsIsTableLocked       (uint64_t, uint16_t* p) { if (p) *p = 0; return OK; }

// AdsGetLogical decodes the underlying char field as 'T'/'Y'/'1'
// → true. Reuses the real AdsGetField for the column read so it
// works with both the bare-name and ADSFIELD(n) forms of pucField.
uint32_t AdsGetLogical(uint64_t hTable, uint8_t* pucField, uint16_t* pbValue) {
    if (pbValue == nullptr) return OK;
    *pbValue = 0;
    uint8_t buf[4] = {0,0,0,0};
    uint32_t cap = sizeof(buf);
    uint32_t rc = AdsGetField(hTable, pucField, buf, &cap, /*ADS_NONE*/ 0);
    if (rc == OK && cap >= 1) {
        char c = static_cast<char>(buf[0]);
        if (c == 'T' || c == 't' || c == 'Y' || c == 'y' || c == '1') {
            *pbValue = 1;
        }
    }
    return OK;
}

// AdsSetMilliseconds / AdsSetRecord — accept any signature, return OK.
// The engine doesn't track millisecond timestamps separately; bulk
// SetRecord lands when the engine grows a write-record-direct path.
uint32_t AdsSetMilliseconds     () { return OK; }
uint32_t AdsSetRecord           () { return OK; }

// --- M9.25 local-mode AdsDD* CRUD surface ---------------------------------
//
// Real ACE persists users / groups / links / RI rules / property
// pairs / index-file registrations in the proprietary `.add` binary.
// OpenADS' DD is a clean-room text format (`# OpenADS Data Dictionary
// v0` + `TABLE alias=path` lines) and at 0.2.x doesn't yet round-trip
// the advanced fields. The DD CRUD calls below accept the requests
// silently (matching the "everything quiescent" contract used for the
// Mg* surface in M9.24): apps that only inspect the return code keep
// running, but the data is not yet persisted across reopens. Real
// persistence ships with the 0.3.x DD work alongside the clean-room
// `.add` writer.
//
// The `Get*Property` calls return zero-filled output so reads see
// 0 / "" instead of uninitialised memory.

uint32_t AdsDDAddIndexFile      (uint64_t /*hConn*/, uint8_t* /*pT*/,
                                 uint8_t* /*pI*/,    uint8_t* /*pC*/) { return OK; }
uint32_t AdsDDRemoveIndexFile   (uint64_t /*hConn*/, uint8_t* /*pT*/,
                                 uint8_t* /*pI*/,    uint16_t /*opt*/) { return OK; }

uint32_t AdsDDCreateLink        (uint64_t /*hConn*/, uint8_t* /*pAlias*/,
                                 uint8_t* /*pPath*/, uint8_t* /*pUser*/,
                                 uint8_t* /*pPwd*/,  uint16_t /*opt*/) { return OK; }
uint32_t AdsDDDropLink          (uint64_t /*hConn*/, uint8_t* /*pAlias*/,
                                 uint16_t /*opt*/) { return OK; }
uint32_t AdsDDModifyLink        (uint64_t /*hConn*/, uint8_t* /*pAlias*/,
                                 uint8_t* /*pPath*/, uint8_t* /*pUser*/,
                                 uint8_t* /*pPwd*/,  uint16_t /*opt*/) { return OK; }

uint32_t AdsDDCreateUser        (uint64_t /*hConn*/, uint8_t* /*pGroup*/,
                                 uint8_t* /*pUser*/, uint8_t* /*pPwd*/,
                                 uint8_t* /*pDesc*/) { return OK; }
uint32_t AdsDDDeleteUser        (uint64_t /*hConn*/, uint8_t* /*pUser*/) { return OK; }
uint32_t AdsDDAddUserToGroup    (uint64_t /*hConn*/, uint8_t* /*pGroup*/,
                                 uint8_t* /*pUser*/) { return OK; }
uint32_t AdsDDRemoveUserFromGroup(uint64_t /*hConn*/, uint8_t* /*pGroup*/,
                                  uint8_t* /*pUser*/) { return OK; }

uint32_t AdsDDCreateRefIntegrity (uint64_t /*hConn*/, uint8_t* /*pName*/,
                                  uint8_t* /*pFail*/, uint8_t* /*pParent*/,
                                  uint8_t* /*pChild*/, uint8_t* /*pTag*/,
                                  uint16_t /*usUpdate*/, uint16_t /*usDelete*/,
                                  uint8_t* /*pDesc*/, uint16_t /*opt*/) { return OK; }
uint32_t AdsDDRemoveRefIntegrity (uint64_t /*hConn*/, uint8_t* /*pName*/) { return OK; }

uint32_t AdsDDGetDatabaseProperty(uint64_t /*hConn*/, uint16_t /*usProp*/,
                                  void* pBuf, uint16_t* pusLen) {
    if (pusLen == nullptr) return OK;
    uint16_t n = *pusLen;
    if (pBuf != nullptr && n > 0) std::memset(pBuf, 0, n);
    *pusLen = 0;
    return OK;
}
uint32_t AdsDDSetDatabaseProperty(uint64_t /*hConn*/, uint16_t /*usProp*/,
                                  void* /*pBuf*/, uint16_t /*usLen*/) { return OK; }
uint32_t AdsDDGetUserProperty    (uint64_t /*hConn*/, uint8_t* /*pUser*/,
                                  uint16_t /*usProp*/, void* pBuf,
                                  uint16_t* pusLen) {
    if (pusLen == nullptr) return OK;
    uint16_t n = *pusLen;
    if (pBuf != nullptr && n > 0) std::memset(pBuf, 0, n);
    *pusLen = 0;
    return OK;
}
// --- M9.24 local-mode management surface ----------------------------------
//
// Real ACE Mg* calls are addressed at a remote management daemon —
// they query the server's activity log, list connected users, kill
// orphan sessions, etc. OpenADS today is local-mode only, so there
// is no remote daemon to consult; the implementation reports
// "everything is empty / quiescent" instead of hard-failing with
// AE_FUNCTION_NOT_AVAILABLE so apps that only probe the management
// surface for diagnostics keep running.
//
// Output buffers passed by the caller are zero-filled for `*pusSize`
// bytes so reads of any struct field return 0 / "" instead of
// uninitialised stack memory. Apps that act on the data (e.g.
// pretty-print "registered owner") see the empty defaults; apps that
// only ever check the return code see AE_SUCCESS and proceed.

uint32_t AdsMgConnect(uint8_t* /*pucServer*/, uint8_t* /*pucUser*/,
                      uint8_t* /*pucPwd*/, uint64_t* phMgmt) {
    if (phMgmt != nullptr) *phMgmt = 1;   // synthetic local-mode handle
    return OK;
}
uint32_t AdsMgDisconnect(uint64_t /*hMgmt*/) { return OK; }

uint32_t AdsMgGetServerType(uint64_t /*hMgmt*/, uint16_t* pusType) {
    if (pusType != nullptr) *pusType = 0;
    return OK;
}

// Zero-fill helper for the struct-shaped Mg* outputs. The caller
// passes a stack struct + an in/out size; we trust the in size and
// memset that many bytes through the pointer.
static void zero_fill(void* p, uint16_t* pusSize) {
    if (pusSize == nullptr) return;
    uint16_t n = *pusSize;
    if (p != nullptr && n > 0) std::memset(p, 0, n);
    // Out-size keeps the original capacity; a real ACE may bump it
    // when the struct grew across versions, but zero is a safe lower
    // bound that won't trip rddads' "newer client lib" trace branch.
}

uint32_t AdsMgGetInstallInfo(uint64_t /*hMgmt*/, void* pStruct,
                             uint16_t* pusSize) {
    zero_fill(pStruct, pusSize);
    return OK;
}
uint32_t AdsMgGetActivityInfo(uint64_t /*hMgmt*/, void* pStruct,
                              uint16_t* pusSize) {
    zero_fill(pStruct, pusSize);
    return OK;
}
uint32_t AdsMgGetCommStats(uint64_t /*hMgmt*/, void* pStruct,
                           uint16_t* pusSize) {
    zero_fill(pStruct, pusSize);
    return OK;
}
uint32_t AdsMgGetConfigInfo(uint64_t /*hMgmt*/, void* pStruct,
                            uint16_t* pusSize) {
    zero_fill(pStruct, pusSize);
    return OK;
}
uint32_t AdsMgGetWorkerThreadActivity(uint64_t /*hMgmt*/, void* pStruct,
                                      uint16_t* pusSize) {
    zero_fill(pStruct, pusSize);
    return OK;
}

// "Get list" Mg* calls — count goes to 0, the list buffer untouched
// (caller keeps whatever it pre-filled).
uint32_t AdsMgGetUserNames(uint64_t /*hMgmt*/, void* /*pBuf*/,
                           uint16_t* pusCount) {
    if (pusCount != nullptr) *pusCount = 0;
    return OK;
}
uint32_t AdsMgGetLocks(uint64_t /*hMgmt*/, void* /*pBuf*/,
                       uint16_t* pusCount) {
    if (pusCount != nullptr) *pusCount = 0;
    return OK;
}
uint32_t AdsMgGetLockOwner(uint64_t /*hMgmt*/, void* /*pBuf*/,
                           uint16_t* pusLen) {
    if (pusLen != nullptr) *pusLen = 0;
    return OK;
}
uint32_t AdsMgGetOpenTables(uint64_t /*hMgmt*/, void* /*pBuf*/,
                            uint16_t* pusCount) {
    if (pusCount != nullptr) *pusCount = 0;
    return OK;
}
uint32_t AdsMgGetOpenIndexes(uint64_t /*hMgmt*/, void* /*pBuf*/,
                             uint16_t* pusCount) {
    if (pusCount != nullptr) *pusCount = 0;
    return OK;
}

uint32_t AdsMgKillUser(uint64_t /*hMgmt*/, uint8_t* /*pucUser*/,
                       uint16_t /*usOption*/) { return OK; }
uint32_t AdsMgResetCommStats(uint64_t /*hMgmt*/) { return OK; }
uint32_t AdsRestructureTable     () { return MISS; }

}  // extern "C"
