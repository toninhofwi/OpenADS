# ACE64 / OpenACE64 Compatibility Report

Date: 2026-07-03

This report tracks OpenADS export coverage against the SAP ACE64 DLL fixtures
checked into `tests/tools`. It is intended to be a shared contributor roadmap.
Personal notes and experiments should live in `ACE64_API_COMPAT_REPORT.local.md`,
which is intentionally gitignored.

## Scope

- SAP reference binaries inspected locally:
  - `tests/tools/ace64.dll` (2015-12-18)
  - `tests/tools/ace64_v12.dll` (2015-10-06)
  - `tests/tools/adsloc64.dll` (2015-12-18)
- OpenADS export surfaces inspected locally:
  - `src/openads_ace.def`
  - `bindings/python/bin/openace64.dll`

## Method

- Export names are extracted with `llvm-objdump -p`.
- SAP names are filtered case-sensitively to `Ads*` and `ObsAds*`.
- Lowercase `ads_*` exports are not counted in this ACE C API comparison.
- OpenADS source exports are read from `src/openads_ace.def`.
- Missing and extra export lists are sorted alphabetically, ignoring a leading
  `*` priority marker.

## How To Update

1. Rebuild or update `src/openads_ace.def` as needed.
2. Extract SAP exports from `tests/tools/ace64.dll` and `tests/tools/ace64_v12.dll`
   with `llvm-objdump -p`.
3. Filter export names case-sensitively to `Ads*` and `ObsAds*`.
4. Compare the filtered SAP names to the names exported by `src/openads_ace.def`.
5. Refresh the summary counts, the missing SAP v12 export list, and the OpenADS
   extra export list.
6. Sort lists alphabetically. Preserve a leading `*` only for missing exports
   that are intentionally marked as higher priority.

## Summary

- SAP `tests/tools/ace64.dll` exposes 574 `Ads*`/`ObsAds*` symbols.
- SAP `tests/tools/ace64_v12.dll` exposes 578 `Ads*`/`ObsAds*` symbols.
- OpenADS `src/openads_ace.def` exports 392 symbols total, including 387
  `Ads*`/`ObsAds*` API symbols, CRT shims, and OpenADS-specific extensions.
- The built `bindings/python/bin/openace64.dll` exports 363 symbols total.
- Against the bundled SAP v11 fixture, 337 of 574 SAP `Ads*`/`ObsAds*` exports
  are present in `src/openads_ace.def`.
- Against the bundled SAP v12 fixture, 341 of 578 SAP `Ads*`/`ObsAds*` exports
  are present in `src/openads_ace.def`.
- Against the bundled SAP v11 fixture, 237 SAP `Ads*`/`ObsAds*` exports are not
  present in `src/openads_ace.def`.
- Against the bundled SAP v12 fixture, 237 SAP `Ads*`/`ObsAds*` exports are not
  present in `src/openads_ace.def`.
- OpenADS currently has 46 `Ads*` API exports that are compatibility aliases or
  OpenADS extensions not present in the SAP v12 fixture.
- `adsloc64.dll` is not an ACE API surface. It exports only `axCommReq`,
  `axConnectLocal`, `axConnectLocal60`, `axDisconnect`,
  `axSetGaugeCallback`, and `AdsGetLibraryVersion`.

Conclusion:

- OpenACE64 is not currently export-complete relative to SAP ACE64.dll.
- OpenACE64 covers the core application/RDD/API surface used by this project,
  plus OpenADS-specific APIs, but a strict "every exposed ACE64.dll function
  exists" target still has a large gap.

## Missing SAP ACE64 v12 Exports

These names are exported by `tests/tools/ace64_v12.dll` but not by
`src/openads_ace.def`.

A leading `*` marks an export that has been manually identified as higher
priority.

