#pragma once

// OpenADS ACE-compatible C ABI — phase 1, milestone M1 subset.
// See openads/error.h for AE_* error codes.

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// SAP-shipped ace.h tags every public function with ENTRYPOINT, which
// expands to platform-specific calling-convention + import/export
// attributes. OpenADS uses plain C linkage on every export so consumers
// (Harbour's contrib/rddads, third-party Clipper apps) link the same
// import library without picking up extra ABI baggage.
#ifndef ENTRYPOINT
#  define ENTRYPOINT
#endif

typedef uint8_t  UNSIGNED8;
typedef uint16_t UNSIGNED16;
typedef uint32_t UNSIGNED32;
typedef int32_t  SIGNED32;
typedef int64_t  SIGNED64;
typedef uint64_t UNSIGNED64;
typedef uint64_t ADSHANDLE;

#define ADS_DEFAULT 0
#define ADS_NTX     1
#define ADS_CDX     2
#define ADS_ADT     3
#define ADS_VFP     4

#define ADS_LOCAL_SERVER  1
#define ADS_REMOTE_SERVER 2

UNSIGNED32 AdsConnect60     (UNSIGNED8* pucServer, UNSIGNED16 usServerType,
                              UNSIGNED8* pucUserName, UNSIGNED8* pucPassword,
                              UNSIGNED32 ulOptions, ADSHANDLE* phConnect);
UNSIGNED32 AdsDisconnect    (ADSHANDLE hConnect);

UNSIGNED32 AdsOpenTable     (ADSHANDLE  hConnect,
                              UNSIGNED8* pucName,
                              UNSIGNED8* pucAlias,
                              UNSIGNED16 usTableType,
                              UNSIGNED16 usCharType,
                              UNSIGNED16 usLockType,
                              UNSIGNED16 usCheckRights,
                              UNSIGNED16 usMode,
                              ADSHANDLE* phTable);
UNSIGNED32 AdsCloseTable    (ADSHANDLE hTable);

UNSIGNED32 AdsGotoTop       (ADSHANDLE hTable);
UNSIGNED32 AdsGotoBottom    (ADSHANDLE hTable);
UNSIGNED32 AdsGotoRecord    (ADSHANDLE hTable, UNSIGNED32 ulRecord);

UNSIGNED32 AdsGetTableType  (ADSHANDLE hTable, UNSIGNED16* pusType);
UNSIGNED32 AdsGetTableFilename(ADSHANDLE  hTable, UNSIGNED16 usOption,
                              UNSIGNED8* pucBuf, UNSIGNED16* pusLen);

UNSIGNED32 AdsCheckExistence(ADSHANDLE  hConnect, UNSIGNED8* pucName,
                              UNSIGNED16* pbExists);
UNSIGNED32 AdsDeleteFile    (ADSHANDLE  hConnect, UNSIGNED8* pucName);
UNSIGNED32 AdsCloseAllTables(ADSHANDLE  hConnect);
UNSIGNED32 AdsGetRecordLength(ADSHANDLE hTable, UNSIGNED32* pulLen);

UNSIGNED32 AdsRefreshRecord (ADSHANDLE hTable);
UNSIGNED32 AdsReindex       (ADSHANDLE hTable);
UNSIGNED32 AdsCopyTable     (ADSHANDLE  hHandle, UNSIGNED16 usFilterOption,
                              UNSIGNED8* pucFile);
UNSIGNED32 AdsCopyTableContents(ADSHANDLE hSrc, ADSHANDLE hDst);
UNSIGNED32 AdsConvertTable  (ADSHANDLE  hHandle, UNSIGNED16 usFilterOption,
                              UNSIGNED8* pucFile, UNSIGNED16 usTargetType);
UNSIGNED32 AdsFTSSearch     (ADSHANDLE   hConnect,
                              UNSIGNED8*  pucFile,
                              UNSIGNED8*  pucQuery,
                              UNSIGNED32* paRecnos,
                              UNSIGNED32* pulCount);

UNSIGNED32 AdsCreateFTSIndex(ADSHANDLE   hTable,
                              UNSIGNED8*  pucFileName,
                              UNSIGNED8*  pucTag,
                              UNSIGNED8*  pucField,
                              UNSIGNED32  ulPageSize,
                              UNSIGNED32  ulMinWordLen,
                              UNSIGNED32  ulMaxWordLen,
                              UNSIGNED16  usUseDefaultDelim,
                              UNSIGNED8*  pucDelimiters,
                              UNSIGNED16  usUseDefaultNoise,
                              UNSIGNED8*  pucNoiseWords,
                              UNSIGNED16  usUseDefaultDrop,
                              UNSIGNED8*  pucDropChars,
                              UNSIGNED16  usUseDefaultConditionals,
                              UNSIGNED8*  pucConditionalChars,
                              UNSIGNED8*  pucReserved1,
                              UNSIGNED8*  pucReserved2,
                              UNSIGNED32  ulOptions);

UNSIGNED32 AdsCreateIndex61 (ADSHANDLE  hTable, UNSIGNED8* pucFileName,
                              UNSIGNED8* pucIndexName, UNSIGNED8* pucExpr,
                              UNSIGNED8* pucCondition, UNSIGNED8* pucKeyFilter,
                              UNSIGNED32 ulOptions, UNSIGNED16 usPageSize,
                              ADSHANDLE* phIndex);
UNSIGNED32 AdsExtractKey    (ADSHANDLE hIndex, UNSIGNED8* pucBuf,
                              UNSIGNED16* pusLen);

UNSIGNED32 AdsAddCustomKey   (ADSHANDLE hIndex);
UNSIGNED32 AdsDeleteCustomKey(ADSHANDLE hIndex);

UNSIGNED32 AdsGetLongLong   (ADSHANDLE hTable, UNSIGNED8* pucField,
                              int64_t* pllValue);
UNSIGNED32 AdsSetFieldRaw   (ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED8* pucBuf, UNSIGNED32 ulLen);
UNSIGNED32 AdsVerifySQL     (ADSHANDLE  hStatement, UNSIGNED8* pucSQL);
UNSIGNED32 AdsFailedTransactionRecovery(UNSIGNED8* pucServer);
UNSIGNED32 AdsGetAllLocks   (ADSHANDLE hTable, UNSIGNED32* paRecnos,
                              UNSIGNED16* pusCount);
UNSIGNED32 AdsSkipUnique    (ADSHANDLE hIndex, SIGNED32 lDirection);

