/*
 * openads_statement.c — AdsStatement Python type for OpenADS.
 */
#include "openads.h"

#define CHECK_STMT(self) \
    do { \
        if ((self)->hStmt == 0) { \
            PyErr_SetString(AdsError, "statement is closed"); \
            return NULL; \
        } \
    } while (0)

static PyObject *AdsStatement_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    AdsStatementObject *self = (AdsStatementObject *)type->tp_alloc(type, 0);
    if (self) {
        self->hStmt     = 0;
        self->hCursor   = 0;
        self->numFields = 0;
        self->eof       = 1;
    }
    return (PyObject *)self;
}

static void AdsStatement_dealloc(AdsStatementObject *self)
{
    if (self->hStmt)
        AdsCloseSQLStatement(self->hStmt);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *AdsStatement_close(AdsStatementObject *self, PyObject *Py_UNUSED(ignored))
{
    if (self->hStmt) {
        AdsCloseSQLStatement(self->hStmt);
        self->hStmt   = 0;
        self->hCursor = 0;
    }
    Py_RETURN_NONE;
}

static PyObject *AdsStatement_fetch_assoc(AdsStatementObject *self,
                                           PyObject *Py_UNUSED(ignored))
{
    UNSIGNED32 ulRet;
    UNSIGNED16 bEof = ADS_FALSE;
    PyObject  *row;

    CHECK_STMT(self);

    if (self->eof)
        Py_RETURN_NONE;

    AdsAtEOF(self->hCursor, &bEof);
    if (bEof) {
        self->eof = 1;
        Py_RETURN_NONE;
    }

    row = ads_cursor_row_dict(self->hCursor, self->numFields);
    if (!row)
        return NULL;

    ulRet = AdsSkip(self->hCursor, 1);
    if (ulRet != AE_SUCCESS) {
        self->eof = 1;
    }

    return row;
}

static PyObject *AdsStatement_fetch_row(AdsStatementObject *self,
                                         PyObject *Py_UNUSED(ignored))
{
    UNSIGNED16 bEof = ADS_FALSE;
    UNSIGNED16 i;
    PyObject  *row;
    UNSIGNED32 ulRet;

    CHECK_STMT(self);

    if (self->eof)
        Py_RETURN_NONE;

    AdsAtEOF(self->hCursor, &bEof);
    if (bEof) {
        self->eof = 1;
        Py_RETURN_NONE;
    }

    row = PyList_New(self->numFields);
    if (!row)
        return NULL;

    for (i = 1; i <= self->numFields; i++) {
        char       nameBuf[256];
        UNSIGNED16 nameLen = (UNSIGNED16)(sizeof(nameBuf) - 1);
        PyObject  *val;

        ulRet = AdsGetFieldName(self->hCursor, i, (UNSIGNED8 *)nameBuf, &nameLen);
        if (ulRet != AE_SUCCESS) {
            nameBuf[0] = '\0';
        }
        nameBuf[nameLen] = '\0';

        val = ads_field_to_pyobject(self->hCursor, nameBuf);
        if (!val) {
            Py_DECREF(row);
            return NULL;
        }
        PyList_SET_ITEM(row, i - 1, val);
    }

    AdsSkip(self->hCursor, 1);

    return row;
}

static PyObject *AdsStatement_fetch_all(AdsStatementObject *self,
                                         PyObject *Py_UNUSED(ignored))
{
    PyObject *result = PyList_New(0);
    if (!result)
        return NULL;

    CHECK_STMT(self);

    while (1) {
        UNSIGNED16 bEof = ADS_FALSE;
        PyObject  *row;

        if (self->eof)
            break;

        AdsAtEOF(self->hCursor, &bEof);
        if (bEof) {
            self->eof = 1;
            break;
        }

        row = ads_cursor_row_dict(self->hCursor, self->numFields);
        if (!row) {
            Py_DECREF(result);
            return NULL;
        }

        if (PyList_Append(result, row) < 0) {
            Py_DECREF(row);
            Py_DECREF(result);
            return NULL;
        }
        Py_DECREF(row);
        AdsSkip(self->hCursor, 1);
    }

    return result;
}

static PyObject *AdsStatement_column_count(AdsStatementObject *self,
                                            PyObject *Py_UNUSED(ignored))
{
    CHECK_STMT(self);
    return PyLong_FromLong((long)self->numFields);
}

static PyObject *AdsStatement_row_count(AdsStatementObject *self,
                                         PyObject *Py_UNUSED(ignored))
{
    UNSIGNED32 ulCount = 0;
    CHECK_STMT(self);
    AdsGetRecordCount(self->hCursor, ADS_IGNOREFILTERS, &ulCount);
    return PyLong_FromUnsignedLong((unsigned long)ulCount);
}

static PyObject *AdsStatement_iter(AdsStatementObject *self)
{
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *AdsStatement_iternext(AdsStatementObject *self)
{
    UNSIGNED16 bEof = ADS_FALSE;
    PyObject  *row;

    if (!self->hStmt || self->eof)
        return NULL;

    AdsAtEOF(self->hCursor, &bEof);
    if (bEof) {
        self->eof = 1;
        return NULL;
    }

    row = ads_cursor_row_dict(self->hCursor, self->numFields);
    if (!row)
        return NULL;

    AdsSkip(self->hCursor, 1);
    return row;
}

static PyObject *AdsStatement_enter(AdsStatementObject *self, PyObject *Py_UNUSED(ignored))
{
    CHECK_STMT(self);
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *AdsStatement_exit(AdsStatementObject *self, PyObject *args)
{
    PyObject *exc_type, *exc_val, *exc_tb;
    if (!PyArg_ParseTuple(args, "OOO", &exc_type, &exc_val, &exc_tb))
        return NULL;
    AdsStatement_close(self, NULL);
    Py_RETURN_FALSE;
}

static PyObject *AdsStatement_repr(AdsStatementObject *self)
{
    if (!self->hStmt)
        return PyUnicode_FromString("<AdsStatement (closed)>");
    return PyUnicode_FromFormat("<AdsStatement fields=%d>", (int)self->numFields);
}

static PyMethodDef AdsStatement_methods[] = {
    {"fetch_assoc",   (PyCFunction)AdsStatement_fetch_assoc,   METH_NOARGS,  "Fetch next row as dict, or None."},
    {"fetch_row",     (PyCFunction)AdsStatement_fetch_row,     METH_NOARGS,  "Fetch next row as list, or None."},
    {"fetch_all",     (PyCFunction)AdsStatement_fetch_all,     METH_NOARGS,  "Fetch all remaining rows as list of dicts."},
    {"row_count",     (PyCFunction)AdsStatement_row_count,     METH_NOARGS,  "Return number of records in result set."},
    {"column_count",  (PyCFunction)AdsStatement_column_count,  METH_NOARGS,  "Return number of columns."},
    {"close",         (PyCFunction)AdsStatement_close,         METH_NOARGS,  "Close statement and release handle."},
    {"__enter__",     (PyCFunction)AdsStatement_enter,         METH_NOARGS,  NULL},
    {"__exit__",      (PyCFunction)AdsStatement_exit,          METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}
};

PyTypeObject AdsStatementType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "openads.AdsStatement",
    .tp_basicsize = sizeof(AdsStatementObject),
    .tp_itemsize  = 0,
    .tp_dealloc   = (destructor)AdsStatement_dealloc,
    .tp_repr      = (reprfunc)AdsStatement_repr,
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_doc       = "AdsStatement — result of AdsConnection.query(sql).",
    .tp_iter      = (iternextfunc)AdsStatement_iter,
    .tp_iternext  = (iternextfunc)AdsStatement_iternext,
    .tp_methods   = AdsStatement_methods,
    .tp_new       = AdsStatement_new,
};