```text
AdsAccessVfpSystemField
AdsActivateAOF
AdsAddToAOF
*AdsAreTriggersEnabled
*AdsBuildKeyFromRecord
*AdsBuildRawKey
*AdsBuildRawKey100
*AdsCachePrepareSQL
*AdsCachePrepareSQLW
AdsClearCachePool
AdsClearCursorAOF
*AdsClearLastError
AdsClearRecordBuffer
AdsCloseCachedTrigStatements
*AdsCloseFile
AdsCompareBookmarks
*AdsConnect101
AdsConvertCodePageToUnicode
*AdsConvertDateToJulian
*AdsConvertJulianToString
*AdsConvertKeyToDouble
*AdsConvertMillisecondsToString
*AdsConvertStringToJulian
*AdsConvertStringToMilliseconds
*AdsConvertUnicodeToCodePage
*AdsCopyTableStructure81
*AdsCopyTableTop
*AdsCopyTableTop100
AdsCreateCriticalSection
*AdsCreateMemTable
*AdsCreateMemTable90
*AdsDBFDateToString
*AdsDDAddProcedure100
*AdsDDAddView100
*AdsDDAutoCreateIndex
*AdsDDAutoCreateTable
*AdsDDClose
*AdsDDCreate101
AdsDDCreateArticle
AdsDDCreateArticle100
AdsDDCreateASA
AdsDDCreateASA101
*AdsDDCreateFunction100
*AdsDDCreatePackage
AdsDDCreatePublication
AdsDDCreateSubscription
*AdsDDCreateTrigger100
*AdsDDCreateUserGroup
AdsDDDeleteArticle
*AdsDDDeleteIndex
AdsDDDeletePublication
AdsDDDeleteSubscription
*AdsDDDeleteUserGroup
*AdsDDDeployDatabase
*AdsDDDisableTriggers
*AdsDDDropPackage
*AdsDDEnableTriggers
*AdsDDExecuteProcedure
*AdsDDFreeTable
AdsDDGetArticleProperty
AdsDDGetIndexFileProperty
AdsDDGetLinkProperty
*AdsDDGetObjectProperty
*AdsDDGetObjectProperty100
AdsDDGetProcInterfaceVersion
AdsDDGetPublicationProperty
AdsDDGetSubscriptionProperty
AdsDDGetTableOpenOptions
AdsDDGetUserGroupProperty
AdsDDMoveObjectFile
*AdsDDOpen
AdsDDRenameObject
*AdsDDSetActiveDictionary
AdsDDSetArticleProperty
AdsDDSetObjectAccessRights
*AdsDDSetObjectProperty
*AdsDDSetObjectProperty100
AdsDDSetPublicationProperty
AdsDDSetSubscriptionProperty
*AdsDDSetUserGroupProperty
*AdsDDVerifyUserRights
AdsDeactivateAOF
*AdsDeleteTable
AdsEcho
AdsEvalAOF100
*AdsEvalExpr
AdsEvalLogicalExprW
*AdsExpressionLongToShort
*AdsExpressionLongToShort100
*AdsExpressionLongToShort90
*AdsExpressionShortToLong
*AdsExpressionShortToLong90
*AdsExtractPathPart
*AdsFindServers
AdsFreeExpr
AdsFreeTokenBuffer
AdsGetActiveLinkInfo
AdsGetAOF100
AdsGetAOFOptLevel100
*AdsGetBaseFieldName
*AdsGetBaseFieldNum
AdsGetBookmarkLength
AdsGetCollation
AdsGetCollationLang
AdsGetColumnPermissions
AdsGetConnectionHandle
AdsGetConnectionPath
AdsGetConnectionProperty
AdsGetCursorAOF
AdsGetDataLength
*AdsGetDecimals
*AdsGetDeleteStatementW
AdsGetFieldCreationString
AdsGetFilePosition
AdsGetFTSIndexInfo
AdsGetFTSIndexInfoW
AdsGetFTSScore
AdsGetHandleLong
AdsGetIndexCollation
*AdsGetIndexFlags
AdsGetIndexHandleByExpr
*AdsGetIndexInfo
*AdsGetIndexPageSize
AdsGetInsertStatementW
AdsGetIntProperty
AdsGetKeyColumn
AdsGetKeyFilter
AdsGetLibraryVersion
AdsGetMergeStatementW
*AdsGetMoney
AdsGetNullRecord
AdsGetNumFTSIndexes
AdsGetNumSegments
AdsGetPreparedFields
AdsGetRecordPointer
AdsGetRILookupInfo
AdsGetROWIDPrefix
AdsGetSegmentFieldname
AdsGetSegmentFieldNumbers
AdsGetSegmentOffset
AdsGetSelectStatementW
*AdsGetShort
*AdsGetSQLStatement
*AdsGetSQLStatementHandle
*AdsGetSQLStmtParams
AdsGetTableCollation
AdsGetTableCreationString
*AdsGetTableHandle
*AdsGetTableMemoSize
*AdsGetTableRights
AdsGetTableWAN
*AdsGetTime
*AdsGetTransactionCount
*AdsGetUpdateStatementW
*AdsGotoBOF
*AdsGotoBookmark
*AdsGotoEOF
*AdsImageToClipboard
*AdsImageToHBitmap
*AdsInitTokenBuffer
*AdsIsFieldBinary
*AdsIsIndexCandidate
*AdsIsIndexCompound
*AdsIsIndexExprValid
*AdsIsIndexFTS
*AdsIsIndexNullable
AdsIsIndexPrimaryKey
AdsIsIndexUserDefined
*AdsIsNullable
AdsIsSegmentDescending
AdsIsTableTransactionFree
*AdsLocate
AdsLockRecordImplicitly
AdsLookupKey
AdsMakeNameUnique
AdsMemCompare
AdsMemCompare90
AdsMemICompare
AdsMemICompare90
AdsMemLwr
AdsMemLwr90
AdsMergeAOF
AdsMgKillUser90
AdsNullTerminateStrings
AdsOpenFileRaw
*AdsOpenTable101
AdsParseExpr
AdsParseExpr100
AdsPerformRI
AdsPrepareSQLNow
AdsPrepareSQLNowW
AdsPullTrigger
AdsReadFile
AdsReadRecordNumbers
AdsReadRecords
AdsReapUnusedConnections
AdsRefreshView
AdsRegisterCallbackFunction101
AdsRegisterSQLAbortFunc
AdsRegisterUDF
AdsReindexFTS
AdsReleaseObject
*AdsRemoveSQLComments
*AdsSeekInFile
AdsSetAOF100
AdsSetBaseTableAccess
AdsSetBOFFlag
AdsSetCollationLang
AdsSetCollationSequence
AdsSetCursorAOF
AdsSetFilter100
AdsSetFlushFlag
AdsSetHandleLong
AdsSetInternalError
AdsSetLastError
AdsSetPacketSize
AdsSetProperty
AdsSetRightsChecking
AdsSetStringFromCodePage
AdsSetTableCharType
AdsSetTableTransactionFree
AdsSetTimeStampRaw
AdsSetupRI
AdsSqlPeekStatement
AdsStepIndexKey
AdsStmtConstrainUpdates
AdsStmtEnableEncryption
AdsStmtGetCursorHandle
AdsStmtGetNumParams
AdsStmtReadAllColumns
AdsStmtSetOptimization
*AdsVerifyPassword
*AdsVerifyRI
AdsWaitForObject
AdsWriteMiniDump
ObsAdsDecryptBuffer
ObsAdsEncryptBuffer
```

