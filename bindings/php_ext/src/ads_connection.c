#include "php_ads.h"
#include "ads_arginfo.h"

#include <ctype.h>

zend_object_handlers ads_connection_handlers;

static int ads_str_eq_ci(const char *s, size_t len, const char *lit)
{
    size_t i = 0;
    for (; i < len && lit[i] != '\0'; ++i) {
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)lit[i])) {
            return 0;
        }
    }
    return i == len && lit[i] == '\0';
}

/* -----------------------------------------------------------------------
 * Object lifecycle
 * --------------------------------------------------------------------- */
static zend_object *ads_connection_create_object(zend_class_entry *ce)
{
    ads_connection_obj *obj = (ads_connection_obj *)
        zend_object_alloc(sizeof(ads_connection_obj), ce);
    obj->hConn  = 0;
    obj->closed = 1;
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    obj->std.handlers = &ads_connection_handlers;
    return &obj->std;
}

static void ads_connection_free_object(zend_object *obj)
{
    ads_connection_obj *intern = ads_conn_from_obj(obj);
    if (!intern->closed && intern->hConn != 0) {
        AdsDisconnect(intern->hConn);
        intern->hConn  = 0;
        intern->closed = 1;
    }
    zend_object_std_dtor(obj);
}

/* -----------------------------------------------------------------------
 * AdsConnection::connect(array $options) : static
 *
 * Options:
 *   "path"       => server path / dictionary path (required)
 *   "user"       => username          (optional)
 *   "password"   => password          (optional)
 *   "serverType" => int bitmask       (default: ADS_LOCAL_SERVER|ADS_REMOTE_SERVER)
 *   "server_type" / "connType" => "local" or "remote" alias
 *   "options"    => int ulOptions     (default: 0)
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsConnection, connect)
{
    zval  *zOptions;
    zval  *zv;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ARRAY(zOptions)
    ZEND_PARSE_PARAMETERS_END();

    HashTable *ht = Z_ARRVAL_P(zOptions);

    /* Required: path */
    zv = zend_hash_str_find(ht, "path", sizeof("path") - 1);
    if (!zv || Z_TYPE_P(zv) != IS_STRING) {
        zend_throw_exception(ads_exception_ce,
            "AdsConnection::connect() requires 'path' option", 0);
        RETURN_THROWS();
    }
    const char *path = Z_STRVAL_P(zv);

    /* Optional: user */
    const char *user = NULL;
    zv = zend_hash_str_find(ht, "user", sizeof("user") - 1);
    if (zv && Z_TYPE_P(zv) == IS_STRING) {
        user = Z_STRVAL_P(zv);
    }

    /* Optional: password */
    const char *pass = NULL;
    zv = zend_hash_str_find(ht, "password", sizeof("password") - 1);
    if (zv && Z_TYPE_P(zv) == IS_STRING) {
        pass = Z_STRVAL_P(zv);
    }

    /* Optional: serverType  (default: local + remote) */
    UNSIGNED16 usServerType = ADS_LOCAL_SERVER | ADS_REMOTE_SERVER;
    zv = zend_hash_str_find(ht, "serverType", sizeof("serverType") - 1);
    if (zv) {
        usServerType = (UNSIGNED16)zval_get_long(zv);
    } else {
        zv = zend_hash_str_find(ht, "server_type", sizeof("server_type") - 1);
        if (!zv) {
            zv = zend_hash_str_find(ht, "connType", sizeof("connType") - 1);
        }
        if (zv && Z_TYPE_P(zv) == IS_STRING) {
            const char *st = Z_STRVAL_P(zv);
            size_t st_len = Z_STRLEN_P(zv);
            if (ads_str_eq_ci(st, st_len, "remote")) {
                usServerType = ADS_REMOTE_SERVER;
            } else if (ads_str_eq_ci(st, st_len, "local")) {
                usServerType = ADS_LOCAL_SERVER;
            }
        }
    }

    /* Optional: ulOptions */
    UNSIGNED32 ulOptions = 0;
    zv = zend_hash_str_find(ht, "options", sizeof("options") - 1);
    if (zv) {
        ulOptions = (UNSIGNED32)zval_get_long(zv);
    }

    ADSHANDLE hConn = 0;
    UNSIGNED32 ulRet = AdsConnect60(
        (UNSIGNED8 *)path,
        usServerType,
        (UNSIGNED8 *)(user ? user : ""),
        (UNSIGNED8 *)(pass ? pass : ""),
        ulOptions,
        &hConn
    );

    if (ulRet != AE_SUCCESS) {
        if (hConn) AdsDisconnect(hConn);
        ads_throw_ace_exception(ulRet, "AdsConnection::connect");
        RETURN_THROWS();
    }

    /* Create and return the AdsConnection object */
    object_init_ex(return_value, ads_connection_ce);
    ads_connection_obj *obj = Z_ADS_CONN_P(return_value);
    obj->hConn  = hConn;
    obj->closed = 0;
}

