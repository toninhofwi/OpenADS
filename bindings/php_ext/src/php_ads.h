#ifndef PHP_ADS_H
#define PHP_ADS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Define before ace.h to get x64 handle type and WINAPI entrypoints */
#ifndef WIN32
#define WIN32
#endif
#ifndef x64
#define x64
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"

/* ZTS static LS cache — defined once in php_ads.c, extern here for all TUs */
#if defined(ZTS) && defined(COMPILE_DL_OPENADS)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#include "openads/ace.h"

/* SAP ACE compatibility aliases not present in OpenADS ace.h */
#ifndef DOUBLE
typedef double DOUBLE;
#endif
#define ADS_TRIM   4
#define ADS_LTRIM  8
#define ADS_RTRIM  16

#define PHP_ADS_VERSION "1.0.0"
#define PHP_ADS_EXTNAME "openads"

extern zend_module_entry ads_module_entry;
#define phpext_ads_ptr &ads_module_entry

/* -----------------------------------------------------------------------
 * Class entries (one per PHP class)
 * --------------------------------------------------------------------- */
extern zend_class_entry *ads_connection_ce;
extern zend_class_entry *ads_statement_ce;
extern zend_class_entry *ads_table_ce;
extern zend_class_entry *ads_dictionary_ce;
extern zend_class_entry *ads_transaction_ce;
extern zend_class_entry *ads_exception_ce;
extern zend_class_entry *ads_prepared_ce;

/* -----------------------------------------------------------------------
 * Object handlers
 * --------------------------------------------------------------------- */
extern zend_object_handlers ads_connection_handlers;
extern zend_object_handlers ads_statement_handlers;
extern zend_object_handlers ads_table_handlers;
extern zend_object_handlers ads_dictionary_handlers;
extern zend_object_handlers ads_transaction_handlers;
extern zend_object_handlers ads_prepared_handlers;

/* -----------------------------------------------------------------------
 * Internal object structs — zend_object MUST be the last member
 * --------------------------------------------------------------------- */
typedef struct {
    ADSHANDLE   hConn;
    zend_bool   closed;
    zend_object std;
} ads_connection_obj;

typedef struct {
    ADSHANDLE   hStmt;
    ADSHANDLE   hCursor;
    UNSIGNED16  numFields;
    zend_bool   executed;
    zend_bool   eof;
    /* Cached field names (populated once on first fetch, avoid AdsGetFieldName per row) */
    char      **fieldNames;   /* NULL until initialized; array of numFields C strings */
    zend_object std;
} ads_statement_obj;

typedef struct {
    ADSHANDLE   hTable;
    zend_bool   closed;
    zend_object std;
} ads_table_obj;

typedef struct {
    ADSHANDLE   hDict;
    zend_bool   closed;
    zend_object std;
} ads_dictionary_obj;

typedef struct {
    ADSHANDLE   hConn;   /* borrowed reference — connection owns it */
    zend_bool   active;
    zend_object std;
} ads_transaction_obj;

typedef struct {
    ADSHANDLE   hStmt;
    zend_bool   closed;
    zend_object std;
} ads_prepared_obj;

/* -----------------------------------------------------------------------
 * Object-from-zend_object accessors
 * --------------------------------------------------------------------- */
static zend_always_inline ads_connection_obj *ads_conn_from_obj(zend_object *obj) {
    return (ads_connection_obj *)((char *)(obj) - XtOffsetOf(ads_connection_obj, std));
}
static zend_always_inline ads_statement_obj *ads_stmt_from_obj(zend_object *obj) {
    return (ads_statement_obj *)((char *)(obj) - XtOffsetOf(ads_statement_obj, std));
}
static zend_always_inline ads_table_obj *ads_table_from_obj(zend_object *obj) {
    return (ads_table_obj *)((char *)(obj) - XtOffsetOf(ads_table_obj, std));
}
static zend_always_inline ads_dictionary_obj *ads_dict_from_obj(zend_object *obj) {
    return (ads_dictionary_obj *)((char *)(obj) - XtOffsetOf(ads_dictionary_obj, std));
}
static zend_always_inline ads_transaction_obj *ads_trans_from_obj(zend_object *obj) {
    return (ads_transaction_obj *)((char *)(obj) - XtOffsetOf(ads_transaction_obj, std));
}
static zend_always_inline ads_prepared_obj *ads_prep_from_obj(zend_object *obj) {
    return (ads_prepared_obj *)((char *)(obj) - XtOffsetOf(ads_prepared_obj, std));
}

#define Z_ADS_CONN_P(zv)   ads_conn_from_obj(Z_OBJ_P(zv))
#define Z_ADS_STMT_P(zv)   ads_stmt_from_obj(Z_OBJ_P(zv))
#define Z_ADS_TABLE_P(zv)  ads_table_from_obj(Z_OBJ_P(zv))
#define Z_ADS_DICT_P(zv)   ads_dict_from_obj(Z_OBJ_P(zv))
#define Z_ADS_TRANS_P(zv)  ads_trans_from_obj(Z_OBJ_P(zv))
#define Z_ADS_PREP_P(zv)   ads_prep_from_obj(Z_OBJ_P(zv))

/* -----------------------------------------------------------------------
 * Error helpers
 * --------------------------------------------------------------------- */
void ads_throw_ace_exception(UNSIGNED32 ulErrCode, const char *context);

/* Throw if connection/table/statement handle is already closed */
#define ADS_CHECK_CONN_CLOSED(obj) \
    do { if ((obj)->closed || (obj)->hConn == 0) { \
        zend_throw_exception(ads_exception_ce, "AdsConnection is already closed", 0); \
        RETURN_THROWS(); \
    } } while(0)

#define ADS_CHECK_TABLE_CLOSED(obj) \
    do { if ((obj)->closed || (obj)->hTable == 0) { \
        zend_throw_exception(ads_exception_ce, "AdsTable is already closed", 0); \
        RETURN_THROWS(); \
    } } while(0)

#define ADS_CHECK_STMT_CLOSED(obj) \
    do { if ((obj)->hStmt == 0) { \
        zend_throw_exception(ads_exception_ce, "AdsStatement is already closed", 0); \
        RETURN_THROWS(); \
    } } while(0)

#define ADS_CHECK_DICT(obj) \
    do { if ((obj)->hDict == 0) { \
        zend_throw_exception(ads_exception_ce, "AdsDictionary is closed", 0); \
        RETURN_THROWS(); \
    } } while(0)

#define ADS_CHECK_PREP_CLOSED(obj) \
    do { if ((obj)->closed || (obj)->hStmt == 0) { \
        zend_throw_exception(ads_exception_ce, "AdsPreparedStatement is already closed", 0); \
        RETURN_THROWS(); \
    } } while(0)

/* Throw on ACE error and return */
#define ADS_CHECK_RC(rc, ctx) \
    do { if ((rc) != AE_SUCCESS) { \
        ads_throw_ace_exception((rc), (ctx)); \
        RETURN_THROWS(); \
    } } while(0)

/* -----------------------------------------------------------------------
 * Shared helper: fetch a field value into a zval
 * --------------------------------------------------------------------- */
void ads_get_field_zval(ADSHANDLE hCursor, const char *fieldName, zval *retval);

/* -----------------------------------------------------------------------
 * Per-class registration functions (called from MINIT)
 * --------------------------------------------------------------------- */
void ads_connection_register_class(void);
void ads_statement_register_class(void);
void ads_table_register_class(void);
void ads_misc_register_classes(void);   /* dictionary + transaction */
void ads_prepared_register_class(void);

#endif /* PHP_ADS_H */
