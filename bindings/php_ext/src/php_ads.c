#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php_ads.h"
#include "ads_arginfo.h"

/* Forward declaration for the standalone function implemented in ads_misc.c */
PHP_FUNCTION(ads_dd_create);

/* Defines _tsrm_ls_cache as a plain global (TSRM_TLS stripped via Makefile).
 * With ZEND_ENABLE_STATIC_TSRMLS_CACHE=0, EG()/CG() call tsrm_get_ls_cache()
 * directly and never read this variable — thread-safe, no .tls section needed. */
#if defined(ZTS) && defined(COMPILE_DL_OPENADS)
ZEND_TSRMLS_CACHE_DEFINE()
#endif

/* -----------------------------------------------------------------------
 * Global class entries
 * --------------------------------------------------------------------- */
zend_class_entry *ads_connection_ce;
zend_class_entry *ads_statement_ce;
zend_class_entry *ads_table_ce;
zend_class_entry *ads_dictionary_ce;
zend_class_entry *ads_transaction_ce;
zend_class_entry *ads_exception_ce;
zend_class_entry *ads_prepared_ce;

/* -----------------------------------------------------------------------
 * Error helper: look up ACE error text and throw AdsException
 * --------------------------------------------------------------------- */
void ads_throw_ace_exception(UNSIGNED32 ulErrCode, const char *context)
{
    char       errBuf[4096];
    UNSIGNED16 usLen = (UNSIGNED16)sizeof(errBuf) - 1;
    char       lastBuf[1024];
    UNSIGNED16 usLastLen = (UNSIGNED16)sizeof(lastBuf) - 1;
    UNSIGNED32 ulLastCode = 0;
    char       msg[8192];

    errBuf[0]  = '\0';
    lastBuf[0] = '\0';

    AdsGetErrorString(ulErrCode, (UNSIGNED8 *)errBuf, &usLen);
    errBuf[usLen] = '\0';

    /* For AQE (SQL engine) errors, AdsGetLastError gives the real SQL error */
    AdsGetLastError(&ulLastCode, (UNSIGNED8 *)lastBuf, &usLastLen);
    lastBuf[usLastLen] = '\0';

    if (context && context[0]) {
        if (lastBuf[0] && ulLastCode != 0 && ulLastCode != ulErrCode) {
            snprintf(msg, sizeof(msg), "%s: [%lu] %s | last=[%lu] %s",
                     context, (unsigned long)ulErrCode, errBuf,
                     (unsigned long)ulLastCode, lastBuf);
        } else if (lastBuf[0]) {
            snprintf(msg, sizeof(msg), "%s: [%lu] %s | %s",
                     context, (unsigned long)ulErrCode, errBuf, lastBuf);
        } else {
            snprintf(msg, sizeof(msg), "%s: [%lu] %s", context, (unsigned long)ulErrCode, errBuf);
        }
    } else {
        if (lastBuf[0]) {
            snprintf(msg, sizeof(msg), "[%lu] %s | %s",
                     (unsigned long)ulErrCode, errBuf, lastBuf);
        } else {
            snprintf(msg, sizeof(msg), "[%lu] %s", (unsigned long)ulErrCode, errBuf);
        }
    }

    zend_throw_exception(ads_exception_ce, msg, (zend_long)ulErrCode);
}

/* -----------------------------------------------------------------------
 * Shared helper: read one field from an open cursor into a zval
 * Type-specific getters are used where possible; fall back to string.
 * --------------------------------------------------------------------- */