/* -----------------------------------------------------------------------
 * AdsConnection::close() : void
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsConnection, close)
{
    ZEND_PARSE_PARAMETERS_NONE();

    ads_connection_obj *obj = Z_ADS_CONN_P(ZEND_THIS);
    if (!obj->closed && obj->hConn != 0) {
        AdsDisconnect(obj->hConn);
        obj->hConn  = 0;
        obj->closed = 1;
    }
}

/* -----------------------------------------------------------------------
 * AdsConnection::query(string $sql) : AdsStatement
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsConnection, query)
{
    char   *sql;
    size_t  sql_len;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(sql, sql_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_connection_obj *conn = Z_ADS_CONN_P(ZEND_THIS);
    ADS_CHECK_CONN_CLOSED(conn);

    ADSHANDLE hStmt   = 0;
    ADSHANDLE hCursor = 0;

    UNSIGNED32 ulRet = AdsCreateSQLStatement(conn->hConn, &hStmt);
    if (ulRet != AE_SUCCESS) {
        ads_throw_ace_exception(ulRet, "AdsConnection::query (create statement)");
        RETURN_THROWS();
    }

    ulRet = AdsExecuteSQLDirect(hStmt, (UNSIGNED8 *)sql, &hCursor);
    if (ulRet != AE_SUCCESS) {
        /* Read error text BEFORE AdsCloseSQLStatement calls ok() and clears it */
        ads_throw_ace_exception(ulRet, "AdsConnection::query (execute)");
        AdsCloseSQLStatement(hStmt);
        RETURN_THROWS();
    }

    UNSIGNED16 numFields = 0;
    if (hCursor != 0) {
        /* OpenADS positions the SELECT cursor at BOF; SAP starts at record 1.
         * GotoTop normalises both to the first real record before any fetch. */
        AdsGotoTop(hCursor);
        AdsGetNumFields(hCursor, &numFields);
    }

    object_init_ex(return_value, ads_statement_ce);
    ads_statement_obj *stmt = Z_ADS_STMT_P(return_value);
    stmt->hStmt     = hStmt;
    stmt->hCursor   = hCursor;
    stmt->numFields = numFields;
    stmt->executed  = 1;
    stmt->eof       = 0;

    /* Check whether we are already at EOF (empty result set) */
    if (hCursor != 0) {
        UNSIGNED16 pbEOF = 0;
        AdsAtEOF(hCursor, &pbEOF);
        stmt->eof = (zend_bool)pbEOF;
    } else {
        stmt->eof = 1;
    }
}

