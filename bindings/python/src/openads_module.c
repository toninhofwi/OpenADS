/*
 * openads_module.c — Python module entry point for the OpenADS extension.
 */
#include "openads.h"

PyObject *AdsError = NULL;

void ads_set_error(UNSIGNED32 code, const char *ctx)
{
    char       errBuf[4096];
    UNSIGNED16 errLen = (UNSIGNED16)(sizeof(errBuf) - 1);
    char       lastBuf[1024];
    UNSIGNED16 lastLen = (UNSIGNED16)(sizeof(lastBuf) - 1);
    UNSIGNED32 lastCode = 0;
    char       msg[8192];

    errBuf[0]  = '\0';
    lastBuf[0] = '\0';

    AdsGetErrorString(code, (UNSIGNED8 *)errBuf, &errLen);
    errBuf[errLen] = '\0';

    AdsGetLastError(&lastCode, (UNSIGNED8 *)lastBuf, &lastLen);
    lastBuf[lastLen] = '\0';

    if (ctx && ctx[0]) {
        if (lastBuf[0] && lastCode != 0 && lastCode != code)
            snprintf(msg, sizeof(msg), "%s: [%lu] %s | last=[%lu] %s",
                     ctx, (unsigned long)code, errBuf,
                     (unsigned long)lastCode, lastBuf);
        else if (lastBuf[0])
            snprintf(msg, sizeof(msg), "%s: [%lu] %s | %s",
                     ctx, (unsigned long)code, errBuf, lastBuf);
        else
            snprintf(msg, sizeof(msg), "%s: [%lu] %s", ctx, (unsigned long)code, errBuf);
    } else {
        if (lastBuf[0])
            snprintf(msg, sizeof(msg), "[%lu] %s | %s",
                     (unsigned long)code, errBuf, lastBuf);
        else
            snprintf(msg, sizeof(msg), "[%lu] %s", (unsigned long)code, errBuf);
    }

    PyErr_SetString(AdsError, msg);
}

PyObject *ads_field_to_pyobject(ADSHANDLE hCursor, const char *fieldName)
{
    UNSIGNED16 usType = 0;
    UNSIGNED32 ulRet;

    ulRet = AdsGetFieldType(hCursor, (UNSIGNED8 *)fieldName, &usType);
    if (ulRet != AE_SUCCESS) {
        Py_RETURN_NONE;
    }

    switch (usType) {
        case ADS_LOGICAL: {
            UNSIGNED16 bVal = 0;
            ulRet = AdsGetLogical(hCursor, (UNSIGNED8 *)fieldName, &bVal);
            if (ulRet == AE_SUCCESS)
                return PyBool_FromLong(bVal);
            Py_RETURN_NONE;
        }

        case ADS_INTEGER:
        case ADS_AUTOINC:
        case ADS_SHORTINT: {
            SIGNED32 lVal = 0;
            ulRet = AdsGetLong(hCursor, (UNSIGNED8 *)fieldName, &lVal);
            if (ulRet == AE_SUCCESS)
                return PyLong_FromLong((long)lVal);
            Py_RETURN_NONE;
        }

        case ADS_DOUBLE:
        case ADS_CURDOUBLE:
        case ADS_MONEY:
        case ADS_NUMERIC: {
            DOUBLE dVal = 0.0;
            ulRet = AdsGetDouble(hCursor, (UNSIGNED8 *)fieldName, &dVal);
            if (ulRet == AE_SUCCESS)
                return PyFloat_FromDouble(dVal);
            Py_RETURN_NONE;
        }

        case ADS_MEMO:
        case ADS_NMEMO: {
            UNSIGNED32 ulMemoLen = 0;
            UNSIGNED32 ulBufSize;
            char      *buf;
            PyObject  *result;

            AdsGetMemoLength(hCursor, (UNSIGNED8 *)fieldName, &ulMemoLen);
            ulBufSize = (ulMemoLen > 0) ? ulMemoLen + 4 : 65536;

            buf = (char *)PyMem_Malloc(ulBufSize);
            if (!buf)
                return PyErr_NoMemory();

            ulRet = AdsGetString(hCursor, (UNSIGNED8 *)fieldName,
                                 (UNSIGNED8 *)buf, &ulBufSize, ADS_TRIM);
            if (ulRet == AE_SUCCESS) {
                result = PyUnicode_DecodeUTF8(buf, (Py_ssize_t)ulBufSize, "replace");
            } else {
                result = Py_None;
                Py_INCREF(result);
            }
            PyMem_Free(buf);
            return result;
        }

        case ADS_BINARY:
        case ADS_IMAGE: {
            UNSIGNED32 ulBinLen = 0;
            UNSIGNED32 ulBufSize;
            char      *buf;
            PyObject  *result;

            AdsGetBinaryLength(hCursor, (UNSIGNED8 *)fieldName, &ulBinLen);
            if (ulBinLen == 0)
                return PyBytes_FromStringAndSize("", 0);

            ulBufSize = ulBinLen;
            buf = (char *)PyMem_Malloc(ulBufSize);
            if (!buf)
                return PyErr_NoMemory();

            ulRet = AdsGetBinary(hCursor, (UNSIGNED8 *)fieldName,
                                 0, (UNSIGNED8 *)buf, &ulBufSize);
            if (ulRet == AE_SUCCESS) {
                result = PyBytes_FromStringAndSize(buf, (Py_ssize_t)ulBufSize);
            } else {
                result = Py_None;
                Py_INCREF(result);
            }
            PyMem_Free(buf);
            return result;
        }

        default: {
            UNSIGNED32 ulFieldLen = 0;
            UNSIGNED32 ulBufSize;
            char      *buf;
            PyObject  *result;

            AdsGetFieldLength(hCursor, (UNSIGNED8 *)fieldName, &ulFieldLen);
            ulBufSize = (ulFieldLen > 1) ? ulFieldLen + 4 : 65536;

            buf = (char *)PyMem_Malloc(ulBufSize);
            if (!buf)
                return PyErr_NoMemory();

            ulRet = AdsGetString(hCursor, (UNSIGNED8 *)fieldName,
                                 (UNSIGNED8 *)buf, &ulBufSize, ADS_TRIM);
            if (ulRet == AE_SUCCESS) {
                result = PyUnicode_DecodeUTF8(buf, (Py_ssize_t)ulBufSize, "replace");
            } else {
                result = Py_None;
                Py_INCREF(result);
            }
            PyMem_Free(buf);
            return result;
        }
    }
}

