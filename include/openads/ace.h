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
                              ADSHANDLE* phIndex);
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
                              UNSIGNED16 usOption, UNSIGNED16* pbFound);
UNSIGNED32 AdsSeekLast      (ADSHANDLE hIndex, UNSIGNED8* pucKey,
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
#define ADS_MEMO_TEXT      1
#define ADS_MEMO_PICTURE   0

#define ADS_FIELD_TYPE_CHAR       1
#define ADS_FIELD_TYPE_NUMERIC    2
#define ADS_FIELD_TYPE_LOGICAL    3
#define ADS_FIELD_TYPE_DATE       4
#define ADS_FIELD_TYPE_DATETIME   5
#define ADS_FIELD_TYPE_MEMO       6
#define ADS_FIELD_TYPE_INTEGER    7
#define ADS_FIELD_TYPE_DOUBLE     8
#define ADS_FIELD_TYPE_CURRENCY   9
#define ADS_FIELD_TYPE_UNKNOWN    99

#ifdef __cplusplus
}
#endif
