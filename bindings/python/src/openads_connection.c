/*
 * openads_connection.c — AdsConnection Python type for OpenADS.
 */
#include "openads.h"

#define CHECK_CONN(self) \
    do { \
        if ((self)->closed || (self)->hConn == 0) { \
            PyErr_SetString(AdsError, "connection is closed"); \
            return NULL; \
        } \
    } while (0)

extern PyTypeObject AdsTransactionType;
extern PyTypeObject AdsStatementType;
extern PyTypeObject AdsPreparedType;

static PyObject *AdsConnection_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    AdsConnectionObject *self = (AdsConnectionObject *)type->tp_alloc(type, 0);
    if (self) {
        self->hConn  = 0;
        self->closed = 1;
    }
    return (PyObject *)self;
}

static int AdsConnection_init(AdsConnectionObject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"path", "server_type", "user", "password", "options", NULL};

    const char *path      = NULL;
    int         serverType = ADS_REMOTE_SERVER;
    const char *user      = "";
    const char *password  = "";
    int         options   = ADS_CHECKRIGHTS;
    UNSIGNED32  ulRet;
    ADSHANDLE   hConn = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|issi", kwlist,
                                     &path, &serverType, &user, &password, &options))
        return -1;

    ulRet = AdsConnect60(
        (UNSIGNED8 *)path,
        (UNSIGNED16)serverType,
        (UNSIGNED8 *)user,
        (UNSIGNED8 *)password,
        (UNSIGNED32)options,
        &hConn);

    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsConnection");
        return -1;
    }

    self->hConn  = hConn;
    self->closed = 0;
    return 0;
}

static void AdsConnection_dealloc(AdsConnectionObject *self)
{
    if (!self->closed && self->hConn)
        AdsDisconnect(self->hConn);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *AdsConnection_close(AdsConnectionObject *self, PyObject *Py_UNUSED(ignored))
{
    if (!self->closed && self->hConn) {
        AdsDisconnect(self->hConn);
        self->hConn  = 0;
        self->closed = 1;
    }
    Py_RETURN_NONE;
}

static PyObject *AdsConnection_query(AdsConnectionObject *self, PyObject *args)
{
    const char *sql = NULL;
    ADSHANDLE   hStmt = 0, hCursor = 0;
    UNSIGNED16  numFields = 0;
    UNSIGNED32  ulRet;

    CHECK_CONN(self);

    if (!PyArg_ParseTuple(args, "s", &sql))
        return NULL;

    ulRet = AdsCreateSQLStatement(self->hConn, &hStmt);
    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsCreateSQLStatement");
        return NULL;
    }

    ulRet = AdsExecuteSQLDirect(hStmt, (UNSIGNED8 *)sql, &hCursor);
    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsExecuteSQLDirect");
        AdsCloseSQLStatement(hStmt);
        return NULL;
    }

    if (hCursor != 0) {
        AdsGetNumFields(hCursor, &numFields);
    }

    {
        AdsStatementObject *stmt =
            (AdsStatementObject *)AdsStatementType.tp_alloc(&AdsStatementType, 0);
        if (!stmt) {
            AdsCloseSQLStatement(hStmt);
            return NULL;
        }
        stmt->hStmt     = hStmt;
        stmt->hCursor   = hCursor;
        stmt->numFields = numFields;
        stmt->eof       = 0;
        return (PyObject *)stmt;
    }
}

static PyObject *AdsConnection_execute(AdsConnectionObject *self, PyObject *args)
{
    const char *sql = NULL;
    ADSHANDLE   hStmt = 0;
    UNSIGNED32  ulRet;

    CHECK_CONN(self);

    if (!PyArg_ParseTuple(args, "s", &sql))
        return NULL;

    ulRet = AdsCreateSQLStatement(self->hConn, &hStmt);
    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsCreateSQLStatement");
        return NULL;
    }

    {
        ADSHANDLE hCursorExec = 0;
        ulRet = AdsExecuteSQLDirect(hStmt, (UNSIGNED8 *)sql, &hCursorExec);
        if (hCursorExec != 0)
            AdsCloseTable(hCursorExec);
    }
    AdsCloseSQLStatement(hStmt);

    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsExecuteSQLDirect");
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *AdsConnection_prepare(AdsConnectionObject *self, PyObject *args)
{
    const char        *sql = NULL;
    ADSHANDLE          hStmt = 0;
    UNSIGNED32         ulRet;
    AdsPreparedObject *prep;

    CHECK_CONN(self);

    if (!PyArg_ParseTuple(args, "s", &sql))
        return NULL;

    ulRet = AdsCreateSQLStatement(self->hConn, &hStmt);
    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsCreateSQLStatement");
        return NULL;
    }

    ulRet = AdsPrepareSQL(hStmt, (UNSIGNED8 *)sql);
    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsPrepareSQL");
        AdsCloseSQLStatement(hStmt);
        return NULL;
    }

    prep = (AdsPreparedObject *)AdsPreparedType.tp_alloc(&AdsPreparedType, 0);
    if (!prep) {
        AdsCloseSQLStatement(hStmt);
        return NULL;
    }
    prep->hStmt  = hStmt;
    prep->closed = 0;
    return (PyObject *)prep;
}