UNSIGNED32 AdsCreateTable   (ADSHANDLE  hConnect, UNSIGNED8* pucName,
                              UNSIGNED8* pucAlias,
                              UNSIGNED16 usTableType, UNSIGNED16 usCharType,
                              UNSIGNED16 usLockType, UNSIGNED16 usCheckRights,
                              UNSIGNED16 usMemoBlockSize,
                              UNSIGNED8* pucFields,
                              ADSHANDLE* phTable);
UNSIGNED32 AdsRestructureTable(ADSHANDLE  hConnect, UNSIGNED8* pucTableName,
                              UNSIGNED8* pucAlias,
                              UNSIGNED16 usFileType, UNSIGNED16 usCharType,
                              UNSIGNED16 usLockType, UNSIGNED16 usCheckRights,
                              UNSIGNED8* pucAddFields,
                              UNSIGNED8* pucDeleteFields,
                              UNSIGNED8* pucChangeFields);
UNSIGNED32 AdsSkip          (ADSHANDLE hTable, SIGNED32 lRows);
UNSIGNED32 AdsAtEOF         (ADSHANDLE hTable, UNSIGNED16* pbAtEnd);
UNSIGNED32 AdsAtBOF         (ADSHANDLE hTable, UNSIGNED16* pbAtBegin);

UNSIGNED32 AdsGetField      (ADSHANDLE  hTable, UNSIGNED8* pucField,
                              UNSIGNED8* pucBuf, UNSIGNED32* pulLen,
                              UNSIGNED16 usOption);
