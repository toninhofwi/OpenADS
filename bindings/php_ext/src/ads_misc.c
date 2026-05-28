#include "php_ads.h"
#include "ads_arginfo.h"

/* =====================================================================
 * AdsTransaction
 * ===================================================================== */

zend_object_handlers ads_transaction_handlers;

static zend_object *ads_transaction_create_object(zend_class_entry *ce)
{
    ads_transaction_obj *obj = (ads_transaction_obj *)
        zend_object_alloc(sizeof(ads_transaction_obj), ce);
    obj->hConn  = 0;
    obj->active = 0;
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    obj->std.handlers = &ads_transaction_handlers;
    return &obj->std;
}

static void ads_transaction_free_object(zend_object *obj)
{
    ads_transaction_obj *intern = ads_trans_from_obj(obj);
    if (intern->active && intern->hConn != 0) {
        AdsRollbackTransaction(intern->hConn);
        intern->active = 0;
    }
    zend_object_std_dtor(obj);
}

PHP_METHOD(AdsTransaction, commit)
{
    ZEND_PARSE_PARAMETERS_NONE();
    ads_transaction_obj *trans = Z_ADS_TRANS_P(ZEND_THIS);
    if (!trans->active) {
        zend_throw_exception(ads_exception_ce, "AdsTransaction: no active transaction", 0);
        RETURN_THROWS();
    }
    UNSIGNED32 ulRet = AdsCommitTransaction(trans->hConn);
    trans->active = 0;
    ADS_CHECK_RC(ulRet, "AdsTransaction::commit");
}

PHP_METHOD(AdsTransaction, rollback)
{
    ZEND_PARSE_PARAMETERS_NONE();
    ads_transaction_obj *trans = Z_ADS_TRANS_P(ZEND_THIS);
    if (!trans->active) {
        zend_throw_exception(ads_exception_ce, "AdsTransaction: no active transaction", 0);
        RETURN_THROWS();
    }
    UNSIGNED32 ulRet = AdsRollbackTransaction(trans->hConn);
    trans->active = 0;
    ADS_CHECK_RC(ulRet, "AdsTransaction::rollback");
}

PHP_METHOD(AdsTransaction, isActive)
{
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_BOOL(Z_ADS_TRANS_P(ZEND_THIS)->active);
}