PyObject *ads_cursor_row_dict(ADSHANDLE hCursor, UNSIGNED16 numFields)
{
    PyObject  *dict = PyDict_New();
    UNSIGNED16 i;

    if (!dict)
        return NULL;

    for (i = 1; i <= numFields; i++) {
        char       nameBuf[256];
        UNSIGNED16 nameLen = (UNSIGNED16)(sizeof(nameBuf) - 1);
        UNSIGNED32 ulRet;
        PyObject  *val;
        PyObject  *key;

        ulRet = AdsGetFieldName(hCursor, i, (UNSIGNED8 *)nameBuf, &nameLen);
        if (ulRet != AE_SUCCESS)
            continue;
        nameBuf[nameLen] = '\0';

        val = ads_field_to_pyobject(hCursor, nameBuf);
        if (!val) {
            Py_DECREF(dict);
            return NULL;
        }

        key = PyUnicode_FromString(nameBuf);
        if (!key) {
            Py_DECREF(val);
            Py_DECREF(dict);
            return NULL;
        }

        PyDict_SetItem(dict, key, val);
        Py_DECREF(key);
        Py_DECREF(val);
    }

    return dict;
}

static PyMethodDef openads_methods[] = {
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef openads_moduledef = {
    PyModuleDef_HEAD_INIT,
    "openads",
    "OpenADS database engine extension for Python.",
    -1,
    openads_methods,
    NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC PyInit_openads(void)
{
    PyObject *m;

    if (PyType_Ready(&AdsConnectionType)  < 0) return NULL;
    if (PyType_Ready(&AdsStatementType)   < 0) return NULL;
    if (PyType_Ready(&AdsTableType)       < 0) return NULL;
    if (PyType_Ready(&AdsTransactionType) < 0) return NULL;
    if (PyType_Ready(&AdsDictionaryType)  < 0) return NULL;
    if (PyType_Ready(&AdsPreparedType)    < 0) return NULL;

    m = PyModule_Create(&openads_moduledef);
    if (!m)
        return NULL;

    AdsError = PyErr_NewExceptionWithDoc(
        "openads.AdsError",
        "Exception raised for OpenADS errors.",
        NULL, NULL);
    if (!AdsError) { Py_DECREF(m); return NULL; }
    Py_INCREF(AdsError);
    PyModule_AddObject(m, "AdsError", AdsError);

    Py_INCREF(&AdsConnectionType);
    PyModule_AddObject(m, "AdsConnection",  (PyObject *)&AdsConnectionType);
    Py_INCREF(&AdsStatementType);
    PyModule_AddObject(m, "AdsStatement",   (PyObject *)&AdsStatementType);
    Py_INCREF(&AdsTableType);
    PyModule_AddObject(m, "AdsTable",       (PyObject *)&AdsTableType);
    Py_INCREF(&AdsTransactionType);
    PyModule_AddObject(m, "AdsTransaction", (PyObject *)&AdsTransactionType);
    Py_INCREF(&AdsDictionaryType);
    PyModule_AddObject(m, "AdsDictionary",  (PyObject *)&AdsDictionaryType);
    Py_INCREF(&AdsPreparedType);
    PyModule_AddObject(m, "AdsPreparedStatement", (PyObject *)&AdsPreparedType);

    PyModule_AddIntConstant(m, "ADS_LOCAL_SERVER",        ADS_LOCAL_SERVER);
    PyModule_AddIntConstant(m, "ADS_REMOTE_SERVER",       ADS_REMOTE_SERVER);

    PyModule_AddIntConstant(m, "ADS_NTX",  ADS_NTX);
    PyModule_AddIntConstant(m, "ADS_CDX",  ADS_CDX);
    PyModule_AddIntConstant(m, "ADS_ADT",  ADS_ADT);
    PyModule_AddIntConstant(m, "ADS_VFP",  ADS_VFP);

    PyModule_AddIntConstant(m, "ADS_ANSI", ADS_ANSI);
    PyModule_AddIntConstant(m, "ADS_OEM",  ADS_OEM);

    PyModule_AddIntConstant(m, "ADS_EXCLUSIVE", ADS_EXCLUSIVE);
    PyModule_AddIntConstant(m, "ADS_SHARED",    ADS_SHARED);

    PyModule_AddIntConstant(m, "ADS_COMPATIBLE_LOCKING",  ADS_COMPATIBLE_LOCKING);
    PyModule_AddIntConstant(m, "ADS_PROPRIETARY_LOCKING", ADS_PROPRIETARY_LOCKING);

    PyModule_AddIntConstant(m, "ADS_CHECKRIGHTS",   ADS_CHECKRIGHTS);
    PyModule_AddIntConstant(m, "ADS_IGNORERIGHTS",  ADS_IGNORERIGHTS);

    PyModule_AddIntConstant(m, "ADS_RESPECTFILTERS", ADS_RESPECTFILTERS);
    PyModule_AddIntConstant(m, "ADS_IGNOREFILTERS",  ADS_IGNOREFILTERS);

    PyModule_AddIntConstant(m, "ADS_TRIM",  ADS_TRIM);
    PyModule_AddIntConstant(m, "ADS_LTRIM", ADS_LTRIM);
    PyModule_AddIntConstant(m, "ADS_RTRIM", ADS_RTRIM);

    PyModule_AddIntConstant(m, "ADS_LOGICAL",    ADS_LOGICAL);
    PyModule_AddIntConstant(m, "ADS_NUMERIC",    ADS_NUMERIC);
    PyModule_AddIntConstant(m, "ADS_DATE",       ADS_DATE);
    PyModule_AddIntConstant(m, "ADS_STRING",     ADS_STRING);
    PyModule_AddIntConstant(m, "ADS_MEMO",       ADS_MEMO);
    PyModule_AddIntConstant(m, "ADS_BINARY",     ADS_BINARY);
    PyModule_AddIntConstant(m, "ADS_IMAGE",      ADS_IMAGE);
    PyModule_AddIntConstant(m, "ADS_VARCHAR",    ADS_VARCHAR);
    PyModule_AddIntConstant(m, "ADS_DOUBLE",     ADS_DOUBLE);
    PyModule_AddIntConstant(m, "ADS_INTEGER",    ADS_INTEGER);
    PyModule_AddIntConstant(m, "ADS_SHORTINT",   ADS_SHORTINT);
    PyModule_AddIntConstant(m, "ADS_TIME",       ADS_TIME);
    PyModule_AddIntConstant(m, "ADS_TIMESTAMP",  ADS_TIMESTAMP);
    PyModule_AddIntConstant(m, "ADS_AUTOINC",    ADS_AUTOINC);
    PyModule_AddIntConstant(m, "ADS_RAW",        ADS_RAW);
    PyModule_AddIntConstant(m, "ADS_CURDOUBLE",  ADS_CURDOUBLE);
    PyModule_AddIntConstant(m, "ADS_MONEY",      ADS_MONEY);
    PyModule_AddIntConstant(m, "ADS_ROWVERSION", ADS_ROWVERSION);
    PyModule_AddIntConstant(m, "ADS_MODTIME",    ADS_MODTIME);
    PyModule_AddIntConstant(m, "ADS_NCHAR",      ADS_NCHAR);
    PyModule_AddIntConstant(m, "ADS_NMEMO",      ADS_NMEMO);

    return m;
}