/* -----------------------------------------------------------------------
 * AdsConnection::execute(string $sql) : bool
 * For DDL / DML that does not return a cursor.
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsConnection, execute)
{
    char   *sql;
    size_t  sql_len;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(sql, sql_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_connection_obj *conn = Z_ADS_CONN_P(ZEND_THIS);
    ADS_CHECK_CONN_CLOSED(conn);

    ADSHANDLE  hStmt   = 0;
    ADSHANDLE  hCursor = 0;

    UNSIGNED32 ulRet = AdsCreateSQLStatement(conn->hConn, &hStmt);
    if (ulRet != AE_SUCCESS) {
        ads_throw_ace_exception(ulRet, "AdsConnection::execute (create statement)");
        RETURN_THROWS();
    }

    ulRet = AdsExecuteSQLDirect(hStmt, (UNSIGNED8 *)sql, &hCursor);
    if (hCursor != 0) {
        AdsCloseTable(hCursor);
    }
    AdsCloseSQLStatement(hStmt);

    if (ulRet != AE_SUCCESS) {
        ads_throw_ace_exception(ulRet, "AdsConnection::execute");
        RETURN_THROWS();
    }

    RETURN_TRUE;
}

/* -----------------------------------------------------------------------
 * AdsConnection::beginTransaction() : AdsTransaction
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsConnection, beginTransaction)
{
    ZEND_PARSE_PARAMETERS_NONE();

    ads_connection_obj *conn = Z_ADS_CONN_P(ZEND_THIS);
    ADS_CHECK_CONN_CLOSED(conn);

    UNSIGNED32 ulRet = AdsBeginTransaction(conn->hConn);
    if (ulRet != AE_SUCCESS) {
        ads_throw_ace_exception(ulRet, "AdsConnection::beginTransaction");
        RETURN_THROWS();
    }

    object_init_ex(return_value, ads_transaction_ce);
    ads_transaction_obj *trans = Z_ADS_TRANS_P(return_value);
    trans->hConn  = conn->hConn;
    trans->active = 1;
}

/* -----------------------------------------------------------------------
 * AdsConnection::prepare(string $sql) : AdsPreparedStatement
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsConnection, prepare)
{
    char   *sql;
    size_t  sql_len;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(sql, sql_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_connection_obj *conn = Z_ADS_CONN_P(ZEND_THIS);
    ADS_CHECK_CONN_CLOSED(conn);

    ADSHANDLE  hStmt = 0;
    UNSIGNED32 ulRet = AdsCreateSQLStatement(conn->hConn, &hStmt);
    if (ulRet != AE_SUCCESS) {
        ads_throw_ace_exception(ulRet, "AdsConnection::prepare (create statement)");
        RETURN_THROWS();
    }

    ulRet = AdsPrepareSQL(hStmt, (UNSIGNED8 *)sql);
    if (ulRet != AE_SUCCESS) {
        AdsCloseSQLStatement(hStmt);
        ads_throw_ace_exception(ulRet, "AdsConnection::prepare (prepare SQL)");
        RETURN_THROWS();
    }

    object_init_ex(return_value, ads_prepared_ce);
    ads_prepared_obj *prep = Z_ADS_PREP_P(return_value);
    prep->hStmt  = hStmt;
    prep->closed = 0;
}

/* -----------------------------------------------------------------------
 * AdsConnection::isAlive() : bool
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsConnection, isAlive)
{
    ZEND_PARSE_PARAMETERS_NONE();

    ads_connection_obj *conn = Z_ADS_CONN_P(ZEND_THIS);
    if (conn->closed || conn->hConn == 0) {
        RETURN_FALSE;
    }

    UNSIGNED16 bAlive = 0;
    AdsIsConnectionAlive(conn->hConn, &bAlive);
    RETURN_BOOL(bAlive);
}

/* -----------------------------------------------------------------------
 * Method table
 * --------------------------------------------------------------------- */
static const zend_function_entry ads_connection_methods[] = {
    PHP_ME(AdsConnection, connect,          arginfo_ads_connection_connect,           ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(AdsConnection, close,            arginfo_ads_connection_close,             ZEND_ACC_PUBLIC)
    PHP_ME(AdsConnection, query,            arginfo_ads_connection_query,             ZEND_ACC_PUBLIC)
    PHP_ME(AdsConnection, execute,          arginfo_ads_connection_execute,           ZEND_ACC_PUBLIC)
    PHP_ME(AdsConnection, prepare,          arginfo_ads_connection_prepare,           ZEND_ACC_PUBLIC)
    PHP_ME(AdsConnection, beginTransaction, arginfo_ads_connection_begin_transaction, ZEND_ACC_PUBLIC)
    PHP_ME(AdsConnection, isAlive,          arginfo_ads_connection_is_alive,          ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* -----------------------------------------------------------------------
 * Class registration (called from MINIT)
 * --------------------------------------------------------------------- */
void ads_connection_register_class(void)
{
    zend_class_entry ce;
    INIT_CLASS_ENTRY(ce, "AdsConnection", ads_connection_methods);
    ads_connection_ce = zend_register_internal_class(&ce);
    ads_connection_ce->create_object = ads_connection_create_object;

    memcpy(&ads_connection_handlers,
           zend_get_std_object_handlers(),
           sizeof(zend_object_handlers));
    ads_connection_handlers.offset    = XtOffsetOf(ads_connection_obj, std);
    ads_connection_handlers.free_obj  = ads_connection_free_object;
    ads_connection_handlers.clone_obj = NULL;
}