void ads_get_field_zval(ADSHANDLE hCursor, const char *fieldName, zval *retval)
{
    UNSIGNED16 usType = 0;
    UNSIGNED32 ulRet;

    ZVAL_NULL(retval);

    ulRet = AdsGetFieldType(hCursor, (UNSIGNED8 *)fieldName, &usType);
    if (ulRet != AE_SUCCESS) {
        return;
    }

    switch (usType) {
        case ADS_LOGICAL: {
            UNSIGNED16 bVal = 0;
            ulRet = AdsGetLogical(hCursor, (UNSIGNED8 *)fieldName, &bVal);
            if (ulRet == AE_SUCCESS) {
                ZVAL_BOOL(retval, bVal);
            }
            break;
        }

        case ADS_INTEGER:
        case ADS_AUTOINC:
        case ADS_SHORTINT: {
            SIGNED32 lVal = 0;
            ulRet = AdsGetLong(hCursor, (UNSIGNED8 *)fieldName, &lVal);
            if (ulRet == AE_SUCCESS) {
                ZVAL_LONG(retval, (zend_long)lVal);
            }
            break;
        }

        case ADS_DOUBLE:
        case ADS_CURDOUBLE:
        case ADS_MONEY: {
            DOUBLE dVal = 0.0;
            ulRet = AdsGetDouble(hCursor, (UNSIGNED8 *)fieldName, &dVal);
            if (ulRet == AE_SUCCESS) {
                ZVAL_DOUBLE(retval, dVal);
            }
            break;
        }

        case ADS_NUMERIC: {
            /* Numeric DBF fields: read as double */
            DOUBLE dVal = 0.0;
            ulRet = AdsGetDouble(hCursor, (UNSIGNED8 *)fieldName, &dVal);
            if (ulRet == AE_SUCCESS) {
                ZVAL_DOUBLE(retval, dVal);
            }
            break;
        }

        case ADS_MEMO:
        case ADS_NMEMO: {
            /* Memo fields: AdsGetFieldLength returns the schema pointer width,
             * not the content size. Use AdsGetMemoLength for the actual length. */
            UNSIGNED32 ulMemoLen = 0;
            UNSIGNED32 ulBufSize;
            char *buf;

            AdsGetMemoLength(hCursor, (UNSIGNED8 *)fieldName, &ulMemoLen);
            ulBufSize = (ulMemoLen > 0) ? ulMemoLen + 4 : 65536;

            buf = (char *)emalloc(ulBufSize);
            ulRet = AdsGetString(hCursor, (UNSIGNED8 *)fieldName,
                                 (UNSIGNED8 *)buf, &ulBufSize, ADS_TRIM);
            if (ulRet == AE_SUCCESS) {
                ZVAL_STRINGL(retval, buf, (size_t)ulBufSize);
            }
            efree(buf);
            break;
        }

        case ADS_BINARY:
        case ADS_IMAGE:
        case ADS_RAW: {
            /* Binary/image fields: return a display placeholder, not raw bytes,
             * so json_encode never chokes on non-UTF-8 byte sequences. */
            UNSIGNED32 ulBinLen = 0;
            AdsGetBinaryLength(hCursor, (UNSIGNED8 *)fieldName, &ulBinLen);
            ZVAL_STRING(retval, ulBinLen > 0 ? "BLOB" : "blob");
            break;
        }

        case ADS_ROWVERSION: {
            /* 8-byte row-version counter: read raw bytes, return as hex string. */
            unsigned char rawBuf[8] = {0};
            UNSIGNED32 ulBufSize = (UNSIGNED32)sizeof(rawBuf);
            ulRet = AdsGetBinary(hCursor, (UNSIGNED8 *)fieldName,
                                 0, (UNSIGNED8 *)rawBuf, &ulBufSize);
            if (ulRet == AE_SUCCESS && ulBufSize > 0) {
                static const char hc[] = "0123456789abcdef";
                char hexBuf[19] = "0x";
                UNSIGNED32 j;
                UNSIGNED32 n = ulBufSize < 8 ? ulBufSize : 8;
                for (j = 0; j < n; j++) {
                    hexBuf[2 + j * 2]     = hc[(rawBuf[j] >> 4) & 0xF];
                    hexBuf[2 + j * 2 + 1] = hc[ rawBuf[j]       & 0xF];
                }
                hexBuf[2 + n * 2] = '\0';
                ZVAL_STRINGL(retval, hexBuf, 2 + (size_t)n * 2);
            }
            break;
        }

        default: {
            /* String, Date, Timestamp, Time, Varchar, NChar, Raw, etc. */
            UNSIGNED32 ulFieldLen = 0;
            UNSIGNED32 ulBufSize;
            char *buf;

            AdsGetFieldLength(hCursor, (UNSIGNED8 *)fieldName, &ulFieldLen);
            ulBufSize = (ulFieldLen > 1) ? ulFieldLen + 4 : 65536;

            buf = (char *)emalloc(ulBufSize);
            ulRet = AdsGetString(hCursor, (UNSIGNED8 *)fieldName,
                                 (UNSIGNED8 *)buf, &ulBufSize, ADS_TRIM);
            if (ulRet == AE_SUCCESS) {
                ZVAL_STRINGL(retval, buf, (size_t)ulBufSize);
            }
            efree(buf);
            break;
        }
    }
}

/* -----------------------------------------------------------------------
 * Register ACE constants as PHP constants
 * --------------------------------------------------------------------- */