## Recently Implemented

These exports were recently removed from the missing list and are useful
reference points for future contributor work:

```text
AdsBinaryToFileW
AdsExecuteSQLDirectW
AdsFileToBinaryW
AdsGetFieldLength100
AdsGetFieldNum
AdsGetFieldOffset
AdsGetTableLockType
AdsPackTable120
AdsPrepareSQLW
AdsRestructureTable120
AdsSetDate
AdsSetDateFormat60
AdsSetExact22
AdsSetFieldW
AdsSetLong
AdsSetSQLTimeout
AdsVerifySQLW
```

v12-only names missing beyond the v11 fixture: none.

## Present In OpenADS But Not In The SAP Fixture

These are either OpenADS extensions, compatibility aliases, or CRT shims:

```text
_dclass
_dsign
_eof
_getch
_kbhit
AdsAggregate
AdsAggregateClose
AdsAggregateCount
AdsAggregateValue
AdsConvertAnsiToOem
AdsConvertOemToAnsi
AdsCopyTableContent
AdsData
AdsDDCreateProcedure
AdsDDCreateView
AdsDDDropProcedure
AdsDDDropTrigger
AdsDDDropView
AdsDDGetFunctionProperty
AdsDDGetProcProperty
AdsDDGetUserTableRights
AdsDDSetFunctionProperty
AdsDDSetProcProperty
AdsDDSetRefIntegrityProperty
AdsDDSetUserTableRights
AdsDropTable
AdsFetchWhere
AdsFetchWhereApplyRow
AdsFetchWhereClose
AdsFetchWhereEof
AdsFetchWhereField
AdsFetchWhereRecno
AdsFetchWhereRows
AdsFilterOption
AdsFTSSearch
AdsGetLockCycle
AdsGetLockRetryCount
AdsGetTableConType
AdsMgGetOpenTables2
AdsReleaseSavepoint
AdsSetDeferredFlush
AdsSetEncryptionPassword
AdsSetIndexOrder
AdsSetIndexOrderByHandle
AdsSetLockCycle
AdsSetLockRetryCount
AdsStudioPort
AdsStudioStart
AdsStudioStop
AdsTestLogin
AdsTestRecLocks
```

