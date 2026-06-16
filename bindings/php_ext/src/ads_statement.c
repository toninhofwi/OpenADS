#include "php_ads.h"
#include "ads_arginfo.h"

zend_object_handlers ads_statement_handlers;

/* -----------------------------------------------------------------------
 * Object lifecycle
 * --------------------------------------------------------------------- */
static zend_object *ads_statement_create_object(zend_class_entry *ce)
{
    ads_statement_obj *obj = (ads_statement_obj *)
        zend_object_alloc(sizeof(ads_statement_obj), ce);
    obj->hStmt      = 0;
    obj->hCursor    = 0;
    obj->numFields  = 0;
    obj->executed   = 0;
    obj->eof        = 0;
    obj->fieldNames = NULL;
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    obj->std.handlers = &ads_statement_handlers;
    return &obj->std;
}

/* Populate stmt->fieldNames[] once from AdsGetFieldName (called on first use). */
static void ads_stmt_cache_field_names(ads_statement_obj *stmt)
{
    UNSIGNED16 i;
    if (stmt->fieldNames || stmt->numFields == 0) return;
    stmt->fieldNames = (char **)emalloc(sizeof(char *) * stmt->numFields);
    for (i = 0; i < stmt->numFields; i++) {
        char nameBuf[256];
        UNSIGNED16 nameLen = (UNSIGNED16)(sizeof(nameBuf) - 1);
        if (AdsGetFieldName(stmt->hCursor, (UNSIGNED16)(i + 1),
                            (UNSIGNED8 *)nameBuf, &nameLen) == AE_SUCCESS) {
            nameBuf[nameLen] = '\0';
            stmt->fieldNames[i] = estrdup(nameBuf);
        } else {
            stmt->fieldNames[i] = estrdup("");
        }
    }
}

static void ads_statement_free_object(zend_object *obj)
{
    ads_statement_obj *intern = ads_stmt_from_obj(obj);
    if (intern->fieldNames) {
        UNSIGNED16 i;
        for (i = 0; i < intern->numFields; i++) {
            if (intern->fieldNames[i]) efree(intern->fieldNames[i]);
        }
        efree(intern->fieldNames);
        intern->fieldNames = NULL;
    }
    if (intern->hCursor != 0) {
        AdsCloseTable(intern->hCursor);
        intern->hCursor = 0;
    }
    if (intern->hStmt != 0) {
        AdsCloseSQLStatement(intern->hStmt);
        intern->hStmt = 0;
    }
    zend_object_std_dtor(obj);
}

/* -----------------------------------------------------------------------
 * Internal: build an assoc array for the current cursor row
 * Returns 1 on success, 0 on error.
 * --------------------------------------------------------------------- */
static int ads_stmt_read_row_assoc(ads_statement_obj *stmt, zval *retval)
{
    UNSIGNED16 i;
    zval       fieldVal;

    /* Populate name cache on first call (eliminates AdsGetFieldName per row). */
    ads_stmt_cache_field_names(stmt);

    array_init(retval);

    for (i = 0; i < stmt->numFields; i++) {
        const char *name = stmt->fieldNames ? stmt->fieldNames[i] : "";
        if (!name || name[0] == '\0') continue;
        ads_get_field_zval(stmt->hCursor, name, &fieldVal);
        add_assoc_zval(retval, name, &fieldVal);
    }

    return 1;
}

/* -----------------------------------------------------------------------
 * Internal: build a numeric-keyed array for the current cursor row
 * --------------------------------------------------------------------- */
static int ads_stmt_read_row_numeric(ads_statement_obj *stmt, zval *retval)
{
    UNSIGNED16 i;
    zval       fieldVal;

    ads_stmt_cache_field_names(stmt);

    array_init(retval);

    for (i = 0; i < stmt->numFields; i++) {
        const char *name = stmt->fieldNames ? stmt->fieldNames[i] : "";
        if (!name || name[0] == '\0') continue;
        ads_get_field_zval(stmt->hCursor, name, &fieldVal);
        add_next_index_zval(retval, &fieldVal);
    }

    return 1;
}

/* -----------------------------------------------------------------------
 * AdsStatement::fetchAssoc() : array|false
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsStatement, fetchAssoc)
{
    ZEND_PARSE_PARAMETERS_NONE();

    ads_statement_obj *stmt = Z_ADS_STMT_P(ZEND_THIS);
    ADS_CHECK_STMT_CLOSED(stmt);

    if (stmt->eof) {
        RETURN_FALSE;
    }

    ads_stmt_read_row_assoc(stmt, return_value);

    /* Advance to next record */
    AdsSkip(stmt->hCursor, 1);

    UNSIGNED16 pbEOF = 0;
    AdsAtEOF(stmt->hCursor, &pbEOF);
    stmt->eof = (zend_bool)pbEOF;
}