static void ads_register_constants(int module_number)
{
    /* Server type flags */
    REGISTER_LONG_CONSTANT("ADS_LOCAL_SERVER",       ADS_LOCAL_SERVER,       CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_REMOTE_SERVER",      ADS_REMOTE_SERVER,      CONST_CS | CONST_PERSISTENT);

    /* Table types */
    REGISTER_LONG_CONSTANT("ADS_NTX",  ADS_NTX,  CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_CDX",  ADS_CDX,  CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_ADT",  ADS_ADT,  CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_VFP",  ADS_VFP,  CONST_CS | CONST_PERSISTENT);

    /* Character sets */
    REGISTER_LONG_CONSTANT("ADS_ANSI", ADS_ANSI, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_OEM",  ADS_OEM,  CONST_CS | CONST_PERSISTENT);

    /* Open modes */
    REGISTER_LONG_CONSTANT("ADS_EXCLUSIVE", ADS_EXCLUSIVE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_SHARED",    ADS_SHARED,    CONST_CS | CONST_PERSISTENT);

    /* Locking types */
    REGISTER_LONG_CONSTANT("ADS_COMPATIBLE_LOCKING",  ADS_COMPATIBLE_LOCKING,  CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_PROPRIETARY_LOCKING", ADS_PROPRIETARY_LOCKING, CONST_CS | CONST_PERSISTENT);

    /* Rights checking */
    REGISTER_LONG_CONSTANT("ADS_CHECKRIGHTS",   ADS_CHECKRIGHTS,  CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_IGNORERIGHTS",  ADS_IGNORERIGHTS, CONST_CS | CONST_PERSISTENT);

    /* Filter modes */
    REGISTER_LONG_CONSTANT("ADS_RESPECTFILTERS", ADS_RESPECTFILTERS, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_IGNOREFILTERS",  ADS_IGNOREFILTERS,  CONST_CS | CONST_PERSISTENT);

    /* String trim flags */
    REGISTER_LONG_CONSTANT("ADS_TRIM",  ADS_TRIM,  CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_LTRIM", ADS_LTRIM, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_RTRIM", ADS_RTRIM, CONST_CS | CONST_PERSISTENT);

    /* Field types */
    REGISTER_LONG_CONSTANT("ADS_LOGICAL",    ADS_LOGICAL,    CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_NUMERIC",    ADS_NUMERIC,    CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_DATE",       ADS_DATE,       CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_STRING",     ADS_STRING,     CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_MEMO",       ADS_MEMO,       CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_BINARY",     ADS_BINARY,     CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_IMAGE",      ADS_IMAGE,      CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_VARCHAR",    ADS_VARCHAR,    CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_DOUBLE",     ADS_DOUBLE,     CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_INTEGER",    ADS_INTEGER,    CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_SHORTINT",   ADS_SHORTINT,   CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_TIME",       ADS_TIME,       CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_TIMESTAMP",  ADS_TIMESTAMP,  CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_AUTOINC",    ADS_AUTOINC,    CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_RAW",        ADS_RAW,        CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_CURDOUBLE",  ADS_CURDOUBLE,  CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_MONEY",      ADS_MONEY,      CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_ROWVERSION", ADS_ROWVERSION, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_MODTIME",    ADS_MODTIME,    CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_NCHAR",      ADS_NCHAR,      CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("ADS_NMEMO",      ADS_NMEMO,      CONST_CS | CONST_PERSISTENT);
}

/* -----------------------------------------------------------------------
 * Module info
 * --------------------------------------------------------------------- */
PHP_MINFO_FUNCTION(ads)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "OpenADS extension", "enabled");
    php_info_print_table_row(2, "Extension version", PHP_ADS_VERSION);
    php_info_print_table_row(2, "ACE version", "11.10");
    php_info_print_table_end();
}

/* -----------------------------------------------------------------------
 * MINIT — register all classes
 * --------------------------------------------------------------------- */
PHP_RINIT_FUNCTION(ads)
{
    return SUCCESS;
}

PHP_MINIT_FUNCTION(ads)
{
    /* AdsException extends Exception */
    zend_class_entry ce_exc;
    INIT_CLASS_ENTRY(ce_exc, "AdsException", NULL);
    ads_exception_ce = zend_register_internal_class_ex(&ce_exc, zend_ce_exception);

    ads_connection_register_class();
    ads_statement_register_class();
    ads_table_register_class();
    ads_misc_register_classes();
    ads_prepared_register_class();

    ads_register_constants(module_number);

    return SUCCESS;
}

/* -----------------------------------------------------------------------
 * Module entry
 * --------------------------------------------------------------------- */
static const zend_function_entry ads_functions[] = {
    PHP_FE(ads_dd_create, arginfo_ads_dd_create)
    PHP_FE_END
};

zend_module_entry ads_module_entry = {
    STANDARD_MODULE_HEADER,
    PHP_ADS_EXTNAME,
    ads_functions,
    PHP_MINIT(ads),
    NULL,           /* MSHUTDOWN */
    PHP_RINIT(ads), /* RINIT */
    NULL,           /* RSHUTDOWN */
    PHP_MINFO(ads),
    PHP_ADS_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_OPENADS
ZEND_GET_MODULE(ads)
#endif
