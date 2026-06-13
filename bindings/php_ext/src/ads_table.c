#include "php_ads.h"
#include "ads_arginfo.h"

zend_object_handlers ads_table_handlers;

/* -----------------------------------------------------------------------
 * Object lifecycle
 * --------------------------------------------------------------------- */
static zend_object *ads_table_create_object(zend_class_entry *ce)
{
    ads_table_obj *obj = (ads_table_obj *)
        zend_object_alloc(sizeof(ads_table_obj), ce);
    obj->hTable = 0;
    obj->closed = 1;
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    obj->std.handlers = &ads_table_handlers;
    return &obj->std;
}

static void ads_table_free_object(zend_object *obj)
{
    ads_table_obj *intern = ads_table_from_obj(obj);
    if (!intern->closed && intern->hTable != 0) {
        AdsCloseTable(intern->hTable);
        intern->hTable = 0;
        intern->closed = 1;
    }
    zend_object_std_dtor(obj);
}

/* -----------------------------------------------------------------------
 * AdsTable::open(
 *   AdsConnection $conn,
 *   string        $tablePath,
 *   int           $tableType  = ADS_ADT,
 *   int           $lockType   = ADS_COMPATIBLE_LOCKING,
 *   int           $charType   = ADS_ANSI,
 *   int           $openMode   = ADS_SHARED
 * ) : static
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, open)
{
    zval   *zConn;
    char   *tablePath;
    size_t  tablePath_len;
    zend_long tableType = ADS_ADT;
    zend_long lockType  = ADS_COMPATIBLE_LOCKING;
    zend_long charType  = ADS_ANSI;
    zend_long openMode  = ADS_SHARED;

    ZEND_PARSE_PARAMETERS_START(2, 6)
        Z_PARAM_OBJECT_OF_CLASS(zConn, ads_connection_ce)
        Z_PARAM_STRING(tablePath, tablePath_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(tableType)
        Z_PARAM_LONG(lockType)
        Z_PARAM_LONG(charType)
        Z_PARAM_LONG(openMode)
    ZEND_PARSE_PARAMETERS_END();

    ads_connection_obj *conn = Z_ADS_CONN_P(zConn);
    ADS_CHECK_CONN_CLOSED(conn);

    ADSHANDLE hTable = 0;
    UNSIGNED32 ulRet = AdsOpenTable(
        conn->hConn,
        (UNSIGNED8 *)tablePath,
        NULL,                        /* alias — use default */
        (UNSIGNED16)tableType,
        (UNSIGNED16)charType,
        (UNSIGNED16)lockType,
        ADS_IGNORERIGHTS,            /* usCheckRights */
        (UNSIGNED32)openMode,        /* ulOptions */
        &hTable
    );

    if (ulRet != AE_SUCCESS) {
        ads_throw_ace_exception(ulRet, "AdsTable::open");
        RETURN_THROWS();
    }

    object_init_ex(return_value, ads_table_ce);
    ads_table_obj *tbl = Z_ADS_TABLE_P(return_value);
    tbl->hTable = hTable;
    tbl->closed = 0;
}