UNSIGNED32 AdsGetFieldName  (ADSHANDLE  hTable, UNSIGNED16 usFieldNum,
                              UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
UNSIGNED32 AdsGetNumFields  (ADSHANDLE  hTable, UNSIGNED16* pusFields);
UNSIGNED32 AdsGetFieldType  (ADSHANDLE  hTable, UNSIGNED8* pucField,
                              UNSIGNED16* pusType);
UNSIGNED32 AdsGetFieldLength(ADSHANDLE  hTable, UNSIGNED8* pucField,
                              UNSIGNED32* pulLen);
UNSIGNED32 AdsGetRecordNum  (ADSHANDLE  hTable, UNSIGNED16 bFilterOption,
                              UNSIGNED32* pulRecordNum);
UNSIGNED32 AdsGetRecordCount(ADSHANDLE  hTable, UNSIGNED16 bFilterOption,
                              UNSIGNED32* pulRecordCount);

UNSIGNED32 AdsGetLastError  (UNSIGNED32* pulCode, UNSIGNED8* pucBuf,
                              UNSIGNED16* pusBufLen);

UNSIGNED32 AdsGetVersion    (UNSIGNED32* pulMajor, UNSIGNED32* pulMinor,
                              UNSIGNED32* pulLetter, UNSIGNED32* pulDesc);

UNSIGNED32 AdsGetServerName (ADSHANDLE   hConnect,
                              UNSIGNED8*  pucBuf, UNSIGNED16* pusLen);
UNSIGNED32 AdsGetServerTime (ADSHANDLE   hConnect,
                              UNSIGNED8*  pucDateBuf, UNSIGNED16* pusDateLen,
                              SIGNED32*   plTime,
                              UNSIGNED8*  pucTimeBuf, UNSIGNED16* pusTimeLen);

UNSIGNED32 AdsAppendRecord  (ADSHANDLE hTable);
UNSIGNED32 AdsWriteRecord   (ADSHANDLE hTable);
UNSIGNED32 AdsDeleteRecord  (ADSHANDLE hTable);
UNSIGNED32 AdsRecallRecord  (ADSHANDLE hTable);
UNSIGNED32 AdsIsRecordDeleted(ADSHANDLE hTable, UNSIGNED16* pbDeleted);

UNSIGNED32 AdsSetString     (ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED8* pucValue, UNSIGNED32 ulLen);
UNSIGNED32 AdsSetStringW    (ADSHANDLE  hTable, UNSIGNED16* pucFieldW,
                              UNSIGNED16* pucValueW, UNSIGNED32 ulLen);
UNSIGNED32 AdsGetStringW    (ADSHANDLE  hTable, UNSIGNED16* pucFieldW,
                              UNSIGNED16* pucBufW, UNSIGNED32* pulLenW,
                              UNSIGNED16 usOption);
UNSIGNED32 AdsGetFieldW     (ADSHANDLE  hTable, UNSIGNED16* pucFieldW,
                              UNSIGNED16* pucBufW, UNSIGNED32* pulLenW,
                              UNSIGNED16 usOption);
UNSIGNED32 AdsSetLogical    (ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED16 bValue);
UNSIGNED32 AdsSetDouble     (ADSHANDLE hTable, UNSIGNED8* pucField,
                              double dValue);

UNSIGNED32 AdsLockRecord    (ADSHANDLE hTable, UNSIGNED32 ulRecord);
UNSIGNED32 AdsUnlockRecord  (ADSHANDLE hTable, UNSIGNED32 ulRecord);
UNSIGNED32 AdsLockTable     (ADSHANDLE hTable);
UNSIGNED32 AdsUnlockTable   (ADSHANDLE hTable);

UNSIGNED32 AdsSetLockCycle      (ADSHANDLE hConnect, UNSIGNED32 ulCycle);
UNSIGNED32 AdsGetLockCycle      (ADSHANDLE hConnect, UNSIGNED32* pulCycle);
UNSIGNED32 AdsSetLockRetryCount (ADSHANDLE hConnect, UNSIGNED16 usRetryCount);
UNSIGNED32 AdsGetLockRetryCount (ADSHANDLE hConnect, UNSIGNED16* pusRetryCount);

UNSIGNED32 AdsFlushFileBuffers(ADSHANDLE hTable);

UNSIGNED32 AdsOpenIndex     (ADSHANDLE hTable, UNSIGNED8* pucName,
                              ADSHANDLE* ahIndex,
                              UNSIGNED16* pu16ArrayLen);
UNSIGNED32 AdsCloseIndex    (ADSHANDLE hIndex);
UNSIGNED32 AdsCloseAllIndexes(ADSHANDLE hTable);
UNSIGNED32 AdsCreateIndex   (ADSHANDLE hTable, UNSIGNED8* pucFile,
                              UNSIGNED8* pucTag, UNSIGNED8* pucExpr,
                              UNSIGNED8* pucCondition, UNSIGNED32 ulOptions,
                              UNSIGNED16 usKeyType, ADSHANDLE* phIndex);
UNSIGNED32 AdsDeleteIndex   (ADSHANDLE hIndex);
UNSIGNED32 AdsGetNumIndexes (ADSHANDLE hTable, UNSIGNED16* pusCount);
UNSIGNED32 AdsGetIndexHandle(ADSHANDLE hTable, UNSIGNED8* pucName,
                              ADSHANDLE* phIndex);
UNSIGNED32 AdsGetIndexHandleByOrder(ADSHANDLE hTable, UNSIGNED16 usOrder,
                                    ADSHANDLE* phIndex);
UNSIGNED32 AdsGetIndexExpr  (ADSHANDLE hIndex, UNSIGNED8* pucBuf,
                              UNSIGNED16* pusBufLen);
UNSIGNED32 AdsGetIndexName  (ADSHANDLE hIndex, UNSIGNED8* pucBuf,
                              UNSIGNED16* pusBufLen);
UNSIGNED32 AdsSetIndexDirection(ADSHANDLE hIndex, UNSIGNED16 usDir);

UNSIGNED32 AdsSeek          (ADSHANDLE hIndex, UNSIGNED8* pucKey,
                              UNSIGNED16 usKeyLen, UNSIGNED16 usKeyType,
                              UNSIGNED16 usSeekType, UNSIGNED16* pbFound);
UNSIGNED32 AdsSeekLast      (ADSHANDLE hIndex, UNSIGNED8* pucKey,
                              UNSIGNED16 usKeyLen, UNSIGNED16 usKeyType,
                              UNSIGNED16* pbFound);

UNSIGNED32 AdsSetScope      (ADSHANDLE hIndex, UNSIGNED16 usScope,
                              UNSIGNED8* pucKey);
UNSIGNED32 AdsClearScope    (ADSHANDLE hIndex, UNSIGNED16 usScope);
UNSIGNED32 AdsGetScope      (ADSHANDLE hIndex, UNSIGNED16 usScope,
                              UNSIGNED8* pucBuf, UNSIGNED16* pusLen);

UNSIGNED32 AdsPackTable     (ADSHANDLE hTable);
UNSIGNED32 AdsZapTable      (ADSHANDLE hTable);
UNSIGNED32 AdsSetAOF        (ADSHANDLE hTable, UNSIGNED8* pucCondition,
                              UNSIGNED16 usResolve);
UNSIGNED32 AdsGetAOFOptLevel(ADSHANDLE hTable, UNSIGNED16* pusLevel,
                              UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
UNSIGNED32 AdsClearAOF      (ADSHANDLE hTable);

UNSIGNED32 AdsGetMemoLength    (ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED32* pulLen);
UNSIGNED32 AdsGetMemoDataType  (ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED16* pusType);
UNSIGNED32 AdsBinaryToFile     (ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED8* pucPath);
UNSIGNED32 AdsFileToBinary     (ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED16 usType, UNSIGNED8* pucPath);
UNSIGNED32 AdsGetBinaryLength  (ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED32* pulLength);
UNSIGNED32 AdsGetBinary        (ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED32 ulOffset, UNSIGNED8* pucBuf,
                                 UNSIGNED32* pulLen);
UNSIGNED32 AdsSetBinary        (ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED16 usBinaryType,
                                 UNSIGNED32 ulTotalBytes,
                                 UNSIGNED32 ulOffset,
                                 UNSIGNED8* pucBuf, UNSIGNED32 ulBytes);
UNSIGNED32 AdsGetLastAutoinc   (ADSHANDLE hTable, UNSIGNED32* pulValue);

UNSIGNED32 AdsEnableEncryption (ADSHANDLE hConnect, UNSIGNED8* pucPassword);
UNSIGNED32 AdsDisableEncryption(ADSHANDLE hConnect);
UNSIGNED32 AdsIsEncryptionEnabled(ADSHANDLE hConnect, UNSIGNED16* pbEnabled);
UNSIGNED32 AdsIsTableEncrypted (ADSHANDLE hTable, UNSIGNED16* pbEncrypted);
UNSIGNED32 AdsIsRecordEncrypted(ADSHANDLE hTable, UNSIGNED16* pbEncrypted);
UNSIGNED32 AdsEncryptTable     (ADSHANDLE hTable);
UNSIGNED32 AdsDecryptTable     (ADSHANDLE hTable);
UNSIGNED32 AdsEncryptRecord    (ADSHANDLE hTable);
UNSIGNED32 AdsDecryptRecord    (ADSHANDLE hTable);

UNSIGNED32 AdsBeginTransaction   (ADSHANDLE hConnect);
UNSIGNED32 AdsCommitTransaction  (ADSHANDLE hConnect);
UNSIGNED32 AdsRollbackTransaction(ADSHANDLE hConnect);
UNSIGNED32 AdsInTransaction      (ADSHANDLE hConnect, UNSIGNED16* pbInTx);

UNSIGNED32 AdsCreateSavepoint    (ADSHANDLE hConnect, UNSIGNED8* pucName);
UNSIGNED32 AdsReleaseSavepoint   (ADSHANDLE hConnect, UNSIGNED8* pucName);
UNSIGNED32 AdsSetEncryptionPassword(ADSHANDLE hConnect, UNSIGNED8* pucPassword);
UNSIGNED32 AdsSetCollation       (ADSHANDLE hConnect, UNSIGNED8* pucName);
UNSIGNED32 AdsConvertOemToAnsi   (UNSIGNED8* pucBuf, UNSIGNED32* pulLen);
UNSIGNED32 AdsConvertAnsiToOem   (UNSIGNED8* pucBuf, UNSIGNED32* pulLen);
UNSIGNED32 AdsRollbackTransaction80(ADSHANDLE hConnect, UNSIGNED8* pucSavepoint);

UNSIGNED32 AdsFindFirstTable     (ADSHANDLE   hConnect,
                                  UNSIGNED8*  pucMask,
                                  UNSIGNED8*  pucFileName,
                                  UNSIGNED16* pusFileNameLen,
                                  ADSHANDLE*  phFind);
UNSIGNED32 AdsFindNextTable      (ADSHANDLE   hConnect,
                                  ADSHANDLE   hFind,
                                  UNSIGNED8*  pucFileName,
                                  UNSIGNED16* pusFileNameLen);
UNSIGNED32 AdsFindClose          (ADSHANDLE hConnect, ADSHANDLE hFind);

UNSIGNED32 AdsDDCreate           (UNSIGNED8* pucDictionary,
                                  UNSIGNED16 bEncrypt,
                                  UNSIGNED8* pucAdminPassword,
                                  ADSHANDLE* phConnect);
UNSIGNED32 AdsDDAddTable         (ADSHANDLE hConnect,
                                  UNSIGNED8* pucAlias,
                                  UNSIGNED8* pucTablePath,
                                  UNSIGNED8* pucIndexPath,
                                  UNSIGNED16 usCharType,
                                  UNSIGNED8* pucDescription,
                                  UNSIGNED8* pucValidationExpression,
                                  UNSIGNED8* pucValidationMessage);
UNSIGNED32 AdsDDRemoveTable      (ADSHANDLE hConnect,
                                  UNSIGNED8* pucAlias,
                                  UNSIGNED16 usDeleteFiles);

UNSIGNED32 AdsCreateSQLStatement (ADSHANDLE hConnect, ADSHANDLE* phStatement);
UNSIGNED32 AdsCloseSQLStatement  (ADSHANDLE hStatement);
UNSIGNED32 AdsPrepareSQL         (ADSHANDLE hStatement, UNSIGNED8* pucSQL);
UNSIGNED32 AdsExecuteSQL         (ADSHANDLE hStatement, ADSHANDLE* phCursor);
UNSIGNED32 AdsExecuteSQLDirect   (ADSHANDLE hStatement, UNSIGNED8* pucSQL,
                                  ADSHANDLE* phCursor);

#define ADS_TOP            0
#define ADS_BOTTOM         1
#define ADS_SOFTSEEK       1
#define ADS_OPTIMIZED_NONE 3
// Memo content kinds returned by AdsGetMemoDataType. Match the
// numeric values rddads switches against in its HB_FT_MEMO branch.
// (See ADS_BINARY / ADS_IMAGE further below for the binary kinds.)
#define ADS_MEMO_TEXT      4   // alias for ADS_STRING; text memo
#define ADS_MEMO_PICTURE   7   // alias for ADS_IMAGE; binary memo

// ACE field-type constants.
//
// Values empirically verified (M8.4) against the prebuilt
// `c:\harbour\lib\win\msvc64\rddads.lib` by sweeping `AdsGetFieldType`'s
// return through 0..40 and observing what Clipper field type the
// `adsOpen` switch routes each value to. These are the numeric
// values rddads.lib decodes; OpenADS publishes the same constants so
// applications built against rddads see the contract they expect.
//
//   value | rddads dbFieldInfo.uiType -> Clipper FieldType()
//   ------+--------------------------------------------------
//     1   | HB_FT_LOGICAL          'L'
//     2   | HB_FT_LONG / numeric   'N'
//     3   | HB_FT_DATE             'D'
//     4   | HB_FT_STRING           'C'   <- standard char
//     5   | HB_FT_MEMO             'M'
//     6   | HB_FT_BLOB             'W'
//     7   | HB_FT_IMAGE            'P'
//     8   | HB_FT_VARLENGTH        'Q'
//     9   | HB_FT_DATE             'D'   (compactdate)
//    10   | HB_FT_DOUBLE           'B'
//    11   | HB_FT_INTEGER          'I'
//    12   | HB_FT_INTEGER          'I'   (shortint alias)
//    13   | HB_FT_TIME             'T'
//    14   | HB_FT_TIMESTAMP        '@'
//    15   | HB_FT_AUTOINC          '+'
//    16   | HB_FT_STRING           'C'   (alias)
//    17   | HB_FT_CURDOUBLE        'Z'
//    18   | HB_FT_CURRENCY         'Y'   (money)
//    19   | HB_FT_INTEGER          'I'   (longlong)
//    20   | HB_FT_STRING           'CICHARACTER'  (case-insensitive)
//    21   | HB_FT_ROWVER           '^'
//    22   | HB_FT_MODTIME          '='
//    23   | HB_FT_VARLENGTH        'Q'   (varchar fox)
//    24   | HB_FT_VARLENGTH        'Q'   (varbinary fox)
//    26   | HB_FT_STRING           'C'   (nchar / unicode)
//    27   | HB_FT_VARLENGTH        'Q'   (nvarchar)
//    28   | HB_FT_MEMO             'M'   (nmemo)
//    other| EDBF_CORRUPT (open fails)
#define ADS_LOGICAL               1
#define ADS_NUMERIC               2
#define ADS_DATE                  3
#define ADS_STRING                4
#define ADS_MEMO                  5
#define ADS_BINARY                6
#define ADS_RAW                   6
#define ADS_IMAGE                 7
#define ADS_VARCHAR_FOX           8
#define ADS_COMPACTDATE           9
#define ADS_DOUBLE               10
#define ADS_INTEGER              11
#define ADS_SHORTINT             12
#define ADS_TIME                 13
#define ADS_TIMESTAMP            14
#define ADS_AUTOINC              15
#define ADS_CURDOUBLE            17
#define ADS_MONEY                18
#define ADS_LONGLONG             19
#define ADS_CISTRING             20
#define ADS_ROWVERSION           21
#define ADS_MODTIME              22
#define ADS_VARBINARY_FOX        24
#define ADS_NCHAR                26
#define ADS_NVARCHAR             27
#define ADS_NMEMO                28
// Legacy ADS_FIELD_TYPE_* aliases kept for the engine-side code that
// pre-dates the ace.h authoritative constants.
#define ADS_FIELD_TYPE_CHAR       ADS_STRING
#define ADS_FIELD_TYPE_NUMERIC    ADS_NUMERIC
#define ADS_FIELD_TYPE_LOGICAL    ADS_LOGICAL
#define ADS_FIELD_TYPE_DATE       ADS_DATE
#define ADS_FIELD_TYPE_DATETIME   ADS_TIMESTAMP
#define ADS_FIELD_TYPE_MEMO       ADS_MEMO
#define ADS_FIELD_TYPE_INTEGER    ADS_INTEGER
#define ADS_FIELD_TYPE_DOUBLE     ADS_DOUBLE
#define ADS_FIELD_TYPE_CURRENCY   ADS_MONEY
#define ADS_FIELD_TYPE_UNKNOWN    99

// ---- Constants required by Harbour contrib/rddads ----
// These mirror the SAP-shipped ace.h constants whose numeric values
// are documented in the Advantage Database SDK and reproduced here
// from public Sybase / SAP documentation. OpenADS is the receiving
// (server) side; the values must match what the standard rddads
// contrib expects so existing Harbour applications and tests
// (rddtst.prg, cdxcl52.prg, ntxcl52.prg) compile and run unchanged.

// Index option flags (AdsCreateIndex61 ulOptions).
#define ADS_DEFAULT_INDEX  0x00000000
#define ADS_UNIQUE         0x00000001
#define ADS_DESCENDING     0x00000002
#define ADS_CUSTOM         0x00000004
#define ADS_COMPOUND       0x00000008
#define ADS_FTS_INDEX_ORDER 0x00000010
#define ADS_INDEX_ORDER    0x00000020

// Lock / share modes.
#define ADS_EXCLUSIVE      1
#define ADS_SHARED         2
#define ADS_READONLY       3
#define ADS_DEFAULT_MODE   0

// Char-type / character-set codes.
#define ADS_ANSI           1
#define ADS_OEM            2

// Filter / scope flags.
#define ADS_RESPECTSCOPES   1
#define ADS_RESPECTFILTERS  2
#define ADS_IGNOREFILTERS   3
#define ADS_IGNORERIGHTS    4
// ADS_HARDSEEK is the OPPOSITE of ADS_SOFTSEEK (defined at line 320 as 1).
// rddads passes one or the other to AdsSeek* etc. as a single bit-flag.
#define ADS_HARDSEEK        0
#define ADS_CHECKRIGHTS     1
#define ADS_OPTIMIZED_FULL  1
#define ADS_OPTIMIZED_PART  2
#define ADS_AOF_ADD_RECORD  1

// Handle classification (AdsGetHandleType).
#define ADS_NONE              0
#define ADS_TABLE             1
#define ADS_STATEMENT         2
#define ADS_CURSOR            4
#define ADS_DATABASE_CONNECTION 6
#define ADS_SYS_ADMIN_CONNECTION 7

// Server / connection types (AdsGetConnectionType).
#define ADS_AIS_SERVER     1
#define ADS_LINUX          2
#define ADS_NOTIFICATION_CONNECTION 4
#define ADS_REPLICATION_CONNECTION  8
#define ADS_UDP_IP_CONNECTION       16
#define ADS_DEFAULT_SQL_TIMEOUT     30

// Locking models (AdsSetLockingMode).
#define ADS_PROPRIETARY_LOCKING  1
#define ADS_COMPATIBLE_LOCKING   2

// AdsGetTableFilename `usOption` values.
#define ADS_FULLPATHNAME    1
#define ADS_BASENAMEANDEXT  2
#define ADS_BASENAME        3

// SQL key types (AdsGetKeyType).
#define ADS_RAWKEY     0
#define ADS_STRINGKEY  1
#define ADS_DOUBLEKEY  2

// Misc query / filter flags.
#define ADS_DEFCONNECTION    1
#define ADS_GETCONNECTION    1
#define ADS_PUTCONNECTION    2
#define ADS_RETCONNECTION    3
#define ADS_PARCONNECTION    4
#define ADS_REFRESHCOUNT     1
#define ADS_GETFUNCTABLE     1
#define ADS_THREAD_DATA      1
#define ADS_TSD_CONNECTION   2
#define ADS_CONN_DATA        3

// Resolution / connection options.
#define ADS_RESOLVE_DYNAMIC   0
#define ADS_RESOLVE_IMMEDIATE 1
#define ADS_NOT_AUTO_OPEN     0x00000004
#define ADS_READ_ALL_COLUMNS  0x00000010
#define ADS_COMPRESS_ALWAYS   0x00000020
#define ADS_GET_FORMAT_WEB    0x00000040
#define ADS_GET_UTF8          0x00000080
#define ADS_ROOT_DD_ALIAS     0x00000100
#define ADS_DD_VERSION        0x00000001
#define ADS_DD_VERSION_MAJOR  0x00000002
#define ADS_DD_VERSION_MINOR  0x00000003
#define ADS_USER_DEFINED      0x00000200
// rddads.h defines ADS_USE_OEM_TRANSLATION conditionally; do not
// redefine here. Field type ADS_VARCHAR is 23 in the public SAP
// documentation (the ace.h above uses 13 for ADS_TIME).
#define ADS_VARCHAR           23
#define ADS_CURSOR_READONLY   1
#define ADS_CS_AS_1252        1
#define ADS_VERSION_STRING    "1.0.0-rc6"

// Character-set table size limits.
#define ADS_MAX_CHAR_SETS         32
#define ADS_MAX_DBF_FIELD_NAME    10
#define ADS_MAX_FIELD_NAME       128
#define ADS_MAX_TABLE_NAME       255
#define ADS_MAX_TAG_NAME          10
#define ADS_MAX_KEY_LENGTH       240
#define ADS_MAX_PARAMDEF_LEN     256
#define ADS_MAX_ERROR_LEN        320

// Data Dictionary string-property keys.
#define ADS_DD_COMMENT                       1
#define ADS_DD_ADMIN_PASSWORD                2
#define ADS_DD_DEFAULT_TABLE_PATH            3
#define ADS_DD_TEMP_TABLE_PATH               4
#define ADS_DD_LOG_IN_REQUIRED               5
#define ADS_DD_VERIFY_ACCESS_RIGHTS          6
#define ADS_DD_ENCRYPT_NEW_TABLE             7
#define ADS_DD_ENCRYPT_TABLE_PASSWORD        8
#define ADS_DD_ENCRYPT_INDEXES               9
#define ADS_DD_ENCRYPT_COMMUNICATION         10
#define ADS_DD_ENCRYPTED                     11
#define ADS_DD_ENABLE_INTERNET               12
#define ADS_DD_INTERNET_SECURITY_LEVEL       13
#define ADS_DD_LOGINS_DISABLED               14
#define ADS_DD_LOGINS_DISABLED_ERRSTR        15
#define ADS_DD_DISABLE_DLL_CACHING           16
#define ADS_DD_FTS_DELIMITERS                17
#define ADS_DD_FTS_NOISE                     18
#define ADS_DD_FTS_DROP_CHARS                19
#define ADS_DD_FTS_CONDITIONAL_CHARS         20
#define ADS_DD_MAX_FAILED_ATTEMPTS           21
#define ADS_DD_USER_DEFINED_PROP             22

// Management API call selectors (AdsMgGet*).
#define ADS_MGMT_                  0
#define ADS_MGMT_FILE_LOCK         1
#define ADS_MGMT_RECORD_LOCK       2
#define ADS_MGMT_NO_LOCK           3
#define ADS_MGMT_TABLE_INFO        4
#define ADS_MGMT_INDEX_INFO        5
#define ADS_MGMT_RECORD_INFO       6
#define ADS_MGMT_USER_INFO         7
#define ADS_MGMT_INSTALL_INFO      8
#define ADS_MGMT_CONFIG_PARAMS     9
#define ADS_MGMT_CONFIG_MEMORY     10
#define ADS_MGMT_ACTIVITY_INFO     11
#define ADS_MGMT_COMM_STATS        12
#define ADS_MGMT_THREAD_ACTIVITY   13

// AE_* error code macros. The canonical enum lives in
// openads/error.h, but rddads includes only ace.h so these need
// preprocessor visibility too. error.h #undef's the duplicates
// before declaring its enum, so both surfaces stay coherent.
#ifndef AE_SUCCESS
#  define AE_SUCCESS               0
#endif
#define AE_INTERNAL_ERROR          5000
#define AE_FUNCTION_NOT_AVAILABLE  5004
#define AE_LOCKED                  5012
#define AE_LOCK_FAILED             5013

// Extra AE_* error codes referenced by Harbour rddads.
#define AE_INVALID_HANDLE          5024
#define AE_INVALID_RECORD_NUMBER   5025
#define AE_NO_CURRENT_RECORD       5026
#define AE_TABLE_NOT_LOCKED        5034
#define AE_RECORD_NOT_LOCKED       5035
#define AE_TABLE_NOT_SHARED        5036
#define AE_TABLE_READONLY          5037
#define AE_INSUFFICIENT_BUFFER     5051
#define AE_DATA_TOO_LONG           5054
#define AE_DATA_TRUNCATED          5055
#define AE_VALUE_OVERFLOW          5057
#define AE_INVALID_EXPRESSION      5079
#define AE_INDEX_ALREADY_OPEN      5108

// ---- Function declarations required by Harbour contrib/rddads ----
// Many of these are already implemented in src/abi/ace_exports.cpp;
// the declarations are added here so the standard rddads contrib
// builds against this header. Functions whose engine support is not
// yet available return AE_FUNCTION_NOT_AVAILABLE (5004).

UNSIGNED32 ENTRYPOINT AdsConnect           (UNSIGNED8* pucServer,
                                             ADSHANDLE* phConnect);
UNSIGNED32 ENTRYPOINT AdsApplicationExit   (void);
UNSIGNED32 ENTRYPOINT AdsClearFilter       (ADSHANDLE hTable);
UNSIGNED32 ENTRYPOINT AdsClearRelation     (ADSHANDLE hTable);
UNSIGNED32 ENTRYPOINT AdsClearCallbackFunction(void);
UNSIGNED32 ENTRYPOINT AdsClearProgressCallback(void);
UNSIGNED32 ENTRYPOINT AdsCacheOpenCursors  (UNSIGNED16 usCacheCount);
UNSIGNED32 ENTRYPOINT AdsCacheOpenTables   (UNSIGNED16 usCacheCount);
UNSIGNED32 ENTRYPOINT AdsCacheRecords      (ADSHANDLE hTable,
                                             UNSIGNED16 usRecCount);
UNSIGNED32 ENTRYPOINT AdsCloseCachedTables (ADSHANDLE hConnect);
UNSIGNED32 ENTRYPOINT AdsCopyTableContent  (ADSHANDLE hSrc, ADSHANDLE hDst);
UNSIGNED32 ENTRYPOINT AdsCustomizeAOF      (ADSHANDLE hTable,
                                             UNSIGNED32 ulNumRecords,
                                             UNSIGNED32* pulRecords,
                                             UNSIGNED16 usOption);
UNSIGNED32 ENTRYPOINT AdsData              (UNSIGNED16 usFlag,
                                             void* pvData);
UNSIGNED32 ENTRYPOINT AdsEvalAOF           (ADSHANDLE hTable,
                                             UNSIGNED8* pucExpr,
                                             UNSIGNED32* pulRecords);
UNSIGNED32 ENTRYPOINT AdsFilterOption      (ADSHANDLE hTable,
                                             UNSIGNED16 usOption,
                                             UNSIGNED16* pusValue);
UNSIGNED32 ENTRYPOINT AdsGetAOF            (ADSHANDLE hTable,
                                             UNSIGNED32* pulRecords,
                                             UNSIGNED32* pulCount);
UNSIGNED32 ENTRYPOINT AdsGetConnectionType (ADSHANDLE hConnect,
                                             UNSIGNED16* pusType);
UNSIGNED32 ENTRYPOINT AdsGetDateFormat     (UNSIGNED8* pucBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetDefault        (UNSIGNED8* pucBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetDeleted        (UNSIGNED16* pbShow);
UNSIGNED32 ENTRYPOINT AdsGetDouble         (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             double* pdValue);
UNSIGNED32 ENTRYPOINT AdsGetEpoch          (UNSIGNED16* pusEpoch);
UNSIGNED32 ENTRYPOINT AdsGetErrorString    (UNSIGNED32 ulErr,
                                             UNSIGNED8* pucBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetExact          (UNSIGNED16* pbExact);
UNSIGNED32 ENTRYPOINT AdsGetFieldDecimals  (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             UNSIGNED16* pusDec);
UNSIGNED32 ENTRYPOINT AdsGetFieldRaw       (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             UNSIGNED8* pucBuf,
                                             UNSIGNED32* pulLen);
UNSIGNED32 ENTRYPOINT AdsGetFilter         (ADSHANDLE hTable,
                                             UNSIGNED8* pucBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetHandleType     (ADSHANDLE hAny,
                                             UNSIGNED16* pusType);
UNSIGNED32 ENTRYPOINT AdsGetIndexCondition (ADSHANDLE hIndex,
                                             UNSIGNED8* pucBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetIndexFilename  (ADSHANDLE hIndex,
                                             UNSIGNED16 usOption,
                                             UNSIGNED8* pucBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetIndexOrderByHandle(ADSHANDLE hIndex,
                                             UNSIGNED16* pusOrder);
UNSIGNED32 ENTRYPOINT AdsGetJulian         (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             SIGNED32* plJulian);
UNSIGNED32 ENTRYPOINT AdsGetKeyLength      (ADSHANDLE hIndex,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetKeyNum         (ADSHANDLE hIndex,
                                             UNSIGNED16 usFlag,
                                             UNSIGNED32* pulKey);
UNSIGNED32 ENTRYPOINT AdsGetKeyType        (ADSHANDLE hIndex,
                                             UNSIGNED16* pusType);
UNSIGNED32 ENTRYPOINT AdsGetLastTableUpdate(ADSHANDLE hTable,
                                             SIGNED32* plDate, SIGNED32* plTime);
UNSIGNED32 ENTRYPOINT AdsGetLogical        (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             UNSIGNED16* pbValue);
UNSIGNED32 ENTRYPOINT AdsGetMilliseconds   (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             SIGNED32* plMs);
UNSIGNED32 ENTRYPOINT AdsGetNumActiveLinks (ADSHANDLE hTable,
                                             UNSIGNED16* pusCount);
UNSIGNED32 ENTRYPOINT AdsGetNumLocks       (ADSHANDLE hTable,
                                             UNSIGNED16* pusCount);
UNSIGNED32 ENTRYPOINT AdsGetNumOpenTables  (ADSHANDLE hConnect,
                                             UNSIGNED16* pusCount);
UNSIGNED32 ENTRYPOINT AdsGetRecord         (ADSHANDLE hTable,
                                             UNSIGNED8* pucBuf,
                                             UNSIGNED32* pulLen);
UNSIGNED32 ENTRYPOINT AdsGetRelKeyPos      (ADSHANDLE hIndex,
                                             double* pdPos);
UNSIGNED32 ENTRYPOINT AdsGetSearchPath     (UNSIGNED8* pucBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetTableAlias     (ADSHANDLE hTable,
                                             UNSIGNED8* pucBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetTableCharType  (ADSHANDLE hTable,
                                             UNSIGNED16* pusType);
UNSIGNED32 ENTRYPOINT AdsGetTableConType   (ADSHANDLE hTable,
                                             UNSIGNED16* pusType);
UNSIGNED32 ENTRYPOINT AdsGetTableConnection(ADSHANDLE hTable,
                                             ADSHANDLE* phConnect);
UNSIGNED32 ENTRYPOINT AdsIsConnectionAlive (ADSHANDLE hConnect,
                                             UNSIGNED16* pbAlive);
UNSIGNED32 ENTRYPOINT AdsIsEmpty           (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             UNSIGNED16* pbEmpty);
UNSIGNED32 ENTRYPOINT AdsIsExprValid       (ADSHANDLE hTable,
                                             UNSIGNED8* pucExpr,
                                             UNSIGNED16* pbValid);
UNSIGNED32 ENTRYPOINT AdsIsFound           (ADSHANDLE hTable,
                                             UNSIGNED16* pbFound);
UNSIGNED32 ENTRYPOINT AdsIsIndexCustom     (ADSHANDLE hIndex,
                                             UNSIGNED16* pbCustom);
UNSIGNED32 ENTRYPOINT AdsIsIndexDescending (ADSHANDLE hIndex,
                                             UNSIGNED16* pbDesc);
UNSIGNED32 ENTRYPOINT AdsIsIndexUnique     (ADSHANDLE hIndex,
                                             UNSIGNED16* pbUnique);
UNSIGNED32 ENTRYPOINT AdsIsNull            (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             UNSIGNED16* pbNull);
UNSIGNED32 ENTRYPOINT AdsIsRecordInAOF     (ADSHANDLE hTable,
                                             UNSIGNED32 ulRecord,
                                             UNSIGNED16* pbInAOF);
UNSIGNED32 ENTRYPOINT AdsIsRecordLocked    (ADSHANDLE hTable,
                                             UNSIGNED32 ulRecord,
                                             UNSIGNED16* pbLocked);
UNSIGNED32 ENTRYPOINT AdsIsServerLoaded    (UNSIGNED8* pucServer,
                                             UNSIGNED16* pbLoaded);
UNSIGNED32 ENTRYPOINT AdsIsTableLocked     (ADSHANDLE hTable,
                                             UNSIGNED16* pbLocked);
UNSIGNED32 ENTRYPOINT AdsRefreshAOF        (ADSHANDLE hTable);
UNSIGNED32 ENTRYPOINT AdsRegisterCallbackFunction(void* pCallback);
UNSIGNED32 ENTRYPOINT AdsRegisterProgressCallback(void* pCallback);
UNSIGNED32 ENTRYPOINT AdsSetDateFormat     (UNSIGNED8* pucFormat);
UNSIGNED32 ENTRYPOINT AdsSetDecimals       (UNSIGNED16 usDecimals);
UNSIGNED32 ENTRYPOINT AdsSetDefault        (UNSIGNED8* pucDir);
UNSIGNED32 ENTRYPOINT AdsSetEpoch          (UNSIGNED16 usEpoch);
UNSIGNED32 ENTRYPOINT AdsSetExact          (UNSIGNED16 bExact);
UNSIGNED32 ENTRYPOINT AdsSetFilter         (ADSHANDLE hTable,
                                             UNSIGNED8* pucExpr);
UNSIGNED32 ENTRYPOINT AdsSetJulian         (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             SIGNED32 lJulian);
UNSIGNED32 ENTRYPOINT AdsSetLongLong       (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             int64_t llValue);
UNSIGNED32 ENTRYPOINT AdsSetMilliseconds   (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             SIGNED32 lMs);
UNSIGNED32 ENTRYPOINT AdsSetRecord         (ADSHANDLE hTable,
                                             UNSIGNED8* pucBuf,
                                             UNSIGNED32 ulLen);
UNSIGNED32 ENTRYPOINT AdsSetRelKeyPos      (ADSHANDLE hIndex, double dPos);
UNSIGNED32 ENTRYPOINT AdsSetRelation       (ADSHANDLE hParent,
                                             ADSHANDLE hChild,
                                             UNSIGNED8* pucExpr);
UNSIGNED32 ENTRYPOINT AdsSetScopedRelation (ADSHANDLE hParent,
                                             ADSHANDLE hChild,
                                             UNSIGNED8* pucExpr);
UNSIGNED32 ENTRYPOINT AdsSetSearchPath     (UNSIGNED8* pucPath);
UNSIGNED32 ENTRYPOINT AdsSetServerType     (UNSIGNED16 usType);
UNSIGNED32 ENTRYPOINT AdsShowDeleted       (UNSIGNED16 bShow);
UNSIGNED32 ENTRYPOINT AdsShowError         (UNSIGNED8* pucCaption);
UNSIGNED32 ENTRYPOINT AdsStmtSetTableLockType(ADSHANDLE hStmt,
                                             UNSIGNED16 usType);
UNSIGNED32 ENTRYPOINT AdsStmtSetTablePassword(ADSHANDLE hStmt,
                                             UNSIGNED8* pucName,
                                             UNSIGNED8* pucPwd);
UNSIGNED32 ENTRYPOINT AdsStmtSetTableReadOnly(ADSHANDLE hStmt,
                                             UNSIGNED16 bReadOnly);
UNSIGNED32 ENTRYPOINT AdsStmtSetTableType  (ADSHANDLE hStmt,
                                             UNSIGNED16 usType);
UNSIGNED32 ENTRYPOINT AdsTestLogin         (UNSIGNED8* pucServer,
                                             UNSIGNED16 usServerType,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED8* pucPwd,
                                             UNSIGNED32 ulOptions);
UNSIGNED32 ENTRYPOINT AdsTestRecLocks      (ADSHANDLE hTable);
UNSIGNED32 ENTRYPOINT AdsWriteAllRecords   (ADSHANDLE hTable);

// Management API stubs — wire-protocol-level operations. OpenADS local
// mode returns AE_FUNCTION_NOT_AVAILABLE; the server build will route
// these to the corresponding TCP frames.
UNSIGNED32 ENTRYPOINT AdsMgConnect         (UNSIGNED8* pucServer,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED8* pucPwd,
                                             ADSHANDLE* phMg);
UNSIGNED32 ENTRYPOINT AdsMgDisconnect      (ADSHANDLE hMg);
UNSIGNED32 ENTRYPOINT AdsMgGetActivityInfo (ADSHANDLE hMg, void* pInfo,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetCommStats    (ADSHANDLE hMg, void* pInfo,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetConfigInfo   (ADSHANDLE hMg, void* pInfo,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetInstallInfo  (ADSHANDLE hMg, void* pInfo,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetLockOwner    (ADSHANDLE hMg, void* pInfo,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetLocks        (ADSHANDLE hMg, void* pInfo,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetOpenIndexes  (ADSHANDLE hMg, void* pInfo,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetOpenTables   (ADSHANDLE hMg, void* pInfo,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetOpenTables2  (ADSHANDLE hMg, void* pInfo,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetServerType   (ADSHANDLE hMg, UNSIGNED16* pusT);
UNSIGNED32 ENTRYPOINT AdsMgGetUserNames    (ADSHANDLE hMg, void* pInfo,
                                             UNSIGNED16* pusCount);
UNSIGNED32 ENTRYPOINT AdsMgGetWorkerThreadActivity(ADSHANDLE hMg, void* pInfo,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgKillUser        (ADSHANDLE hMg, UNSIGNED8* pucUser,
                                             UNSIGNED16 usOption);
UNSIGNED32 ENTRYPOINT AdsMgResetCommStats  (ADSHANDLE hMg);

// Data-dictionary helpers used by rddads.
UNSIGNED32 ENTRYPOINT AdsDDAddIndexFile    (ADSHANDLE hConnect,
                                             UNSIGNED8* pucTable,
                                             UNSIGNED8* pucIndex,
                                             UNSIGNED8* pucComment);
UNSIGNED32 ENTRYPOINT AdsDDAddUserToGroup  (ADSHANDLE hConnect,
                                             UNSIGNED8* pucGroup,
                                             UNSIGNED8* pucUser);
UNSIGNED32 ENTRYPOINT AdsDDCreateLink      (ADSHANDLE hConnect,
                                             UNSIGNED8* pucLink,
                                             UNSIGNED8* pucPath,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED8* pucPwd,
                                             UNSIGNED16 usOptions);
UNSIGNED32 ENTRYPOINT AdsDDCreateRefIntegrity(ADSHANDLE hConnect,
                                             UNSIGNED8* pucName,
                                             UNSIGNED8* pucFail,
                                             UNSIGNED8* pucParent,
                                             UNSIGNED8* pucChild,
                                             UNSIGNED8* pucTag,
                                             UNSIGNED16 usUpdate,
                                             UNSIGNED16 usDelete,
                                             UNSIGNED8* pucDesc,
                                             UNSIGNED16 usOptions);
UNSIGNED32 ENTRYPOINT AdsDDCreateUser      (ADSHANDLE hConnect,
                                             UNSIGNED8* pucGroup,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED8* pucPwd,
                                             UNSIGNED8* pucComment);
UNSIGNED32 ENTRYPOINT AdsDDDeleteUser      (ADSHANDLE hConnect,
                                             UNSIGNED8* pucUser);
UNSIGNED32 ENTRYPOINT AdsDDDropLink        (ADSHANDLE hConnect,
                                             UNSIGNED8* pucLink,
                                             UNSIGNED16 usOptions);
UNSIGNED32 ENTRYPOINT AdsDDGetDatabaseProperty(ADSHANDLE hConnect,
                                             UNSIGNED16 usProp,
                                             void* pvBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsDDGetUserProperty (ADSHANDLE hConnect,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED16 usProp,
                                             void* pvBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsDDModifyLink      (ADSHANDLE hConnect,
                                             UNSIGNED8* pucLink,
                                             UNSIGNED8* pucPath,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED8* pucPwd,
                                             UNSIGNED16 usOptions);
UNSIGNED32 ENTRYPOINT AdsDDRemoveIndexFile (ADSHANDLE hConnect,
                                             UNSIGNED8* pucTable,
                                             UNSIGNED8* pucIndex,
                                             UNSIGNED16 usOptions);
UNSIGNED32 ENTRYPOINT AdsDDRemoveRefIntegrity(ADSHANDLE hConnect,
                                             UNSIGNED8* pucRI);
UNSIGNED32 ENTRYPOINT AdsDDRemoveUserFromGroup(ADSHANDLE hConnect,
                                             UNSIGNED8* pucGroup,
                                             UNSIGNED8* pucUser);
UNSIGNED32 ENTRYPOINT AdsDDSetDatabaseProperty(ADSHANDLE hConnect,
                                             UNSIGNED16 usProp,
                                             void* pvBuf,
                                             UNSIGNED16 usLen);

#ifdef __cplusplus
}
#endif
