// Auto-generated stubs for ACE entry points required by Harbour rddads but not
// yet implemented. Each returns AE_FUNCTION_NOT_AVAILABLE (5004). Linker matches
// by name only; the empty parameter list is intentional so callers with any
// signature still resolve.

#include "openads/error.h"

#include <cstdint>

#define STUB_TRACE(name) ((void)0)

// Forward to the real AdsConnect60 / AdsGetField which live in
// ace_exports.cpp.
extern "C" uint32_t AdsConnect60(uint8_t*, uint16_t, uint8_t*, uint8_t*,
                                 uint32_t, uint64_t*);
extern "C" uint32_t AdsGetField  (uint64_t, uint8_t*, uint8_t*, uint32_t*, uint16_t);

extern "C" {
// AdsConnect(server, &hConnect) ??? convenience wrapper that picks
// ADS_LOCAL_SERVER and no auth. Used by rddads' HB_FUNC(ADSCONNECT)
// for the simple case `AdsConnect(".")` from Harbour.
uint32_t AdsConnect(uint8_t* server, uint64_t* phConnect) {
    return AdsConnect60(server, /* ADS_LOCAL_SERVER */ 1,
                        nullptr, nullptr, 0u, phConnect);
}

// rddads' adsGetValue takes the AdsGetFieldRaw path when OEM
// translation is on. Until OpenADS implements an OEM-aware variant
// that bypasses charset translation, forward to AdsGetField ??? both
// return the raw character data; the translation step is a no-op for
// the ASCII fixtures the smoke test uses.
uint32_t AdsGetFieldRaw(uint64_t hTable, uint8_t* pucField,
                        uint8_t* pucBuf, uint32_t* pulLen) {
    return AdsGetField(hTable, pucField, pucBuf, pulLen, /*ADS_NONE*/ 0);
}

uint32_t AdsAddCustomKey() { STUB_TRACE(AdsAddCustomKey); return 5004u; }
uint32_t AdsApplicationExit() { STUB_TRACE(AdsApplicationExit); return 5004u; }
uint32_t AdsCacheOpenCursors() { STUB_TRACE(AdsCacheOpenCursors); return 5004u; }
uint32_t AdsCacheOpenTables() { STUB_TRACE(AdsCacheOpenTables); return 5004u; }
uint32_t AdsCacheRecords() { STUB_TRACE(AdsCacheRecords); return 5004u; }
uint32_t AdsCheckExistence() { STUB_TRACE(AdsCheckExistence); return 5004u; }
uint32_t AdsClearCallbackFunction() { STUB_TRACE(AdsClearCallbackFunction); return 5004u; }
uint32_t AdsClearFilter() { STUB_TRACE(AdsClearFilter); return 5004u; }
uint32_t AdsClearRelation() { STUB_TRACE(AdsClearRelation); return 5004u; }
uint32_t AdsCloseAllTables() { STUB_TRACE(AdsCloseAllTables); return 5004u; }
uint32_t AdsCloseCachedTables() { STUB_TRACE(AdsCloseCachedTables); return 5004u; }
uint32_t AdsConvertTable() { STUB_TRACE(AdsConvertTable); return 5004u; }
uint32_t AdsCopyTable() { STUB_TRACE(AdsCopyTable); return 5004u; }
uint32_t AdsCopyTableContents() { STUB_TRACE(AdsCopyTableContents); return 5004u; }
uint32_t AdsCreateFTSIndex() { STUB_TRACE(AdsCreateFTSIndex); return 5004u; }
uint32_t AdsCreateIndex61() { STUB_TRACE(AdsCreateIndex61); return 5004u; }
uint32_t AdsCreateTable() { STUB_TRACE(AdsCreateTable); return 5004u; }
uint32_t AdsCustomizeAOF() { STUB_TRACE(AdsCustomizeAOF); return 5004u; }
uint32_t AdsDDAddIndexFile() { STUB_TRACE(AdsDDAddIndexFile); return 5004u; }
uint32_t AdsDDAddUserToGroup() { STUB_TRACE(AdsDDAddUserToGroup); return 5004u; }
uint32_t AdsDDCreateLink() { STUB_TRACE(AdsDDCreateLink); return 5004u; }
uint32_t AdsDDCreateRefIntegrity() { STUB_TRACE(AdsDDCreateRefIntegrity); return 5004u; }
uint32_t AdsDDCreateUser() { STUB_TRACE(AdsDDCreateUser); return 5004u; }
uint32_t AdsDDDeleteUser() { STUB_TRACE(AdsDDDeleteUser); return 5004u; }
uint32_t AdsDDDropLink() { STUB_TRACE(AdsDDDropLink); return 5004u; }
uint32_t AdsDDGetDatabaseProperty() { STUB_TRACE(AdsDDGetDatabaseProperty); return 5004u; }
uint32_t AdsDDGetUserProperty() { STUB_TRACE(AdsDDGetUserProperty); return 5004u; }
uint32_t AdsDDModifyLink() { STUB_TRACE(AdsDDModifyLink); return 5004u; }
uint32_t AdsDDRemoveIndexFile() { STUB_TRACE(AdsDDRemoveIndexFile); return 5004u; }
uint32_t AdsDDRemoveRefIntegrity() { STUB_TRACE(AdsDDRemoveRefIntegrity); return 5004u; }
uint32_t AdsDDRemoveUserFromGroup() { STUB_TRACE(AdsDDRemoveUserFromGroup); return 5004u; }
uint32_t AdsDDSetDatabaseProperty() { STUB_TRACE(AdsDDSetDatabaseProperty); return 5004u; }
uint32_t AdsDeleteCustomKey() { STUB_TRACE(AdsDeleteCustomKey); return 5004u; }
uint32_t AdsDeleteFile() { STUB_TRACE(AdsDeleteFile); return 5004u; }
uint32_t AdsEvalAOF() { STUB_TRACE(AdsEvalAOF); return 5004u; }
uint32_t AdsExtractKey() { STUB_TRACE(AdsExtractKey); return 5004u; }
uint32_t AdsFailedTransactionRecovery() { STUB_TRACE(AdsFailedTransactionRecovery); return 5004u; }
uint32_t AdsFindClose() { STUB_TRACE(AdsFindClose); return 5004u; }
uint32_t AdsFindFirstTable() { STUB_TRACE(AdsFindFirstTable); return 5004u; }
uint32_t AdsFindNextTable() { STUB_TRACE(AdsFindNextTable); return 5004u; }
uint32_t AdsGetAllLocks() { STUB_TRACE(AdsGetAllLocks); return 5004u; }
uint32_t AdsGetAOF() { STUB_TRACE(AdsGetAOF); return 5004u; }
uint32_t AdsGetBinary() { STUB_TRACE(AdsGetBinary); return 5004u; }
uint32_t AdsGetBinaryLength() { STUB_TRACE(AdsGetBinaryLength); return 5004u; }
uint32_t AdsGetConnectionType() { STUB_TRACE(AdsGetConnectionType); return 5004u; }
uint32_t AdsGetDateFormat() { STUB_TRACE(AdsGetDateFormat); return 5004u; }
uint32_t AdsGetDefault() { STUB_TRACE(AdsGetDefault); return 5004u; }
uint32_t AdsGetDeleted() { STUB_TRACE(AdsGetDeleted); return 5004u; }
uint32_t AdsGetEpoch() { STUB_TRACE(AdsGetEpoch); return 5004u; }
uint32_t AdsGetErrorString() { STUB_TRACE(AdsGetErrorString); return 5004u; }
uint32_t AdsGetExact() { STUB_TRACE(AdsGetExact); return 5004u; }
uint32_t AdsGetFieldW() { STUB_TRACE(AdsGetFieldW); return 5004u; }
uint32_t AdsGetFilter() { STUB_TRACE(AdsGetFilter); return 5004u; }
uint32_t AdsGetHandleType() { STUB_TRACE(AdsGetHandleType); return 5004u; }
uint32_t AdsGetIndexCondition() { STUB_TRACE(AdsGetIndexCondition); return 5004u; }
uint32_t AdsGetIndexFilename() { STUB_TRACE(AdsGetIndexFilename); return 5004u; }
uint32_t AdsGetIndexOrderByHandle() { STUB_TRACE(AdsGetIndexOrderByHandle); return 5004u; }
uint32_t AdsGetKeyLength() { STUB_TRACE(AdsGetKeyLength); return 5004u; }
uint32_t AdsGetKeyNum() { STUB_TRACE(AdsGetKeyNum); return 5004u; }
uint32_t AdsGetKeyType() { STUB_TRACE(AdsGetKeyType); return 5004u; }
uint32_t AdsGetLastTableUpdate() { STUB_TRACE(AdsGetLastTableUpdate); return 5004u; }

uint32_t AdsGetLongLong() { STUB_TRACE(AdsGetLongLong); return 5004u; }
uint32_t AdsGetMilliseconds() { STUB_TRACE(AdsGetMilliseconds); return 5004u; }
uint32_t AdsGetNumActiveLinks() { STUB_TRACE(AdsGetNumActiveLinks); return 5004u; }
uint32_t AdsGetNumLocks() { STUB_TRACE(AdsGetNumLocks); return 5004u; }
uint32_t AdsGetNumOpenTables() { STUB_TRACE(AdsGetNumOpenTables); return 5004u; }
uint32_t AdsGetRecord() { STUB_TRACE(AdsGetRecord); return 5004u; }
uint32_t AdsGetRecordLength() { STUB_TRACE(AdsGetRecordLength); return 5004u; }
uint32_t AdsGetRelKeyPos() { STUB_TRACE(AdsGetRelKeyPos); return 5004u; }
uint32_t AdsGetSearchPath() { STUB_TRACE(AdsGetSearchPath); return 5004u; }
uint32_t AdsGetServerName() { STUB_TRACE(AdsGetServerName); return 5004u; }
uint32_t AdsGetServerTime() { STUB_TRACE(AdsGetServerTime); return 5004u; }
uint32_t AdsGetString() { STUB_TRACE(AdsGetString); return 5004u; }
uint32_t AdsGetStringW() { STUB_TRACE(AdsGetStringW); return 5004u; }
uint32_t AdsGetTableAlias() { STUB_TRACE(AdsGetTableAlias); return 5004u; }
uint32_t AdsGetTableCharType() { STUB_TRACE(AdsGetTableCharType); return 5004u; }
uint32_t AdsGetTableConnection() { STUB_TRACE(AdsGetTableConnection); return 5004u; }
uint32_t AdsGetTableFilename() { STUB_TRACE(AdsGetTableFilename); return 5004u; }
uint32_t AdsGetTableType() { STUB_TRACE(AdsGetTableType); return 5004u; }
uint32_t AdsGotoRecord() { STUB_TRACE(AdsGotoRecord); return 5004u; }
uint32_t AdsIsConnectionAlive() { STUB_TRACE(AdsIsConnectionAlive); return 5004u; }
uint32_t AdsIsEmpty() { STUB_TRACE(AdsIsEmpty); return 5004u; }
uint32_t AdsIsExprValid() { STUB_TRACE(AdsIsExprValid); return 5004u; }

uint32_t AdsIsIndexCustom() { STUB_TRACE(AdsIsIndexCustom); return 5004u; }
uint32_t AdsIsIndexDescending() { STUB_TRACE(AdsIsIndexDescending); return 5004u; }
uint32_t AdsIsIndexUnique() { STUB_TRACE(AdsIsIndexUnique); return 5004u; }
uint32_t AdsIsNull() { STUB_TRACE(AdsIsNull); return 5004u; }
uint32_t AdsIsRecordInAOF() { STUB_TRACE(AdsIsRecordInAOF); return 5004u; }
uint32_t AdsIsRecordLocked() { STUB_TRACE(AdsIsRecordLocked); return 5004u; }
uint32_t AdsIsServerLoaded() { STUB_TRACE(AdsIsServerLoaded); return 5004u; }
uint32_t AdsIsTableLocked() { STUB_TRACE(AdsIsTableLocked); return 5004u; }
uint32_t AdsMgConnect() { STUB_TRACE(AdsMgConnect); return 5004u; }
uint32_t AdsMgDisconnect() { STUB_TRACE(AdsMgDisconnect); return 5004u; }
uint32_t AdsMgGetActivityInfo() { STUB_TRACE(AdsMgGetActivityInfo); return 5004u; }
uint32_t AdsMgGetCommStats() { STUB_TRACE(AdsMgGetCommStats); return 5004u; }
uint32_t AdsMgGetConfigInfo() { STUB_TRACE(AdsMgGetConfigInfo); return 5004u; }
uint32_t AdsMgGetInstallInfo() { STUB_TRACE(AdsMgGetInstallInfo); return 5004u; }
uint32_t AdsMgGetLockOwner() { STUB_TRACE(AdsMgGetLockOwner); return 5004u; }
uint32_t AdsMgGetLocks() { STUB_TRACE(AdsMgGetLocks); return 5004u; }
uint32_t AdsMgGetOpenIndexes() { STUB_TRACE(AdsMgGetOpenIndexes); return 5004u; }
uint32_t AdsMgGetOpenTables() { STUB_TRACE(AdsMgGetOpenTables); return 5004u; }
uint32_t AdsMgGetServerType() { STUB_TRACE(AdsMgGetServerType); return 5004u; }
uint32_t AdsMgGetUserNames() { STUB_TRACE(AdsMgGetUserNames); return 5004u; }
uint32_t AdsMgGetWorkerThreadActivity() { STUB_TRACE(AdsMgGetWorkerThreadActivity); return 5004u; }
uint32_t AdsMgKillUser() { STUB_TRACE(AdsMgKillUser); return 5004u; }
uint32_t AdsMgResetCommStats() { STUB_TRACE(AdsMgResetCommStats); return 5004u; }
uint32_t AdsRefreshAOF() { STUB_TRACE(AdsRefreshAOF); return 5004u; }
uint32_t AdsRefreshRecord() { STUB_TRACE(AdsRefreshRecord); return 5004u; }
uint32_t AdsRegisterCallbackFunction() { STUB_TRACE(AdsRegisterCallbackFunction); return 5004u; }
uint32_t AdsReindex() { STUB_TRACE(AdsReindex); return 5004u; }
uint32_t AdsRestructureTable() { STUB_TRACE(AdsRestructureTable); return 5004u; }
uint32_t AdsSetBinary() { STUB_TRACE(AdsSetBinary); return 5004u; }
uint32_t AdsSetDateFormat() { STUB_TRACE(AdsSetDateFormat); return 5004u; }
uint32_t AdsSetDecimals() { STUB_TRACE(AdsSetDecimals); return 5004u; }
uint32_t AdsSetDefault() { STUB_TRACE(AdsSetDefault); return 5004u; }
uint32_t AdsSetEpoch() { STUB_TRACE(AdsSetEpoch); return 5004u; }
uint32_t AdsSetExact() { STUB_TRACE(AdsSetExact); return 5004u; }
uint32_t AdsSetFieldRaw() { STUB_TRACE(AdsSetFieldRaw); return 5004u; }
uint32_t AdsSetFilter() { STUB_TRACE(AdsSetFilter); return 5004u; }
uint32_t AdsSetJulian() { STUB_TRACE(AdsSetJulian); return 5004u; }
uint32_t AdsSetLongLong() { STUB_TRACE(AdsSetLongLong); return 5004u; }
uint32_t AdsSetMilliseconds() { STUB_TRACE(AdsSetMilliseconds); return 5004u; }
uint32_t AdsSetRecord() { STUB_TRACE(AdsSetRecord); return 5004u; }
uint32_t AdsSetRelation() { STUB_TRACE(AdsSetRelation); return 5004u; }
uint32_t AdsSetRelKeyPos() { STUB_TRACE(AdsSetRelKeyPos); return 5004u; }
uint32_t AdsSetScopedRelation() { STUB_TRACE(AdsSetScopedRelation); return 5004u; }
uint32_t AdsSetSearchPath() { STUB_TRACE(AdsSetSearchPath); return 5004u; }
uint32_t AdsSetServerType() { STUB_TRACE(AdsSetServerType); return 5004u; }
uint32_t AdsSetStringW() { STUB_TRACE(AdsSetStringW); return 5004u; }
uint32_t AdsShowDeleted() { STUB_TRACE(AdsShowDeleted); return 5004u; }
uint32_t AdsShowError() { STUB_TRACE(AdsShowError); return 5004u; }
uint32_t AdsSkipUnique() { STUB_TRACE(AdsSkipUnique); return 5004u; }
uint32_t AdsStmtSetTableLockType() { STUB_TRACE(AdsStmtSetTableLockType); return 5004u; }
uint32_t AdsStmtSetTablePassword() { STUB_TRACE(AdsStmtSetTablePassword); return 5004u; }
uint32_t AdsStmtSetTableReadOnly() { STUB_TRACE(AdsStmtSetTableReadOnly); return 5004u; }
uint32_t AdsStmtSetTableType() { STUB_TRACE(AdsStmtSetTableType); return 5004u; }
uint32_t AdsVerifySQL() { STUB_TRACE(AdsVerifySQL); return 5004u; }
uint32_t AdsWriteAllRecords() { STUB_TRACE(AdsWriteAllRecords); return 5004u; }

// AdsIsFound moved to ace_exports.cpp (real impl reading the table's
// last-seek-hit state).

// rddads' adsGetValue routes HB_FT_LOGICAL fields through
// AdsGetLogical. Read the underlying field as raw character data and
// decode the first byte: 'T'/'t'/'Y'/'y' -> true, anything else false.
uint32_t AdsGetLogical(uint64_t hTable, uint8_t* pucField,
                       uint16_t* pbValue) {
    if (pbValue == nullptr) return 0u;
    *pbValue = 0;
    uint8_t buf[4] = {0,0,0,0};
    uint32_t cap = sizeof(buf);
    uint32_t rc = AdsGetField(hTable, pucField, buf, &cap, /*ADS_NONE*/ 0);
    if (rc == 0u && cap >= 1) {
        char c = static_cast<char>(buf[0]);
        if (c == 'T' || c == 't' || c == 'Y' || c == 'y') *pbValue = 1;
    }
    return 0u;
}
}