/* -----------------------------------------------------------------------
 * AdsTable::close() : void
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, close)
{
    ZEND_PARSE_PARAMETERS_NONE();

    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    if (!tbl->closed && tbl->hTable != 0) {
        AdsCloseTable(tbl->hTable);
        tbl->hTable = 0;
        tbl->closed = 1;
    }
}

/* -----------------------------------------------------------------------
 * AdsTable::gotoTop() : void
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, gotoTop)
{
    ZEND_PARSE_PARAMETERS_NONE();
    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);
    UNSIGNED32 ulRet = AdsGotoTop(tbl->hTable);
    ADS_CHECK_RC(ulRet, "AdsTable::gotoTop");
}

/* -----------------------------------------------------------------------
 * AdsTable::gotoBottom() : void
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, gotoBottom)
{
    ZEND_PARSE_PARAMETERS_NONE();
    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);
    UNSIGNED32 ulRet = AdsGotoBottom(tbl->hTable);
    ADS_CHECK_RC(ulRet, "AdsTable::gotoBottom");
}

/* -----------------------------------------------------------------------
 * AdsTable::skip(int $n = 1) : void
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, skip)
{
    zend_long n = 1;

    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(n)
    ZEND_PARSE_PARAMETERS_END();

    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);
    UNSIGNED32 ulRet = AdsSkip(tbl->hTable, (SIGNED32)n);
    ADS_CHECK_RC(ulRet, "AdsTable::skip");
}

/* -----------------------------------------------------------------------
 * AdsTable::gotoRecord(int $recNo) : void
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, gotoRecord)
{
    zend_long recNo;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(recNo)
    ZEND_PARSE_PARAMETERS_END();

    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);
    UNSIGNED32 ulRet = AdsGotoRecord(tbl->hTable, (UNSIGNED32)recNo);
    ADS_CHECK_RC(ulRet, "AdsTable::gotoRecord");
}

/* -----------------------------------------------------------------------
 * AdsTable::atEOF() : bool
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, atEOF)
{
    ZEND_PARSE_PARAMETERS_NONE();
    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);
    UNSIGNED16 pbEOF = 0;
    AdsAtEOF(tbl->hTable, &pbEOF);
    RETURN_BOOL(pbEOF);
}

/* -----------------------------------------------------------------------
 * AdsTable::atBOF() : bool
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, atBOF)
{
    ZEND_PARSE_PARAMETERS_NONE();
    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);
    UNSIGNED16 pbBOF = 0;
    AdsAtBOF(tbl->hTable, &pbBOF);
    RETURN_BOOL(pbBOF);
}

/* -----------------------------------------------------------------------
 * AdsTable::getRecord() : array
 * Returns all fields as an assoc array.
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, getRecord)
{
    ZEND_PARSE_PARAMETERS_NONE();

    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);

    UNSIGNED16 numFields = 0;
    AdsGetNumFields(tbl->hTable, &numFields);

    array_init(return_value);

    UNSIGNED16 i;
    for (i = 1; i <= numFields; i++) {
        char       nameBuf[256];
        UNSIGNED16 nameLen = (UNSIGNED16)(sizeof(nameBuf) - 1);
        if (AdsGetFieldName(tbl->hTable, i, (UNSIGNED8 *)nameBuf, &nameLen)
                != AE_SUCCESS) {
            continue;
        }
        nameBuf[nameLen] = '\0';

        zval fieldVal;
        ads_get_field_zval(tbl->hTable, nameBuf, &fieldVal);
        add_assoc_zval(return_value, nameBuf, &fieldVal);
    }
}

/* -----------------------------------------------------------------------
 * AdsTable::getString(string $field) : string
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, getString)
{
    char   *field;
    size_t  field_len;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(field, field_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);

    UNSIGNED32 ulFieldLen = 0;
    AdsGetFieldLength(tbl->hTable, (UNSIGNED8 *)field, &ulFieldLen);
    UNSIGNED32 ulBufSize = (ulFieldLen > 1) ? ulFieldLen + 4 : 65536;

    char *buf = (char *)emalloc(ulBufSize);
    UNSIGNED32 ulActualLen = ulBufSize;
    UNSIGNED32 ulRet = AdsGetString(tbl->hTable, (UNSIGNED8 *)field,
                                     (UNSIGNED8 *)buf, &ulActualLen, ADS_TRIM);
    if (ulRet != AE_SUCCESS) {
        efree(buf);
        ads_throw_ace_exception(ulRet, "AdsTable::getString");
        RETURN_THROWS();
    }

    RETVAL_STRINGL(buf, (size_t)ulActualLen);
    efree(buf);
}

/* -----------------------------------------------------------------------
 * AdsTable::getDouble(string $field) : float
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, getDouble)
{
    char   *field;
    size_t  field_len;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(field, field_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);

    DOUBLE dVal = 0.0;
    UNSIGNED32 ulRet = AdsGetDouble(tbl->hTable, (UNSIGNED8 *)field, &dVal);
    ADS_CHECK_RC(ulRet, "AdsTable::getDouble");
    RETURN_DOUBLE(dVal);
}

/* -----------------------------------------------------------------------
 * AdsTable::getLong(string $field) : int
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, getLong)
{
    char   *field;
    size_t  field_len;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(field, field_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);

    SIGNED32 lVal = 0;
    UNSIGNED32 ulRet = AdsGetLong(tbl->hTable, (UNSIGNED8 *)field, &lVal);
    ADS_CHECK_RC(ulRet, "AdsTable::getLong");
    RETURN_LONG((zend_long)lVal);
}

/* -----------------------------------------------------------------------
 * AdsTable::getLogical(string $field) : bool
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, getLogical)
{
    char   *field;
    size_t  field_len;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(field, field_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);

    UNSIGNED16 bVal = 0;
    UNSIGNED32 ulRet = AdsGetLogical(tbl->hTable, (UNSIGNED8 *)field, &bVal);
    ADS_CHECK_RC(ulRet, "AdsTable::getLogical");
    RETURN_BOOL(bVal);
}

/* -----------------------------------------------------------------------
 * AdsTable::recordCount() : int
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, recordCount)
{
    ZEND_PARSE_PARAMETERS_NONE();
    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);

    UNSIGNED32 ulCount = 0;
    AdsGetRecordCount(tbl->hTable, ADS_IGNOREFILTERS, &ulCount);
    RETURN_LONG((zend_long)ulCount);
}

/* -----------------------------------------------------------------------
 * AdsTable::recordNum() : int
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, recordNum)
{
    ZEND_PARSE_PARAMETERS_NONE();
    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);

    UNSIGNED32 ulRec = 0;
    AdsGetRecordNum(tbl->hTable, ADS_IGNOREFILTERS, &ulRec);
    RETURN_LONG((zend_long)ulRec);
}

/* -----------------------------------------------------------------------
 * AdsTable::setString(string $field, string $value) : void
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, setString)
{
    char   *field, *value;
    size_t  field_len, value_len;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(field, field_len)
        Z_PARAM_STRING(value, value_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);
    UNSIGNED32 ulRet = AdsSetString(tbl->hTable, (UNSIGNED8 *)field,
                                     (UNSIGNED8 *)value, (UNSIGNED32)value_len);
    ADS_CHECK_RC(ulRet, "AdsTable::setString");
}

/* -----------------------------------------------------------------------
 * AdsTable::setLong(string $field, int $value) : void
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, setLong)
{
    char      *field;
    size_t     field_len;
    zend_long  value;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(field, field_len)
        Z_PARAM_LONG(value)
    ZEND_PARSE_PARAMETERS_END();

    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);
    UNSIGNED32 ulRet = AdsSetLongLong(tbl->hTable, (UNSIGNED8 *)field, (SIGNED64)value);
    ADS_CHECK_RC(ulRet, "AdsTable::setLong");
}

/* -----------------------------------------------------------------------
 * AdsTable::setDouble(string $field, float $value) : void
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, setDouble)
{
    char   *field;
    size_t  field_len;
    double  value;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(field, field_len)
        Z_PARAM_DOUBLE(value)
    ZEND_PARSE_PARAMETERS_END();

    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);
    UNSIGNED32 ulRet = AdsSetDouble(tbl->hTable, (UNSIGNED8 *)field, (DOUBLE)value);
    ADS_CHECK_RC(ulRet, "AdsTable::setDouble");
}

/* -----------------------------------------------------------------------
 * AdsTable::setLogical(string $field, bool $value) : void
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, setLogical)
{
    char *field;
    size_t field_len;
    zend_bool value;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(field, field_len)
        Z_PARAM_BOOL(value)
    ZEND_PARSE_PARAMETERS_END();

    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);
    UNSIGNED32 ulRet = AdsSetLogical(tbl->hTable, (UNSIGNED8 *)field, (UNSIGNED16)value);
    ADS_CHECK_RC(ulRet, "AdsTable::setLogical");
}

/* -----------------------------------------------------------------------
 * AdsTable::appendRecord() : void
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, appendRecord)
{
    ZEND_PARSE_PARAMETERS_NONE();
    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);
    UNSIGNED32 ulRet = AdsAppendRecord(tbl->hTable);
    ADS_CHECK_RC(ulRet, "AdsTable::appendRecord");
}

/* -----------------------------------------------------------------------
 * AdsTable::deleteRecord() : void
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, deleteRecord)
{
    ZEND_PARSE_PARAMETERS_NONE();
    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);
    UNSIGNED32 ulRet = AdsDeleteRecord(tbl->hTable);
    ADS_CHECK_RC(ulRet, "AdsTable::deleteRecord");
}

/* -----------------------------------------------------------------------
 * AdsTable::writeRecord() : void
 * Commits pending changes on the current record.
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, writeRecord)
{
    ZEND_PARSE_PARAMETERS_NONE();
    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);
    UNSIGNED32 ulRet = AdsWriteRecord(tbl->hTable);
    ADS_CHECK_RC(ulRet, "AdsTable::writeRecord");
}

/* -----------------------------------------------------------------------
 * AdsTable::cancelUpdate() : void
 * Discards pending changes on the current record.
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, cancelUpdate)
{
    ZEND_PARSE_PARAMETERS_NONE();
    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);
    AdsCancelUpdate(tbl->hTable);
}

/* -----------------------------------------------------------------------
 * AdsTable::getIndexTags() : array
 * Returns [{tag, expression, descending}, ...] for every open index tag.
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsTable, getIndexTags)
{
    ZEND_PARSE_PARAMETERS_NONE();
    ads_table_obj *tbl = Z_ADS_TABLE_P(ZEND_THIS);
    ADS_CHECK_TABLE_CLOSED(tbl);

    UNSIGNED16 count = 0;
    array_init(return_value);
    if (AdsGetNumIndexes(tbl->hTable, &count) != AE_SUCCESS || count == 0)
        return;

    UNSIGNED8  nameBuf[256];
    UNSIGNED8  exprBuf[1024];
    UNSIGNED16 nameLen, exprLen, desc, uniq;
    ADSHANDLE  hIndex;
    UNSIGNED16 i;

    for (i = 1; i <= count; i++) {
        hIndex = 0;
        if (AdsGetIndexHandleByOrder(tbl->hTable, i, &hIndex) != AE_SUCCESS)
            continue;

        nameBuf[0] = '\0'; nameLen = (UNSIGNED16)sizeof(nameBuf);
        exprBuf[0] = '\0'; exprLen = (UNSIGNED16)sizeof(exprBuf);
        desc = 0; uniq = 0;

        AdsGetIndexName(hIndex, nameBuf, &nameLen);
        AdsGetIndexExpr(hIndex, exprBuf, &exprLen);
        AdsIsIndexDescending(hIndex, &desc);
        AdsIsIndexUnique(hIndex, &uniq);

        zval entry;
        array_init(&entry);
        add_assoc_string(&entry, "tag",        (char *)nameBuf);
        add_assoc_string(&entry, "expression", (char *)exprBuf);
        add_assoc_bool(  &entry, "descending", desc != 0);
        add_assoc_bool(  &entry, "unique",     uniq != 0);
        add_next_index_zval(return_value, &entry);
    }
}

/* -----------------------------------------------------------------------
 * Method table
 * --------------------------------------------------------------------- */