/* -----------------------------------------------------------------------
 * AdsStatement::fetchRow() : array|false  (numeric keys)
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsStatement, fetchRow)
{
    ZEND_PARSE_PARAMETERS_NONE();

    ads_statement_obj *stmt = Z_ADS_STMT_P(ZEND_THIS);
    ADS_CHECK_STMT_CLOSED(stmt);

    if (stmt->eof) {
        RETURN_FALSE;
    }

    ads_stmt_read_row_numeric(stmt, return_value);

    AdsSkip(stmt->hCursor, 1);

    UNSIGNED16 pbEOF = 0;
    AdsAtEOF(stmt->hCursor, &pbEOF);
    stmt->eof = (zend_bool)pbEOF;
}

/* -----------------------------------------------------------------------
 * AdsStatement::fetchAll() : array
 * Returns all remaining rows as array of assoc arrays.
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsStatement, fetchAll)
{
    ZEND_PARSE_PARAMETERS_NONE();

    ads_statement_obj *stmt = Z_ADS_STMT_P(ZEND_THIS);
    ADS_CHECK_STMT_CLOSED(stmt);

    array_init(return_value);

    while (!stmt->eof) {
        zval row;
        ads_stmt_read_row_assoc(stmt, &row);
        add_next_index_zval(return_value, &row);

        AdsSkip(stmt->hCursor, 1);
        UNSIGNED16 pbEOF = 0;
        AdsAtEOF(stmt->hCursor, &pbEOF);
        stmt->eof = (zend_bool)pbEOF;
    }
}

/* -----------------------------------------------------------------------
 * AdsStatement::rowCount() : int
 * Returns the number of rows in the result set (filtered).
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsStatement, rowCount)
{
    ZEND_PARSE_PARAMETERS_NONE();

    ads_statement_obj *stmt = Z_ADS_STMT_P(ZEND_THIS);
    ADS_CHECK_STMT_CLOSED(stmt);

    if (stmt->hCursor == 0) {
        RETURN_LONG(0);
    }

    UNSIGNED32 ulCount = 0;
    AdsGetRecordCount(stmt->hCursor, ADS_IGNOREFILTERS, &ulCount);
    RETURN_LONG((zend_long)ulCount);
}

/* -----------------------------------------------------------------------
 * AdsStatement::columnCount() : int
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsStatement, columnCount)
{
    ZEND_PARSE_PARAMETERS_NONE();

    ads_statement_obj *stmt = Z_ADS_STMT_P(ZEND_THIS);
    ADS_CHECK_STMT_CLOSED(stmt);

    RETURN_LONG((zend_long)stmt->numFields);
}

/* -----------------------------------------------------------------------
 * AdsStatement::close() : void
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsStatement, close)
{
    ZEND_PARSE_PARAMETERS_NONE();

    ads_statement_obj *stmt = Z_ADS_STMT_P(ZEND_THIS);
    if (stmt->fieldNames) {
        UNSIGNED16 i;
        for (i = 0; i < stmt->numFields; i++) {
            if (stmt->fieldNames[i]) efree(stmt->fieldNames[i]);
        }
        efree(stmt->fieldNames);
        stmt->fieldNames = NULL;
    }
    if (stmt->hCursor != 0) {
        AdsCloseTable(stmt->hCursor);
        stmt->hCursor = 0;
    }
    if (stmt->hStmt != 0) {
        AdsCloseSQLStatement(stmt->hStmt);
        stmt->hStmt = 0;
    }
}

/* -----------------------------------------------------------------------
 * Method table
 * --------------------------------------------------------------------- */
static const zend_function_entry ads_statement_methods[] = {
    PHP_ME(AdsStatement, fetchAssoc,   arginfo_ads_statement_fetch_assoc,   ZEND_ACC_PUBLIC)
    PHP_ME(AdsStatement, fetchRow,     arginfo_ads_statement_fetch_row,     ZEND_ACC_PUBLIC)
    PHP_ME(AdsStatement, fetchAll,     arginfo_ads_statement_fetch_all,     ZEND_ACC_PUBLIC)
    PHP_ME(AdsStatement, rowCount,     arginfo_ads_statement_row_count,     ZEND_ACC_PUBLIC)
    PHP_ME(AdsStatement, columnCount,  arginfo_ads_statement_column_count,  ZEND_ACC_PUBLIC)
    PHP_ME(AdsStatement, close,        arginfo_ads_statement_close,         ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* -----------------------------------------------------------------------
 * Class registration
 * --------------------------------------------------------------------- */
void ads_statement_register_class(void)
{
    zend_class_entry ce;
    INIT_CLASS_ENTRY(ce, "AdsStatement", ads_statement_methods);
    ads_statement_ce = zend_register_internal_class(&ce);
    ads_statement_ce->create_object = ads_statement_create_object;

    memcpy(&ads_statement_handlers,
           zend_get_std_object_handlers(),
           sizeof(zend_object_handlers));
    ads_statement_handlers.offset    = XtOffsetOf(ads_statement_obj, std);
    ads_statement_handlers.free_obj  = ads_statement_free_object;
    ads_statement_handlers.clone_obj = NULL;
}
