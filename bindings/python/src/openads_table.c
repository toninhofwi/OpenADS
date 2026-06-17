/*
 * openads_table.c — AdsTable Python type for OpenADS.
 */
#include "openads.h"

extern PyTypeObject AdsConnectionType;

#define CHECK_TABLE(self) \
    do { \
        if ((self)->closed || (self)->hTable == 0) { \
            PyErr_SetString(AdsError, "table is closed"); \
            return NULL; \
        } \
    } while (0)

static PyObject *AdsTable_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    AdsTableObject *self = (AdsTableObject *)type->tp_alloc(type, 0);
    if (self) {
        self->hTable = 0;
        self->closed = 1;
    }
    return (PyObject *)self;
}

static void AdsTable_dealloc(AdsTableObject *self)
{
    if (!self->closed && self->hTable)
        AdsCloseTable(self->hTable);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *AdsTable_open(PyObject *cls, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"path", "connection", "table_type", "lock_type",
                             "char_set", "open_mode", NULL};

    const char *path      = NULL;
    PyObject   *connObj   = Py_None;
    int         tableType = ADS_ADT;
    int         lockType  = ADS_COMPATIBLE_LOCKING;
    int         charSet   = ADS_ANSI;
    int         openMode  = ADS_SHARED;
    ADSHANDLE   hConn     = 0;
    ADSHANDLE   hTable    = 0;
    UNSIGNED32  ulRet;
    AdsTableObject *self;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|Oiiii", kwlist,
                                     &path, &connObj, &tableType, &lockType,
                                     &charSet, &openMode))
        return NULL;

    if (connObj != Py_None) {
        if (!PyObject_IsInstance(connObj, (PyObject *)&AdsConnectionType)) {
            PyErr_SetString(PyExc_TypeError, "connection must be AdsConnection or None");
            return NULL;
        }
        AdsConnectionObject *conn = (AdsConnectionObject *)connObj;
        if (conn->closed || !conn->hConn) {
            PyErr_SetString(AdsError, "connection is closed");
            return NULL;
        }
        hConn = conn->hConn;
    }

    ulRet = AdsOpenTable(
        hConn,
        (UNSIGNED8 *)path,
        NULL,
        (UNSIGNED16)tableType,
        (UNSIGNED16)charSet,
        (UNSIGNED16)lockType,
        ADS_IGNORERIGHTS,
        (UNSIGNED32)openMode,
        &hTable);

    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsTable.open");
        return NULL;
    }

    self = (AdsTableObject *)((PyTypeObject *)cls)->tp_alloc((PyTypeObject *)cls, 0);
    if (!self) {
        AdsCloseTable(hTable);
        return NULL;
    }
    self->hTable = hTable;
    self->closed = 0;
    return (PyObject *)self;
}

static PyObject *AdsTable_close(AdsTableObject *self, PyObject *Py_UNUSED(ignored))
{
    if (!self->closed && self->hTable) {
        AdsCloseTable(self->hTable);
        self->hTable = 0;
        self->closed = 1;
    }
    Py_RETURN_NONE;
}