static const zend_function_entry ads_table_methods[] = {
    PHP_ME(AdsTable, open,         arginfo_ads_table_open,          ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(AdsTable, close,        arginfo_ads_table_close,         ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, gotoTop,      arginfo_ads_table_goto_top,      ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, gotoBottom,   arginfo_ads_table_goto_bottom,   ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, skip,         arginfo_ads_table_skip,          ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, gotoRecord,   arginfo_ads_table_goto_record,   ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, atEOF,        arginfo_ads_table_at_eof,        ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, atBOF,        arginfo_ads_table_at_bof,        ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, getRecord,    arginfo_ads_table_get_record,    ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, getString,    arginfo_ads_table_get_string,    ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, getDouble,    arginfo_ads_table_get_double,    ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, getLong,      arginfo_ads_table_get_long,      ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, getLogical,   arginfo_ads_table_get_logical,   ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, recordCount,  arginfo_ads_table_record_count,  ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, recordNum,    arginfo_ads_table_record_num,    ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, setString,    arginfo_ads_table_set_string,    ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, setLong,      arginfo_ads_table_set_long,      ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, setDouble,    arginfo_ads_table_set_double,    ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, setLogical,   arginfo_ads_table_set_logical,   ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, appendRecord, arginfo_ads_table_append_record, ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, deleteRecord, arginfo_ads_table_delete_record, ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, writeRecord,  arginfo_ads_table_write_record,  ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, cancelUpdate,  arginfo_ads_table_cancel_update,   ZEND_ACC_PUBLIC)
    PHP_ME(AdsTable, getIndexTags, arginfo_ads_table_get_index_tags,  ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* -----------------------------------------------------------------------
 * Class registration
 * --------------------------------------------------------------------- */
void ads_table_register_class(void)
{
    zend_class_entry ce;
    INIT_CLASS_ENTRY(ce, "AdsTable", ads_table_methods);
    ads_table_ce = zend_register_internal_class(&ce);
    ads_table_ce->create_object = ads_table_create_object;

    memcpy(&ads_table_handlers,
           zend_get_std_object_handlers(),
           sizeof(zend_object_handlers));
    ads_table_handlers.offset    = XtOffsetOf(ads_table_obj, std);
    ads_table_handlers.free_obj  = ads_table_free_object;
    ads_table_handlers.clone_obj = NULL;
}