static const zend_function_entry ads_transaction_methods[] = {
    PHP_ME(AdsTransaction, commit,   arginfo_ads_transaction_commit,    ZEND_ACC_PUBLIC)
    PHP_ME(AdsTransaction, rollback, arginfo_ads_transaction_rollback,  ZEND_ACC_PUBLIC)
    PHP_ME(AdsTransaction, isActive, arginfo_ads_transaction_is_active, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* =====================================================================
 * AdsDictionary
 *
 * In ACE the connection handle obtained by connecting to a .ADD file IS
 * the dictionary handle.  AdsDictionary wraps it and exposes all DD ops.
 * ===================================================================== */

zend_object_handlers ads_dictionary_handlers;

static zend_object *ads_dictionary_create_object(zend_class_entry *ce)
{
    ads_dictionary_obj *obj = (ads_dictionary_obj *)
        zend_object_alloc(sizeof(ads_dictionary_obj), ce);
    obj->hDict  = 0;
    obj->closed = 1;
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    obj->std.handlers = &ads_dictionary_handlers;
    return &obj->std;
}

static void ads_dictionary_free_object(zend_object *obj)
{
    ads_dictionary_obj *intern = ads_dict_from_obj(obj);
    if (!intern->closed && intern->hDict != 0) {
        AdsDisconnect(intern->hDict);
        intern->hDict  = 0;
        intern->closed = 1;
    }
    zend_object_std_dtor(obj);
}

/* -----------------------------------------------------------------------
 * AdsDictionary::open(string $dictPath, string $user="", string $pass="")
 *   : static
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsDictionary, open)
{
    char   *dictPath;  size_t dictPath_len;
    char   *user = ""; size_t user_len = 0;
    char   *pass = ""; size_t pass_len = 0;

    ZEND_PARSE_PARAMETERS_START(1, 3)
        Z_PARAM_STRING(dictPath, dictPath_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(user, user_len)
        Z_PARAM_STRING(pass, pass_len)
    ZEND_PARSE_PARAMETERS_END();

    ADSHANDLE  hConn = 0;
    UNSIGNED32 ulRet = AdsConnect60(
        (UNSIGNED8 *)dictPath,
        ADS_LOCAL_SERVER | ADS_REMOTE_SERVER,
        (UNSIGNED8 *)user,
        (UNSIGNED8 *)pass,
        0,
        &hConn
    );
    if (ulRet != AE_SUCCESS) {
        ads_throw_ace_exception(ulRet, "AdsDictionary::open");
        RETURN_THROWS();
    }
    object_init_ex(return_value, ads_dictionary_ce);
    ads_dictionary_obj *dict = Z_ADS_DICT_P(return_value);
    dict->hDict  = hConn;
    dict->closed = 0;
}

/* -----------------------------------------------------------------------
 * AdsDictionary::fromConnection(AdsConnection $conn) : static
 *
 * Borrowed handle — close() is a no-op, GC won't disconnect.
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsDictionary, fromConnection)
{
    zval *zConn;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT_OF_CLASS(zConn, ads_connection_ce)
    ZEND_PARSE_PARAMETERS_END();

    ads_connection_obj *conn = Z_ADS_CONN_P(zConn);
    ADS_CHECK_CONN_CLOSED(conn);

    object_init_ex(return_value, ads_dictionary_ce);
    ads_dictionary_obj *dict = Z_ADS_DICT_P(return_value);
    dict->hDict  = conn->hConn;
    dict->closed = 1;  /* borrowed */
}

/* -----------------------------------------------------------------------
 * AdsDictionary::close() : void
 * --------------------------------------------------------------------- */
PHP_METHOD(AdsDictionary, close)
{
    ZEND_PARSE_PARAMETERS_NONE();
    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    if (!dict->closed && dict->hDict != 0) {
        AdsDisconnect(dict->hDict);
        dict->hDict  = 0;
        dict->closed = 1;
    }
}

/* =====================================================================
 * Database-level properties
 * ===================================================================== */

PHP_METHOD(AdsDictionary, getDatabaseProperty)
{
    zend_long property;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(property)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    char       buf[1024];
    UNSIGNED16 usLen = (UNSIGNED16)(sizeof(buf) - 1);
    UNSIGNED32 ulRet = AdsDDGetDatabaseProperty(
        dict->hDict, (UNSIGNED16)property, buf, &usLen);
    ADS_CHECK_RC(ulRet, "AdsDictionary::getDatabaseProperty");
    buf[usLen] = '\0';
    RETURN_STRINGL(buf, (size_t)usLen);
}

PHP_METHOD(AdsDictionary, setDatabaseProperty)
{
    zend_long  property;
    char      *value;  size_t value_len;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_LONG(property)
        Z_PARAM_STRING(value, value_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDSetDatabaseProperty(
        dict->hDict, (UNSIGNED16)property,
        (void *)value, (UNSIGNED16)value_len);
    ADS_CHECK_RC(ulRet, "AdsDictionary::setDatabaseProperty");
}

/* =====================================================================
 * Table management
 * ===================================================================== */

PHP_METHOD(AdsDictionary, addTable)
{
    char      *alias;     size_t alias_len;
    char      *path;      size_t path_len;
    zend_long  tableType  = 3;   /* ADS_ADT */
    zend_long  charType   = 1;   /* ADS_ANSI */
    char      *indexPath  = ""; size_t indexPath_len = 0;
    char      *comment    = ""; size_t comment_len   = 0;

    ZEND_PARSE_PARAMETERS_START(2, 6)
        Z_PARAM_STRING(alias, alias_len)
        Z_PARAM_STRING(path, path_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(tableType)
        Z_PARAM_LONG(charType)
        Z_PARAM_STRING(indexPath, indexPath_len)
        Z_PARAM_STRING(comment, comment_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDAddTable(
        dict->hDict,
        (UNSIGNED8 *)alias,
        (UNSIGNED8 *)path,
        (UNSIGNED16)tableType,
        (UNSIGNED16)charType,
        (UNSIGNED8 *)indexPath,
        (UNSIGNED8 *)comment);
    ADS_CHECK_RC(ulRet, "AdsDictionary::addTable");
}

PHP_METHOD(AdsDictionary, removeTable)
{
    char      *alias;  size_t alias_len;
    zend_bool  deleteFiles = 0;

    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(alias, alias_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(deleteFiles)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDRemoveTable(
        dict->hDict,
        (UNSIGNED8 *)alias,
        (UNSIGNED16)(deleteFiles ? 1 : 0));
    ADS_CHECK_RC(ulRet, "AdsDictionary::removeTable");
}

PHP_METHOD(AdsDictionary, getTableProperty)
{
    char      *tableName;  size_t tableName_len;
    zend_long  property;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(tableName, tableName_len)
        Z_PARAM_LONG(property)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    char       buf[1024];
    UNSIGNED16 usLen = (UNSIGNED16)(sizeof(buf) - 1);
    UNSIGNED32 ulRet = AdsDDGetTableProperty(
        dict->hDict, (UNSIGNED8 *)tableName, (UNSIGNED16)property,
        buf, &usLen);
    ADS_CHECK_RC(ulRet, "AdsDictionary::getTableProperty");
    buf[usLen] = '\0';
    RETURN_STRINGL(buf, (size_t)usLen);
}

PHP_METHOD(AdsDictionary, setTableProperty)
{
    char      *tableName;  size_t tableName_len;
    zend_long  property;
    char      *value;      size_t value_len;

    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_STRING(tableName, tableName_len)
        Z_PARAM_LONG(property)
        Z_PARAM_STRING(value, value_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDSetTableProperty(
        dict->hDict, (UNSIGNED8 *)tableName, (UNSIGNED16)property,
        (void *)value, (UNSIGNED16)value_len);
    ADS_CHECK_RC(ulRet, "AdsDictionary::setTableProperty");
}

/* =====================================================================
 * Field properties
 * ===================================================================== */

PHP_METHOD(AdsDictionary, getFieldProperty)
{
    char      *tableName;  size_t tableName_len;
    char      *field;      size_t field_len;
    zend_long  property;

    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_STRING(tableName, tableName_len)
        Z_PARAM_STRING(field, field_len)
        Z_PARAM_LONG(property)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    char       buf[1024];
    UNSIGNED16 usLen = (UNSIGNED16)(sizeof(buf) - 1);
    UNSIGNED32 ulRet = AdsDDGetFieldProperty(
        dict->hDict, (UNSIGNED8 *)tableName, (UNSIGNED8 *)field,
        (UNSIGNED16)property, buf, &usLen);
    ADS_CHECK_RC(ulRet, "AdsDictionary::getFieldProperty");
    buf[usLen] = '\0';
    RETURN_STRINGL(buf, (size_t)usLen);
}

PHP_METHOD(AdsDictionary, setFieldProperty)
{
    char      *tableName;  size_t tableName_len;
    char      *field;      size_t field_len;
    zend_long  property;
    char      *value;      size_t value_len;

    ZEND_PARSE_PARAMETERS_START(4, 4)
        Z_PARAM_STRING(tableName, tableName_len)
        Z_PARAM_STRING(field, field_len)
        Z_PARAM_LONG(property)
        Z_PARAM_STRING(value, value_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDSetFieldProperty(
        dict->hDict, (UNSIGNED8 *)tableName, (UNSIGNED8 *)field,
        (UNSIGNED16)property, (void *)value, (UNSIGNED16)value_len);
    ADS_CHECK_RC(ulRet, "AdsDictionary::setFieldProperty");
}

/* =====================================================================
 * Index file management
 * ===================================================================== */

PHP_METHOD(AdsDictionary, addIndexFile)
{
    char *tableName;  size_t tableName_len;
    char *indexPath;  size_t indexPath_len;
    char *comment = ""; size_t comment_len = 0;

    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_STRING(tableName, tableName_len)
        Z_PARAM_STRING(indexPath, indexPath_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(comment, comment_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDAddIndexFile(
        dict->hDict,
        (UNSIGNED8 *)tableName,
        (UNSIGNED8 *)indexPath,
        (UNSIGNED8 *)comment);
    ADS_CHECK_RC(ulRet, "AdsDictionary::addIndexFile");
}

PHP_METHOD(AdsDictionary, removeIndexFile)
{
    char      *tableName;  size_t tableName_len;
    char      *indexPath;  size_t indexPath_len;
    zend_bool  deleteFile = 0;

    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_STRING(tableName, tableName_len)
        Z_PARAM_STRING(indexPath, indexPath_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(deleteFile)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDRemoveIndexFile(
        dict->hDict,
        (UNSIGNED8 *)tableName,
        (UNSIGNED8 *)indexPath,
        (UNSIGNED16)(deleteFile ? 1 : 0));
    ADS_CHECK_RC(ulRet, "AdsDictionary::removeIndexFile");
}

PHP_METHOD(AdsDictionary, getIndexProperty)
{
    char      *tableName;  size_t tableName_len;
    char      *indexName;  size_t indexName_len;
    zend_long  property;

    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_STRING(tableName, tableName_len)
        Z_PARAM_STRING(indexName, indexName_len)
        Z_PARAM_LONG(property)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    char       buf[1024];
    UNSIGNED16 usLen = (UNSIGNED16)(sizeof(buf) - 1);
    UNSIGNED32 ulRet = AdsDDGetIndexProperty(
        dict->hDict, (UNSIGNED8 *)tableName, (UNSIGNED8 *)indexName,
        (UNSIGNED16)property, buf, &usLen);
    ADS_CHECK_RC(ulRet, "AdsDictionary::getIndexProperty");
    buf[usLen] = '\0';
    RETURN_STRINGL(buf, (size_t)usLen);
}

PHP_METHOD(AdsDictionary, setIndexProperty)
{
    char      *tableName;  size_t tableName_len;
    char      *indexName;  size_t indexName_len;
    zend_long  property;
    char      *value;      size_t value_len;

    ZEND_PARSE_PARAMETERS_START(4, 4)
        Z_PARAM_STRING(tableName, tableName_len)
        Z_PARAM_STRING(indexName, indexName_len)
        Z_PARAM_LONG(property)
        Z_PARAM_STRING(value, value_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDSetIndexProperty(
        dict->hDict, (UNSIGNED8 *)tableName, (UNSIGNED8 *)indexName,
        (UNSIGNED16)property, (void *)value, (UNSIGNED16)value_len);
    ADS_CHECK_RC(ulRet, "AdsDictionary::setIndexProperty");
}

/* =====================================================================
 * User management
 * ===================================================================== */

PHP_METHOD(AdsDictionary, createUser)
{
    char *user;        size_t user_len;
    char *password = ""; size_t password_len = 0;
    char *group    = ""; size_t group_len    = 0;
    char *desc     = ""; size_t desc_len     = 0;

    ZEND_PARSE_PARAMETERS_START(1, 4)
        Z_PARAM_STRING(user, user_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(password, password_len)
        Z_PARAM_STRING(group, group_len)
        Z_PARAM_STRING(desc, desc_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDCreateUser(
        dict->hDict,
        (UNSIGNED8 *)group,
        (UNSIGNED8 *)user,
        (UNSIGNED8 *)password,
        (UNSIGNED8 *)desc);
    ADS_CHECK_RC(ulRet, "AdsDictionary::createUser");
}

PHP_METHOD(AdsDictionary, deleteUser)
{
    char *user;  size_t user_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(user, user_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDDeleteUser(dict->hDict, (UNSIGNED8 *)user);
    ADS_CHECK_RC(ulRet, "AdsDictionary::deleteUser");
}

PHP_METHOD(AdsDictionary, getUserProperty)
{
    char      *user;     size_t user_len;
    zend_long  property;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(user, user_len)
        Z_PARAM_LONG(property)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    char       buf[1024];
    UNSIGNED16 usLen = (UNSIGNED16)(sizeof(buf) - 1);
    UNSIGNED32 ulRet = AdsDDGetUserProperty(
        dict->hDict, (UNSIGNED8 *)user, (UNSIGNED16)property,
        buf, &usLen);
    ADS_CHECK_RC(ulRet, "AdsDictionary::getUserProperty");
    buf[usLen] = '\0';
    RETURN_STRINGL(buf, (size_t)usLen);
}

PHP_METHOD(AdsDictionary, setUserProperty)
{
    char      *user;   size_t user_len;
    zend_long  property;
    char      *value;  size_t value_len;

    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_STRING(user, user_len)
        Z_PARAM_LONG(property)
        Z_PARAM_STRING(value, value_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDSetUserProperty(
        dict->hDict, (UNSIGNED8 *)user, (UNSIGNED16)property,
        (void *)value, (UNSIGNED16)value_len);
    ADS_CHECK_RC(ulRet, "AdsDictionary::setUserProperty");
}

PHP_METHOD(AdsDictionary, addUserToGroup)
{
    char *user;   size_t user_len;
    char *group;  size_t group_len;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(user, user_len)
        Z_PARAM_STRING(group, group_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDAddUserToGroup(
        dict->hDict, (UNSIGNED8 *)group, (UNSIGNED8 *)user);
    ADS_CHECK_RC(ulRet, "AdsDictionary::addUserToGroup");
}

PHP_METHOD(AdsDictionary, removeUserFromGroup)
{
    char *user;   size_t user_len;
    char *group;  size_t group_len;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(user, user_len)
        Z_PARAM_STRING(group, group_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDRemoveUserFromGroup(
        dict->hDict, (UNSIGNED8 *)group, (UNSIGNED8 *)user);
    ADS_CHECK_RC(ulRet, "AdsDictionary::removeUserFromGroup");
}

PHP_METHOD(AdsDictionary, getUserTableRights)
{
    char *tableName;  size_t tableName_len;
    char *user;       size_t user_len;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(tableName, tableName_len)
        Z_PARAM_STRING(user, user_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRights = 0;
    UNSIGNED32 ulRet = AdsDDGetUserTableRights(
        dict->hDict, (UNSIGNED8 *)tableName, (UNSIGNED8 *)user, &ulRights);
    ADS_CHECK_RC(ulRet, "AdsDictionary::getUserTableRights");
    RETURN_LONG((zend_long)ulRights);
}

PHP_METHOD(AdsDictionary, setUserTableRights)
{
    char      *tableName;  size_t tableName_len;
    char      *user;       size_t user_len;
    zend_long  level;

    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_STRING(tableName, tableName_len)
        Z_PARAM_STRING(user, user_len)
        Z_PARAM_LONG(level)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDSetUserTableRights(
        dict->hDict, (UNSIGNED8 *)tableName, (UNSIGNED8 *)user,
        (UNSIGNED32)level);
    ADS_CHECK_RC(ulRet, "AdsDictionary::setUserTableRights");
}

/* =====================================================================
 * Views
 * ===================================================================== */

PHP_METHOD(AdsDictionary, createView)
{
    char *name;         size_t name_len;
    char *sql;          size_t sql_len;
    char *comment = ""; size_t comment_len = 0;

    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_STRING(sql, sql_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(comment, comment_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    /* OpenADS: AdsDDCreateView(hConn, name, comment, sql) */
    UNSIGNED32 ulRet = AdsDDCreateView(
        dict->hDict,
        (UNSIGNED8 *)name,
        (UNSIGNED8 *)comment,
        (UNSIGNED8 *)sql);
    ADS_CHECK_RC(ulRet, "AdsDictionary::createView");
}

PHP_METHOD(AdsDictionary, dropView)
{
    char *name;  size_t name_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDDropView(dict->hDict, (UNSIGNED8 *)name);
    ADS_CHECK_RC(ulRet, "AdsDictionary::dropView");
}

PHP_METHOD(AdsDictionary, getViewProperty)
{
    char      *name;  size_t name_len;
    zend_long  property;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_LONG(property)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    char       buf[4096];
    UNSIGNED16 usLen = (UNSIGNED16)(sizeof(buf) - 1);
    UNSIGNED32 ulRet = AdsDDGetViewProperty(
        dict->hDict, (UNSIGNED8 *)name, (UNSIGNED16)property, buf, &usLen);
    ADS_CHECK_RC(ulRet, "AdsDictionary::getViewProperty");
    buf[usLen] = '\0';
    RETURN_STRINGL(buf, (size_t)usLen);
}

PHP_METHOD(AdsDictionary, setViewProperty)
{
    char      *name;   size_t name_len;
    zend_long  property;
    char      *value;  size_t value_len;

    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_LONG(property)
        Z_PARAM_STRING(value, value_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDSetViewProperty(
        dict->hDict, (UNSIGNED8 *)name, (UNSIGNED16)property,
        (void *)value, (UNSIGNED16)value_len);
    ADS_CHECK_RC(ulRet, "AdsDictionary::setViewProperty");
}

/* =====================================================================
 * Stored procedures
 * ===================================================================== */

PHP_METHOD(AdsDictionary, createProcedure)
{
    char *name;         size_t name_len;
    char *container;    size_t container_len;
    char *procedure;    size_t procedure_len;
    char *input   = ""; size_t input_len   = 0;
    char *output  = ""; size_t output_len  = 0;
    char *comment = ""; size_t comment_len = 0;

    ZEND_PARSE_PARAMETERS_START(3, 6)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_STRING(container, container_len)
        Z_PARAM_STRING(procedure, procedure_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(input, input_len)
        Z_PARAM_STRING(output, output_len)
        Z_PARAM_STRING(comment, comment_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDCreateProcedure(
        dict->hDict,
        (UNSIGNED8 *)name,
        (UNSIGNED8 *)container,
        (UNSIGNED8 *)procedure,
        0,                      /* ulInvokeOption — not stored */
        (UNSIGNED8 *)input,
        (UNSIGNED8 *)output,
        (UNSIGNED8 *)comment);
    ADS_CHECK_RC(ulRet, "AdsDictionary::createProcedure");
}

PHP_METHOD(AdsDictionary, dropProcedure)
{
    char *name;  size_t name_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDDropProcedure(dict->hDict, (UNSIGNED8 *)name);
    ADS_CHECK_RC(ulRet, "AdsDictionary::dropProcedure");
}

PHP_METHOD(AdsDictionary, getProcProperty)
{
    char      *name;  size_t name_len;
    zend_long  property;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_LONG(property)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    char       buf[4096];
    UNSIGNED16 usLen = (UNSIGNED16)(sizeof(buf) - 1);
    UNSIGNED32 ulRet = AdsDDGetProcProperty(
        dict->hDict, (UNSIGNED8 *)name, (UNSIGNED16)property, buf, &usLen);
    ADS_CHECK_RC(ulRet, "AdsDictionary::getProcProperty");
    buf[usLen] = '\0';
    RETURN_STRINGL(buf, (size_t)usLen);
}

PHP_METHOD(AdsDictionary, setProcProperty)
{
    char      *name;   size_t name_len;
    zend_long  property;
    char      *value;  size_t value_len;

    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_LONG(property)
        Z_PARAM_STRING(value, value_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDSetProcProperty(
        dict->hDict, (UNSIGNED8 *)name, (UNSIGNED16)property,
        (void *)value, (UNSIGNED16)value_len);
    ADS_CHECK_RC(ulRet, "AdsDictionary::setProcProperty");
}

/* =====================================================================
 * Triggers
 * ===================================================================== */

PHP_METHOD(AdsDictionary, createTrigger)
{
    char      *name;          size_t name_len;
    char      *table;         size_t table_len;
    zend_long  type;
    char      *container = ""; size_t container_len = 0;
    char      *procedure = ""; size_t procedure_len = 0;
    zend_long  priority  = 1;

    ZEND_PARSE_PARAMETERS_START(3, 6)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_STRING(table, table_len)
        Z_PARAM_LONG(type)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(container, container_len)
        Z_PARAM_STRING(procedure, procedure_len)
        Z_PARAM_LONG(priority)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDCreateTrigger(
        dict->hDict,
        (UNSIGNED8 *)name,
        (UNSIGNED8 *)table,
        (UNSIGNED32)type,
        0,
        (UNSIGNED8 *)container,
        (UNSIGNED8 *)procedure,
        (UNSIGNED32)priority);
    ADS_CHECK_RC(ulRet, "AdsDictionary::createTrigger");
}

PHP_METHOD(AdsDictionary, dropTrigger)
{
    char *name;  size_t name_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDDropTrigger(dict->hDict, (UNSIGNED8 *)name);
    ADS_CHECK_RC(ulRet, "AdsDictionary::dropTrigger");
}

PHP_METHOD(AdsDictionary, getTriggerProperty)
{
    char      *name;  size_t name_len;
    zend_long  property;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_LONG(property)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    char       buf[1024];
    UNSIGNED16 usLen = (UNSIGNED16)(sizeof(buf) - 1);
    UNSIGNED32 ulRet = AdsDDGetTriggerProperty(
        dict->hDict, (UNSIGNED8 *)name, (UNSIGNED16)property, buf, &usLen);
    ADS_CHECK_RC(ulRet, "AdsDictionary::getTriggerProperty");
    buf[usLen] = '\0';
    RETURN_STRINGL(buf, (size_t)usLen);
}

PHP_METHOD(AdsDictionary, setTriggerProperty)
{
    char      *name;   size_t name_len;
    zend_long  property;
    char      *value;  size_t value_len;

    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_LONG(property)
        Z_PARAM_STRING(value, value_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDSetTriggerProperty(
        dict->hDict, (UNSIGNED8 *)name, (UNSIGNED16)property,
        (void *)value, (UNSIGNED16)value_len);
    ADS_CHECK_RC(ulRet, "AdsDictionary::setTriggerProperty");
}

/* =====================================================================
 * Referential integrity
 * ===================================================================== */

PHP_METHOD(AdsDictionary, createRefIntegrity)
{
    char      *name;       size_t name_len;
    char      *failTable;  size_t failTable_len;
    char      *parent;     size_t parent_len;
    char      *parentTag;  size_t parentTag_len;
    char      *child;      size_t child_len;
    char      *childTag;   size_t childTag_len;
    zend_long  updateRule = 0;
    zend_long  deleteRule = 0;

    ZEND_PARSE_PARAMETERS_START(6, 8)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_STRING(failTable, failTable_len)
        Z_PARAM_STRING(parent, parent_len)
        Z_PARAM_STRING(parentTag, parentTag_len)
        Z_PARAM_STRING(child, child_len)
        Z_PARAM_STRING(childTag, childTag_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(updateRule)
        Z_PARAM_LONG(deleteRule)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDCreateRefIntegrity(
        dict->hDict,
        (UNSIGNED8 *)name,
        (UNSIGNED8 *)failTable,
        (UNSIGNED8 *)parent,
        (UNSIGNED8 *)parentTag,
        (UNSIGNED8 *)child,
        (UNSIGNED8 *)childTag,
        (UNSIGNED16)updateRule,
        (UNSIGNED16)deleteRule);
    ADS_CHECK_RC(ulRet, "AdsDictionary::createRefIntegrity");
}

PHP_METHOD(AdsDictionary, removeRefIntegrity)
{
    char *name;  size_t name_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDRemoveRefIntegrity(dict->hDict, (UNSIGNED8 *)name);
    ADS_CHECK_RC(ulRet, "AdsDictionary::removeRefIntegrity");
}

/* =====================================================================
 * Links
 * ===================================================================== */

PHP_METHOD(AdsDictionary, createLink)
{
    char *alias;         size_t alias_len;
    char *path;          size_t path_len;
    char *user     = ""; size_t user_len     = 0;
    char *password = ""; size_t password_len = 0;

    ZEND_PARSE_PARAMETERS_START(2, 4)
        Z_PARAM_STRING(alias, alias_len)
        Z_PARAM_STRING(path, path_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(user, user_len)
        Z_PARAM_STRING(password, password_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDCreateLink(
        dict->hDict,
        (UNSIGNED8 *)alias,
        (UNSIGNED8 *)path,
        (UNSIGNED8 *)user,
        (UNSIGNED8 *)password,
        0);
    ADS_CHECK_RC(ulRet, "AdsDictionary::createLink");
}

PHP_METHOD(AdsDictionary, dropLink)
{
    char *alias;  size_t alias_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(alias, alias_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDDropLink(dict->hDict, (UNSIGNED8 *)alias, 0);
    ADS_CHECK_RC(ulRet, "AdsDictionary::dropLink");
}

PHP_METHOD(AdsDictionary, modifyLink)
{
    char *alias;         size_t alias_len;
    char *path     = ""; size_t path_len     = 0;
    char *user     = ""; size_t user_len     = 0;
    char *password = ""; size_t password_len = 0;

    ZEND_PARSE_PARAMETERS_START(1, 4)
        Z_PARAM_STRING(alias, alias_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(path, path_len)
        Z_PARAM_STRING(user, user_len)
        Z_PARAM_STRING(password, password_len)
    ZEND_PARSE_PARAMETERS_END();

    ads_dictionary_obj *dict = Z_ADS_DICT_P(ZEND_THIS);
    ADS_CHECK_DICT(dict);

    UNSIGNED32 ulRet = AdsDDModifyLink(
        dict->hDict,
        (UNSIGNED8 *)alias,
        (UNSIGNED8 *)path,
        (UNSIGNED8 *)user,
        (UNSIGNED8 *)password,
        0);
    ADS_CHECK_RC(ulRet, "AdsDictionary::modifyLink");
}

/* =====================================================================
 * Method table and class registration
 * ===================================================================== */

static const zend_function_entry ads_dictionary_methods[] = {
    /* lifecycle */
    PHP_ME(AdsDictionary, open,                 arginfo_ads_dictionary_open,                  ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(AdsDictionary, fromConnection,       arginfo_ads_dictionary_from_connection,       ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(AdsDictionary, close,                arginfo_ads_dictionary_close,                 ZEND_ACC_PUBLIC)
    /* database */
    PHP_ME(AdsDictionary, getDatabaseProperty,  arginfo_ads_dictionary_get_database_property, ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, setDatabaseProperty,  arginfo_ads_dictionary_set_database_property, ZEND_ACC_PUBLIC)
    /* tables */
    PHP_ME(AdsDictionary, addTable,             arginfo_ads_dictionary_add_table,             ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, removeTable,          arginfo_ads_dictionary_remove_table,          ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, getTableProperty,     arginfo_ads_dictionary_get_table_property,    ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, setTableProperty,     arginfo_ads_dictionary_set_table_property,    ZEND_ACC_PUBLIC)
    /* fields */
    PHP_ME(AdsDictionary, getFieldProperty,     arginfo_ads_dictionary_get_field_property,    ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, setFieldProperty,     arginfo_ads_dictionary_set_field_property,    ZEND_ACC_PUBLIC)
    /* indexes */
    PHP_ME(AdsDictionary, addIndexFile,         arginfo_ads_dictionary_add_index_file,        ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, removeIndexFile,      arginfo_ads_dictionary_remove_index_file,     ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, getIndexProperty,     arginfo_ads_dictionary_get_index_property,    ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, setIndexProperty,     arginfo_ads_dictionary_set_index_property,    ZEND_ACC_PUBLIC)
    /* users */
    PHP_ME(AdsDictionary, createUser,           arginfo_ads_dictionary_create_user,           ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, deleteUser,           arginfo_ads_dictionary_delete_user,           ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, getUserProperty,      arginfo_ads_dictionary_get_user_property,     ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, setUserProperty,      arginfo_ads_dictionary_set_user_property,     ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, addUserToGroup,       arginfo_ads_dictionary_add_user_to_group,     ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, removeUserFromGroup,  arginfo_ads_dictionary_remove_user_from_group,ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, getUserTableRights,   arginfo_ads_dictionary_get_user_table_rights, ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, setUserTableRights,   arginfo_ads_dictionary_set_user_table_rights, ZEND_ACC_PUBLIC)
    /* views */
    PHP_ME(AdsDictionary, createView,           arginfo_ads_dictionary_create_view,           ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, dropView,             arginfo_ads_dictionary_drop_view,             ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, getViewProperty,      arginfo_ads_dictionary_get_view_property,     ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, setViewProperty,      arginfo_ads_dictionary_set_view_property,     ZEND_ACC_PUBLIC)
    /* procedures */
    PHP_ME(AdsDictionary, createProcedure,      arginfo_ads_dictionary_create_procedure,      ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, dropProcedure,        arginfo_ads_dictionary_drop_procedure,        ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, getProcProperty,      arginfo_ads_dictionary_get_proc_property,     ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, setProcProperty,      arginfo_ads_dictionary_set_proc_property,     ZEND_ACC_PUBLIC)
    /* triggers */
    PHP_ME(AdsDictionary, createTrigger,        arginfo_ads_dictionary_create_trigger,        ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, dropTrigger,          arginfo_ads_dictionary_drop_trigger,          ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, getTriggerProperty,   arginfo_ads_dictionary_get_trigger_property,  ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, setTriggerProperty,   arginfo_ads_dictionary_set_trigger_property,  ZEND_ACC_PUBLIC)
    /* referential integrity */
    PHP_ME(AdsDictionary, createRefIntegrity,   arginfo_ads_dictionary_create_ref_integrity,  ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, removeRefIntegrity,   arginfo_ads_dictionary_remove_ref_integrity,  ZEND_ACC_PUBLIC)
    /* links */
    PHP_ME(AdsDictionary, createLink,           arginfo_ads_dictionary_create_link,           ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, dropLink,             arginfo_ads_dictionary_drop_link,             ZEND_ACC_PUBLIC)
    PHP_ME(AdsDictionary, modifyLink,           arginfo_ads_dictionary_modify_link,           ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* =====================================================================
 * Standalone function: ads_dd_create
 * OpenADS: AdsDDCreate(path, encrypt, password) — 3 args, no handle
 * ads_dd_create(string $path, bool $encrypt = false, string $password = ''): void
 * ===================================================================== */
PHP_FUNCTION(ads_dd_create)
{
    char      *path     = NULL;
    size_t     pathLen  = 0;
    zend_bool  encrypt  = 0;
    char      *extra    = NULL;
    size_t     extraLen = 0;

    ZEND_PARSE_PARAMETERS_START(1, 3)
        Z_PARAM_STRING(path, pathLen)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(encrypt)
        Z_PARAM_STRING(extra, extraLen)
    ZEND_PARSE_PARAMETERS_END();

    ADSHANDLE  hConn = 0;
    UNSIGNED32 ulRet = AdsDDCreate(
        (UNSIGNED8 *)path,
        (UNSIGNED16)(encrypt ? 1 : 0),
        (UNSIGNED8 *)(extra && extraLen > 0 ? extra : (char *)""),
        &hConn
    );
    if (ulRet != AE_SUCCESS) {
        ads_throw_ace_exception(ulRet, "ads_dd_create");
        RETURN_THROWS();
    }
    if (hConn != 0) {
        AdsDisconnect(hConn);
    }
}

/* =====================================================================
 * Combined registration
 * ===================================================================== */
void ads_misc_register_classes(void)
{
    zend_class_entry ce;

    /* AdsTransaction */
    INIT_CLASS_ENTRY(ce, "AdsTransaction", ads_transaction_methods);
    ads_transaction_ce = zend_register_internal_class(&ce);
    ads_transaction_ce->create_object = ads_transaction_create_object;

    memcpy(&ads_transaction_handlers,
           zend_get_std_object_handlers(),
           sizeof(zend_object_handlers));
    ads_transaction_handlers.offset    = XtOffsetOf(ads_transaction_obj, std);
    ads_transaction_handlers.free_obj  = ads_transaction_free_object;
    ads_transaction_handlers.clone_obj = NULL;

    /* AdsDictionary */
    INIT_CLASS_ENTRY(ce, "AdsDictionary", ads_dictionary_methods);
    ads_dictionary_ce = zend_register_internal_class(&ce);
    ads_dictionary_ce->create_object = ads_dictionary_create_object;

    memcpy(&ads_dictionary_handlers,
           zend_get_std_object_handlers(),
           sizeof(zend_object_handlers));
    ads_dictionary_handlers.offset    = XtOffsetOf(ads_dictionary_obj, std);
    ads_dictionary_handlers.free_obj  = ads_dictionary_free_object;
    ads_dictionary_handlers.clone_obj = NULL;
}