static PyObject *AdsTable_goto_top(AdsTableObject *self, PyObject *Py_UNUSED(ignored))
{
    UNSIGNED32 ulRet;
    CHECK_TABLE(self);
    ulRet = AdsGotoTop(self->hTable);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "goto_top"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsTable_goto_bottom(AdsTableObject *self, PyObject *Py_UNUSED(ignored))
{
    UNSIGNED32 ulRet;
    CHECK_TABLE(self);
    ulRet = AdsGotoBottom(self->hTable);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "goto_bottom"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsTable_goto_record(AdsTableObject *self, PyObject *args)
{
    unsigned long recNum = 0;
    UNSIGNED32    ulRet;
    CHECK_TABLE(self);
    if (!PyArg_ParseTuple(args, "k", &recNum))
        return NULL;
    ulRet = AdsGotoRecord(self->hTable, (UNSIGNED32)recNum);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "goto_record"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsTable_skip(AdsTableObject *self, PyObject *args)
{
    long       n = 1;
    UNSIGNED32 ulRet;
    CHECK_TABLE(self);
    if (!PyArg_ParseTuple(args, "|l", &n))
        return NULL;
    ulRet = AdsSkip(self->hTable, (SIGNED32)n);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "skip"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsTable_at_eof(AdsTableObject *self, PyObject *Py_UNUSED(ignored))
{
    UNSIGNED16 bEof = ADS_FALSE;
    CHECK_TABLE(self);
    AdsAtEOF(self->hTable, &bEof);
    return PyBool_FromLong(bEof);
}

static PyObject *AdsTable_at_bof(AdsTableObject *self, PyObject *Py_UNUSED(ignored))
{
    UNSIGNED16 bBof = ADS_FALSE;
    CHECK_TABLE(self);
    AdsAtBOF(self->hTable, &bBof);
    return PyBool_FromLong(bBof);
}

static PyObject *AdsTable_get_string(AdsTableObject *self, PyObject *args)
{
    const char *fieldName;
    UNSIGNED32  ulBufSize = 65536;
    UNSIGNED32  ulRet;
    char       *buf;
    PyObject   *result;

    CHECK_TABLE(self);
    if (!PyArg_ParseTuple(args, "s", &fieldName))
        return NULL;

    buf = (char *)PyMem_Malloc(ulBufSize);
    if (!buf) return PyErr_NoMemory();

    ulRet = AdsGetString(self->hTable, (UNSIGNED8 *)fieldName,
                         (UNSIGNED8 *)buf, &ulBufSize, ADS_TRIM);
    if (ulRet == AE_SUCCESS) {
        result = PyUnicode_DecodeUTF8(buf, (Py_ssize_t)ulBufSize, "replace");
    } else {
        ads_set_error(ulRet, "get_string");
        result = NULL;
    }
    PyMem_Free(buf);
    return result;
}

static PyObject *AdsTable_get_long(AdsTableObject *self, PyObject *args)
{
    const char *fieldName;
    SIGNED32    lVal = 0;
    UNSIGNED32  ulRet;
    CHECK_TABLE(self);
    if (!PyArg_ParseTuple(args, "s", &fieldName))
        return NULL;
    ulRet = AdsGetLong(self->hTable, (UNSIGNED8 *)fieldName, &lVal);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "get_long"); return NULL; }
    return PyLong_FromLong((long)lVal);
}

static PyObject *AdsTable_get_double(AdsTableObject *self, PyObject *args)
{
    const char *fieldName;
    DOUBLE      dVal = 0.0;
    UNSIGNED32  ulRet;
    CHECK_TABLE(self);
    if (!PyArg_ParseTuple(args, "s", &fieldName))
        return NULL;
    ulRet = AdsGetDouble(self->hTable, (UNSIGNED8 *)fieldName, &dVal);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "get_double"); return NULL; }
    return PyFloat_FromDouble(dVal);
}

static PyObject *AdsTable_get_logical(AdsTableObject *self, PyObject *args)
{
    const char *fieldName;
    UNSIGNED16  bVal = 0;
    UNSIGNED32  ulRet;
    CHECK_TABLE(self);
    if (!PyArg_ParseTuple(args, "s", &fieldName))
        return NULL;
    ulRet = AdsGetLogical(self->hTable, (UNSIGNED8 *)fieldName, &bVal);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "get_logical"); return NULL; }
    return PyBool_FromLong(bVal);
}