## Remote Vs Local Behavior

OpenADS intentionally replaces SAP's split `ACE64.dll` + `AdsLoc64.dll` model
with one DLL:

- Local paths run the engine in-process.
- `tcp://` and `tls://` paths route through `openads_serverd`.
- `AdsConnect60(..., ADS_REMOTE_SERVER, ...)` prefixes a non-URI path with
  `tcp://127.0.0.1:6262/`.

Evidence in the tree:

- `README.md` states SAP ACE64 loads AdsLoc64 for `ADS_LOCAL_SERVER`, while
  OpenADS uses `ace64.dll` / `openace64.dll` as both API surface and engine.
- `src/abi/ace_exports.cpp` branches `AdsConnect60` on `tcp://` and `tls://`,
  stores `RemoteConnection` handles, and routes many table, SQL, index,
  management, filtered-scan, and aggregate calls through remote branches.
- `include/openads/ace.h` defines `ADS_LOCAL_SERVER` and `ADS_REMOTE_SERVER`.

Assessment:

- For the OpenADS-supported core surface, local and remote are designed to be
  the same client-facing API, and the implementation has remote branches for
  connection, open/close, navigation, field reads, writes, SQL execution, index
  navigation, management telemetry, filtered fetch, and aggregation.
- It is not yet accurate to say OpenACE64 works the same as SAP ACE64 for all
  local and remote behavior, because export completeness is not there and some
  supported exports are documented or implemented as no-op/stub/limited
  behavior.
- Examples of intentional gaps include SAP-original encryption formats and
  various unsupported or internal SAP helper APIs.
- Remote parity should be treated API-by-API. The strongest supported areas are
  normal table navigation, CRUD, SQL execution, index operations, management
  stats, filtered fetch, and aggregation. Missing SAP exports above have no
  guaranteed local or remote parity.

## Recommended Next Work

1. Decide the target surface:
   - "SAP public/client API only" is smaller and likely enough for most apps.
   - "Every SAP ACE64.dll export" requires adding many low-use, internal,
     W-suffix, versioned, SQL-cache, FTS, RI, DD, bookmark, raw-file,
     expression, and utility APIs.
2. Add a generated export-parity test that compares `src/openads_ace.def`
   against a checked-in allowlist derived from `tests/tools/ace64_v12.dll`.
3. For each missing name, classify as:
   - alias to an existing OpenADS implementation,
   - safe stub returning `AE_FUNCTION_NOT_AVAILABLE`,
   - local-only implementation,
   - remote+local implementation,
   - intentionally unsupported SAP internal/private export.
4. Bring `bindings/php_ext/openace64.def` back in sync with
   `src/openads_ace.def` or document that it is intentionally legacy.
