#pragma once

// OpenADS ACE-compatible C ABI — phase 1, milestone M1 subset.
// See openads/error.h for AE_* error codes.

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t  UNSIGNED8;
typedef uint16_t UNSIGNED16;
typedef uint32_t UNSIGNED32;
typedef int32_t  SIGNED32;
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
UNSIGNED32 AdsCreateIndex61 (ADSHANDLE  hTable, UNSIGNED8* pucFileName,
                              UNSIGNED8* pucIndexName, UNSIGNED8* pucExpr,
                              UNSIGNED8* pucCondition, UNSIGNED8* pucKeyFilter,
                              UNSIGNED32 ulOptions, UNSIGNED16 usPageSize,
                              ADSHANDLE* phIndex);
UNSIGNED32 AdsExtractKey    (ADSHANDLE hIndex, UNSIGNED8* pucBuf,
                              UNSIGNED16* pusLen);

UNSIGNED32 AdsCreateTable   (ADSHANDLE  hConnect, UNSIGNED8* pucName,
                              UNSIGNED8* pucAlias,
                              UNSIGNED16 usTableType, UNSIGNED16 usCharType,
                              UNSIGNED16 usLockType, UNSIGNED16 usCheckRights,
                              UNSIGNED16 usMemoBlockSize,
                              UNSIGNED8* pucFields,
                              ADSHANDLE* phTable);
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

UNSIGNED32 AdsAppendRecord  (ADSHANDLE hTable);
UNSIGNED32 AdsWriteRecord   (ADSHANDLE hTable);
UNSIGNED32 AdsDeleteRecord  (ADSHANDLE hTable);
UNSIGNED32 AdsRecallRecord  (ADSHANDLE hTable);
UNSIGNED32 AdsIsRecordDeleted(ADSHANDLE hTable, UNSIGNED16* pbDeleted);

UNSIGNED32 AdsSetString     (ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED8* pucValue, UNSIGNED32 ulLen);
UNSIGNED32 AdsSetLogical    (ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED16 bValue);
UNSIGNED32 AdsSetDouble     (ADSHANDLE hTable, UNSIGNED8* pucField,
                              double dValue);

UNSIGNED32 AdsLockRecord    (ADSHANDLE hTable, UNSIGNED32 ulRecord);
UNSIGNED32 AdsUnlockRecord  (ADSHANDLE hTable, UNSIGNED32 ulRecord);
UNSIGNED32 AdsLockTable     (ADSHANDLE hTable);
UNSIGNED32 AdsUnlockTable   (ADSHANDLE hTable);

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
// `adsOpen` switch routes each value to. The probe shows that whichever
// SAP/Sybase ACE.h Harbour was built against does not match the
// commonly-cited "ADS_STRING = 1, ADS_LOGICAL = 4" layout — the values
// below are what rddads.lib actually decodes.
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

#ifdef __cplusplus
}
#endif