static PyObject *AdsTable_set_string(AdsTableObject *self, PyObject *args)
{
    const char *fieldName, *val;
    UNSIGNED32  ulRet;
    CHECK_TABLE(self);
    if (!PyArg_ParseTuple(args, "ss", &fieldName, &val))
        return NULL;
    ulRet = AdsSetString(self->hTable, (UNSIGNED8 *)fieldName,
                         (UNSIGNED8 *)val, (UNSIGNED16)strlen(val));
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "set_string"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsTable_set_long(AdsTableObject *self, PyObject *args)
{
    const char *fieldName;
    long        val;
    UNSIGNED32  ulRet;
    CHECK_TABLE(self);
    if (!PyArg_ParseTuple(args, "sl", &fieldName, &val))
        return NULL;
    ulRet = AdsSetLongLong(self->hTable, (UNSIGNED8 *)fieldName, (SIGNED64)val);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "set_long"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsTable_set_double(AdsTableObject *self, PyObject *args)
{
    const char *fieldName;
    double      val;
    UNSIGNED32  ulRet;
    CHECK_TABLE(self);
    if (!PyArg_ParseTuple(args, "sd", &fieldName, &val))
        return NULL;
    ulRet = AdsSetDouble(self->hTable, (UNSIGNED8 *)fieldName, (DOUBLE)val);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "set_double"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsTable_set_logical(AdsTableObject *self, PyObject *args)
{
    const char *fieldName;
    int         val;
    UNSIGNED32  ulRet;
    CHECK_TABLE(self);
    if (!PyArg_ParseTuple(args, "sp", &fieldName, &val))
        return NULL;
    ulRet = AdsSetLogical(self->hTable, (UNSIGNED8 *)fieldName, (UNSIGNED16)val);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "set_logical"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsTable_get_record(AdsTableObject *self, PyObject *Py_UNUSED(ignored))
{
    UNSIGNED16 numFields = 0;
    CHECK_TABLE(self);
    AdsGetNumFields(self->hTable, &numFields);
    return ads_cursor_row_dict(self->hTable, numFields);
}

static PyObject *AdsTable_record_count(AdsTableObject *self, PyObject *Py_UNUSED(ignored))
{
    UNSIGNED32 ulCount = 0;
    CHECK_TABLE(self);
    AdsGetRecordCount(self->hTable, ADS_IGNOREFILTERS, &ulCount);
    return PyLong_FromUnsignedLong((unsigned long)ulCount);
}

static PyObject *AdsTable_record_num(AdsTableObject *self, PyObject *Py_UNUSED(ignored))
{
    UNSIGNED32 ulNum = 0;
    CHECK_TABLE(self);
    AdsGetRecordNum(self->hTable, ADS_IGNOREFILTERS, &ulNum);
    return PyLong_FromUnsignedLong((unsigned long)ulNum);
}

static PyObject *AdsTable_append_record(AdsTableObject *self, PyObject *Py_UNUSED(ignored))
{
    UNSIGNED32 ulRet;
    CHECK_TABLE(self);
    ulRet = AdsAppendRecord(self->hTable);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "append_record"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsTable_delete_record(AdsTableObject *self, PyObject *Py_UNUSED(ignored))
{
    UNSIGNED32 ulRet;
    CHECK_TABLE(self);
    ulRet = AdsDeleteRecord(self->hTable);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "delete_record"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsTable_write_record(AdsTableObject *self, PyObject *Py_UNUSED(ignored))
{
    UNSIGNED32 ulRet;
    CHECK_TABLE(self);
    ulRet = AdsWriteRecord(self->hTable);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "write_record"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsTable_cancel_update(AdsTableObject *self, PyObject *Py_UNUSED(ignored))
{
    UNSIGNED32 ulRet;
    CHECK_TABLE(self);
    ulRet = AdsCancelUpdate(self->hTable);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "cancel_update"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsTable_enter(AdsTableObject *self, PyObject *Py_UNUSED(ignored))
{
    CHECK_TABLE(self);
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *AdsTable_exit(AdsTableObject *self, PyObject *args)
{
    PyObject *exc_type, *exc_val, *exc_tb;
    if (!PyArg_ParseTuple(args, "OOO", &exc_type, &exc_val, &exc_tb))
        return NULL;
    AdsTable_close(self, NULL);
    Py_RETURN_FALSE;
}

static PyObject *AdsTable_repr(AdsTableObject *self)
{
    if (self->closed)
        return PyUnicode_FromString("<AdsTable (closed)>");
    return PyUnicode_FromFormat("<AdsTable handle=%p>", (void *)self->hTable);
}

static PyMethodDef AdsTable_methods[] = {
    {"open",          (PyCFunction)AdsTable_open,          METH_CLASS|METH_VARARGS|METH_KEYWORDS,
                                                           "open(path, connection=None, table_type=ADS_ADT, ...) -> AdsTable"},
    {"close",         (PyCFunction)AdsTable_close,         METH_NOARGS,  "Close the table."},
    {"goto_top",      (PyCFunction)AdsTable_goto_top,      METH_NOARGS,  "Move to first record."},
    {"goto_bottom",   (PyCFunction)AdsTable_goto_bottom,   METH_NOARGS,  "Move to last record."},
    {"goto_record",   (PyCFunction)AdsTable_goto_record,   METH_VARARGS, "goto_record(n) — move to record number n."},
    {"skip",          (PyCFunction)AdsTable_skip,          METH_VARARGS, "skip(n=1) — skip n records."},
    {"at_eof",        (PyCFunction)AdsTable_at_eof,        METH_NOARGS,  "Return True if at end-of-file."},
    {"at_bof",        (PyCFunction)AdsTable_at_bof,        METH_NOARGS,  "Return True if at beginning-of-file."},
    {"get_string",    (PyCFunction)AdsTable_get_string,    METH_VARARGS, "get_string(field) -> str"},
    {"get_long",      (PyCFunction)AdsTable_get_long,      METH_VARARGS, "get_long(field) -> int"},
    {"get_double",    (PyCFunction)AdsTable_get_double,    METH_VARARGS, "get_double(field) -> float"},
    {"get_logical",   (PyCFunction)AdsTable_get_logical,   METH_VARARGS, "get_logical(field) -> bool"},
    {"set_string",    (PyCFunction)AdsTable_set_string,    METH_VARARGS, "set_string(field, value)"},
    {"set_long",      (PyCFunction)AdsTable_set_long,      METH_VARARGS, "set_long(field, value)"},
    {"set_double",    (PyCFunction)AdsTable_set_double,    METH_VARARGS, "set_double(field, value)"},
    {"set_logical",   (PyCFunction)AdsTable_set_logical,   METH_VARARGS, "set_logical(field, value)"},
    {"get_record",    (PyCFunction)AdsTable_get_record,    METH_NOARGS,  "get_record() -> dict of all fields."},
    {"record_count",  (PyCFunction)AdsTable_record_count,  METH_NOARGS,  "Return total record count."},
    {"record_num",    (PyCFunction)AdsTable_record_num,    METH_NOARGS,  "Return current record number."},
    {"append_record", (PyCFunction)AdsTable_append_record, METH_NOARGS,  "Append a blank record."},
    {"delete_record", (PyCFunction)AdsTable_delete_record, METH_NOARGS,  "Delete current record."},
    {"write_record",  (PyCFunction)AdsTable_write_record,  METH_NOARGS,  "Flush pending changes to current record."},
    {"cancel_update", (PyCFunction)AdsTable_cancel_update, METH_NOARGS,  "Cancel pending changes."},
    {"__enter__",     (PyCFunction)AdsTable_enter,         METH_NOARGS,  NULL},
    {"__exit__",      (PyCFunction)AdsTable_exit,          METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}
};

PyTypeObject AdsTableType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "openads.AdsTable",
    .tp_basicsize = sizeof(AdsTableObject),
    .tp_itemsize  = 0,
    .tp_dealloc   = (destructor)AdsTable_dealloc,
    .tp_repr      = (reprfunc)AdsTable_repr,
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_doc       = "AdsTable — direct table access (not SQL).",
    .tp_methods   = AdsTable_methods,
    .tp_new       = AdsTable_new,
};
