/*
 * openads_prepared.c — AdsPreparedStatement Python type for OpenADS.
 */
#include "openads.h"

#define CHECK_PREP(self) \
    do { \
        if ((self)->closed || (self)->hStmt == 0) { \
            PyErr_SetString(AdsError, "prepared statement is closed"); \
            return NULL; \
        } \
    } while (0)

static const char *prep_param_name(const char *name, char *buf, size_t bufsz)
{
    if (name[0] == ':') {
        snprintf(buf, bufsz, "%s", name + 1);
        return buf;
    }
    return name;
}

static PyObject *AdsPrepared_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    AdsPreparedObject *self = (AdsPreparedObject *)type->tp_alloc(type, 0);
    if (self) {
        self->hStmt  = 0;
        self->closed = 1;
    }
    return (PyObject *)self;
}

static void AdsPrepared_dealloc(AdsPreparedObject *self)
{
    if (!self->closed && self->hStmt)
        AdsCloseSQLStatement(self->hStmt);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *AdsPrepared_close(AdsPreparedObject *self, PyObject *Py_UNUSED(ignored))
{
    if (!self->closed && self->hStmt) {
        AdsCloseSQLStatement(self->hStmt);
        self->hStmt  = 0;
        self->closed = 1;
    }
    Py_RETURN_NONE;
}

static PyObject *AdsPrepared_param_count(AdsPreparedObject *self, PyObject *Py_UNUSED(ignored))
{
    UNSIGNED16 usCount = 0;
    CHECK_PREP(self);
    AdsGetNumParams(self->hStmt, &usCount);
    return PyLong_FromLong((long)usCount);
}

static PyObject *AdsPrepared_bind_string(AdsPreparedObject *self, PyObject *args)
{
    const char *name, *value;
    Py_ssize_t  value_len;
    UNSIGNED32  ulRet;
    char        nbuf[256];
    const char *pname;

    CHECK_PREP(self);
    if (!PyArg_ParseTuple(args, "ss#", &name, &value, &value_len))
        return NULL;

    pname = prep_param_name(name, nbuf, sizeof(nbuf));
    ulRet = AdsSetString(self->hStmt, (UNSIGNED8 *)pname,
                         (UNSIGNED8 *)value, (UNSIGNED32)value_len);
    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsPreparedStatement.bind_string");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *AdsPrepared_bind_int(AdsPreparedObject *self, PyObject *args)
{
    const char *name;
    long        value;
    UNSIGNED32  ulRet;
    char        nbuf[256];
    const char *pname;

    CHECK_PREP(self);
    if (!PyArg_ParseTuple(args, "sl", &name, &value))
        return NULL;

    pname = prep_param_name(name, nbuf, sizeof(nbuf));
    ulRet = AdsSetLongLong(self->hStmt, (UNSIGNED8 *)pname, (SIGNED64)value);
    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsPreparedStatement.bind_int");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *AdsPrepared_bind_double(AdsPreparedObject *self, PyObject *args)
{
    const char *name;
    double      value;
    UNSIGNED32  ulRet;
    char        nbuf[256];
    const char *pname;

    CHECK_PREP(self);
    if (!PyArg_ParseTuple(args, "sd", &name, &value))
        return NULL;

    pname = prep_param_name(name, nbuf, sizeof(nbuf));
    ulRet = AdsSetDouble(self->hStmt, (UNSIGNED8 *)pname, (DOUBLE)value);
    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsPreparedStatement.bind_double");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *AdsPrepared_bind_bool(AdsPreparedObject *self, PyObject *args)
{
    const char *name;
    int         value;
    UNSIGNED32  ulRet;
    char        nbuf[256];
    const char *pname;

    CHECK_PREP(self);
    if (!PyArg_ParseTuple(args, "sp", &name, &value))
        return NULL;

    pname = prep_param_name(name, nbuf, sizeof(nbuf));
    ulRet = AdsSetLogical(self->hStmt, (UNSIGNED8 *)pname, (UNSIGNED16)(value ? 1 : 0));
    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsPreparedStatement.bind_bool");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *AdsPrepared_bind_date(AdsPreparedObject *self, PyObject *args)
{
    const char *name, *value;
    Py_ssize_t  value_len;
    UNSIGNED32  ulRet;
    char        nbuf[256];
    const char *pname;

    CHECK_PREP(self);
    if (!PyArg_ParseTuple(args, "ss#", &name, &value, &value_len))
        return NULL;

    pname = prep_param_name(name, nbuf, sizeof(nbuf));
    ulRet = AdsSetString(self->hStmt, (UNSIGNED8 *)pname,
                         (UNSIGNED8 *)value, (UNSIGNED32)value_len);
    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsPreparedStatement.bind_date");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *AdsPrepared_bind_timestamp(AdsPreparedObject *self, PyObject *args)
{
    const char *name, *value;
    Py_ssize_t  value_len;
    UNSIGNED32  ulRet;
    char        nbuf[256];
    const char *pname;

    CHECK_PREP(self);
    if (!PyArg_ParseTuple(args, "ss#", &name, &value, &value_len))
        return NULL;

    pname = prep_param_name(name, nbuf, sizeof(nbuf));
    ulRet = AdsSetString(self->hStmt, (UNSIGNED8 *)pname,
                         (UNSIGNED8 *)value, (UNSIGNED32)value_len);
    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsPreparedStatement.bind_timestamp");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *AdsPrepared_bind_money(AdsPreparedObject *self, PyObject *args)
{
    const char  *name;
    long long    value;
    UNSIGNED32   ulRet;
    char         nbuf[256];
    const char  *pname;

    CHECK_PREP(self);
    if (!PyArg_ParseTuple(args, "sL", &name, &value))
        return NULL;

    pname = prep_param_name(name, nbuf, sizeof(nbuf));
    ulRet = AdsSetMoney(self->hStmt, (UNSIGNED8 *)pname, (SIGNED64)value);
    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsPreparedStatement.bind_money");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *AdsPrepared_bind_binary(AdsPreparedObject *self, PyObject *args)
{
    const char  *name;
    const char  *data;
    Py_ssize_t   data_len;
    int          btype = ADS_BINARY;
    UNSIGNED32   ulRet;
    char         nbuf[256];
    const char  *pname;

    CHECK_PREP(self);
    if (!PyArg_ParseTuple(args, "sy#|i", &name, &data, &data_len, &btype))
        return NULL;

    pname = prep_param_name(name, nbuf, sizeof(nbuf));
    ulRet = AdsSetBinary(self->hStmt, (UNSIGNED8 *)pname,
                         (UNSIGNED16)btype,
                         (UNSIGNED32)data_len, 0,
                         (UNSIGNED8 *)data, (UNSIGNED32)data_len);
    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsPreparedStatement.bind_binary");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *AdsPrepared_bind_null(AdsPreparedObject *self, PyObject *args)
{
    const char *name;
    UNSIGNED32  ulRet;
    char        nbuf[256];
    const char *pname;

    CHECK_PREP(self);
    if (!PyArg_ParseTuple(args, "s", &name))
        return NULL;

    pname = prep_param_name(name, nbuf, sizeof(nbuf));
    ulRet = AdsSetNull(self->hStmt, (UNSIGNED8 *)pname);
    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsPreparedStatement.bind_null");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *AdsPrepared_bind(AdsPreparedObject *self, PyObject *args)
{
    const char *name;
    PyObject   *value;
    UNSIGNED32  ulRet = AE_SUCCESS;
    char        nbuf[256];
    const char *pname;

    CHECK_PREP(self);
    if (!PyArg_ParseTuple(args, "sO", &name, &value))
        return NULL;

    pname = prep_param_name(name, nbuf, sizeof(nbuf));

    if (value == Py_None) {
        ulRet = AdsSetNull(self->hStmt, (UNSIGNED8 *)pname);
    } else if (PyBool_Check(value)) {
        ulRet = AdsSetLogical(self->hStmt, (UNSIGNED8 *)pname,
                              (UNSIGNED16)(value == Py_True ? 1 : 0));
    } else if (PyLong_Check(value)) {
        ulRet = AdsSetLongLong(self->hStmt, (UNSIGNED8 *)pname,
                               (SIGNED64)PyLong_AsLongLong(value));
    } else if (PyFloat_Check(value)) {
        ulRet = AdsSetDouble(self->hStmt, (UNSIGNED8 *)pname,
                             (DOUBLE)PyFloat_AsDouble(value));
    } else if (PyUnicode_Check(value)) {
        Py_ssize_t  slen;
        const char *sval = PyUnicode_AsUTF8AndSize(value, &slen);
        if (!sval) return NULL;
        ulRet = AdsSetString(self->hStmt, (UNSIGNED8 *)pname,
                             (UNSIGNED8 *)sval, (UNSIGNED32)slen);
    } else if (PyBytes_Check(value)) {
        Py_ssize_t   blen = PyBytes_GET_SIZE(value);
        const char  *bval = PyBytes_AS_STRING(value);
        ulRet = AdsSetBinary(self->hStmt, (UNSIGNED8 *)pname,
                             ADS_BINARY, (UNSIGNED32)blen, 0,
                             (UNSIGNED8 *)bval, (UNSIGNED32)blen);
    } else {
        PyErr_Format(AdsError,
            "AdsPreparedStatement.bind(): unsupported Python type '%s' for parameter '%s'",
            Py_TYPE(value)->tp_name, name);
        return NULL;
    }

    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsPreparedStatement.bind");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *AdsPrepared_execute(AdsPreparedObject *self, PyObject *Py_UNUSED(ignored))
{
    ADSHANDLE  hCursor = 0;
    UNSIGNED32 ulRet;

    CHECK_PREP(self);

    ulRet = AdsExecuteSQL(self->hStmt, &hCursor);
    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsPreparedStatement.execute");
        return NULL;
    }

    if (hCursor == 0) {
        Py_RETURN_TRUE;
    }

    {
        UNSIGNED16         numFields = 0;
        AdsStatementObject *stmt;

        AdsGetNumFields(hCursor, &numFields);

        stmt = (AdsStatementObject *)AdsStatementType.tp_alloc(&AdsStatementType, 0);
        if (!stmt) {
            AdsCloseTable(hCursor);
            return NULL;
        }
        stmt->hStmt     = self->hStmt;
        stmt->hCursor   = hCursor;
        stmt->numFields = numFields;
        stmt->eof       = 0;

        self->hStmt  = 0;
        self->closed = 1;

        return (PyObject *)stmt;
    }
}

static PyObject *AdsPrepared_enter(AdsPreparedObject *self, PyObject *Py_UNUSED(ignored))
{
    CHECK_PREP(self);
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *AdsPrepared_exit(AdsPreparedObject *self, PyObject *args)
{
    PyObject *exc_type, *exc_val, *exc_tb;
    if (!PyArg_ParseTuple(args, "OOO", &exc_type, &exc_val, &exc_tb))
        return NULL;
    AdsPrepared_close(self, NULL);
    Py_RETURN_FALSE;
}

static PyObject *AdsPrepared_repr(AdsPreparedObject *self)
{
    if (self->closed)
        return PyUnicode_FromString("<AdsPreparedStatement (closed)>");
    return PyUnicode_FromFormat("<AdsPreparedStatement handle=%p>", (void *)self->hStmt);
}

static PyMethodDef AdsPrepared_methods[] = {
    {"bind_string",    (PyCFunction)AdsPrepared_bind_string,    METH_VARARGS, "bind_string(name, value)"},
    {"bind_int",       (PyCFunction)AdsPrepared_bind_int,       METH_VARARGS, "bind_int(name, value)"},
    {"bind_double",    (PyCFunction)AdsPrepared_bind_double,    METH_VARARGS, "bind_double(name, value)"},
    {"bind_bool",      (PyCFunction)AdsPrepared_bind_bool,      METH_VARARGS, "bind_bool(name, value)"},
    {"bind_date",      (PyCFunction)AdsPrepared_bind_date,      METH_VARARGS, "bind_date(name, value)"},
    {"bind_timestamp", (PyCFunction)AdsPrepared_bind_timestamp, METH_VARARGS, "bind_timestamp(name, value)"},
    {"bind_money",     (PyCFunction)AdsPrepared_bind_money,     METH_VARARGS, "bind_money(name, value)"},
    {"bind_binary",    (PyCFunction)AdsPrepared_bind_binary,    METH_VARARGS, "bind_binary(name, data, type=ADS_BINARY)"},
    {"bind_null",      (PyCFunction)AdsPrepared_bind_null,      METH_VARARGS, "bind_null(name)"},
    {"bind",           (PyCFunction)AdsPrepared_bind,           METH_VARARGS, "bind(name, value)"},
    {"execute",        (PyCFunction)AdsPrepared_execute,        METH_NOARGS,  "execute() — returns AdsStatement or True."},
    {"param_count",    (PyCFunction)AdsPrepared_param_count,    METH_NOARGS,  "param_count() -> int"},
    {"close",          (PyCFunction)AdsPrepared_close,          METH_NOARGS,  "close()"},
    {"__enter__",      (PyCFunction)AdsPrepared_enter,          METH_NOARGS,  NULL},
    {"__exit__",       (PyCFunction)AdsPrepared_exit,           METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}
};

PyTypeObject AdsPreparedType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "openads.AdsPreparedStatement",
    .tp_basicsize = sizeof(AdsPreparedObject),
    .tp_itemsize  = 0,
    .tp_dealloc   = (destructor)AdsPrepared_dealloc,
    .tp_repr      = (reprfunc)AdsPrepared_repr,
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_doc       = "AdsPreparedStatement — result of AdsConnection.prepare(sql).",
    .tp_methods   = AdsPrepared_methods,
    .tp_new       = AdsPrepared_new,
};