static PyObject *AdsConnection_begin_transaction(AdsConnectionObject *self,
                                                  PyObject *Py_UNUSED(ignored))
{
    UNSIGNED32           ulRet;
    AdsTransactionObject *tx;

    CHECK_CONN(self);

    ulRet = AdsBeginTransaction(self->hConn);
    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsBeginTransaction");
        return NULL;
    }

    tx = (AdsTransactionObject *)AdsTransactionType.tp_alloc(&AdsTransactionType, 0);
    if (!tx) {
        AdsRollbackTransaction(self->hConn);
        return NULL;
    }
    tx->hConn  = self->hConn;
    tx->active = 1;
    return (PyObject *)tx;
}

static PyObject *AdsConnection_is_alive(AdsConnectionObject *self,
                                         PyObject *Py_UNUSED(ignored))
{
    UNSIGNED16 usConnected = 0;

    if (self->closed || !self->hConn)
        Py_RETURN_FALSE;

    AdsIsConnectionAlive(self->hConn, &usConnected);
    return PyBool_FromLong((long)usConnected);
}

static PyObject *AdsConnection_enter(AdsConnectionObject *self, PyObject *Py_UNUSED(ignored))
{
    CHECK_CONN(self);
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *AdsConnection_exit(AdsConnectionObject *self, PyObject *args)
{
    PyObject *exc_type, *exc_val, *exc_tb;
    if (!PyArg_ParseTuple(args, "OOO", &exc_type, &exc_val, &exc_tb))
        return NULL;
    AdsConnection_close(self, NULL);
    Py_RETURN_FALSE;
}

static PyObject *AdsConnection_repr(AdsConnectionObject *self)
{
    if (self->closed)
        return PyUnicode_FromString("<AdsConnection (closed)>");
    return PyUnicode_FromFormat("<AdsConnection handle=%p>", (void *)self->hConn);
}

static PyMethodDef AdsConnection_methods[] = {
    {"close",             (PyCFunction)AdsConnection_close,             METH_NOARGS,  "Disconnect from the server."},
    {"query",             (PyCFunction)AdsConnection_query,             METH_VARARGS, "Execute SQL query, return AdsStatement."},
    {"execute",           (PyCFunction)AdsConnection_execute,           METH_VARARGS, "Execute non-SELECT SQL."},
    {"prepare",           (PyCFunction)AdsConnection_prepare,           METH_VARARGS, "Prepare SQL statement, return AdsPreparedStatement."},
    {"begin_transaction", (PyCFunction)AdsConnection_begin_transaction, METH_NOARGS,  "Begin a transaction, return AdsTransaction."},
    {"is_alive",          (PyCFunction)AdsConnection_is_alive,          METH_NOARGS,  "Return True if connection is open."},
    {"__enter__",         (PyCFunction)AdsConnection_enter,             METH_NOARGS,  NULL},
    {"__exit__",          (PyCFunction)AdsConnection_exit,              METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}
};

PyTypeObject AdsConnectionType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "openads.AdsConnection",
    .tp_basicsize = sizeof(AdsConnectionObject),
    .tp_itemsize  = 0,
    .tp_dealloc   = (destructor)AdsConnection_dealloc,
    .tp_repr      = (reprfunc)AdsConnection_repr,
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_doc       = "AdsConnection(path, server_type=ADS_REMOTE_SERVER, user='', password='', options=ADS_CHECKRIGHTS)",
    .tp_methods   = AdsConnection_methods,
    .tp_new       = AdsConnection_new,
    .tp_init      = (initproc)AdsConnection_init,
};
