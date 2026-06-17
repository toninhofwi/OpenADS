/*
 * openads_misc.c — AdsTransaction and AdsDictionary Python types for OpenADS.
 *
 * Function name differences vs SAP ACE (python_ads):
 *   AdsDDAddView          → AdsDDCreateView
 *   AdsDDRemoveView       → AdsDDDropView
 *   AdsDDAddProcedure     → AdsDDCreateProcedure
 *   AdsDDRemoveProcedure  → AdsDDDropProcedure
 *   AdsDDGetProcedureProperty → AdsDDGetProcProperty
 *   AdsDDSetProcedureProperty → AdsDDSetProcProperty
 *   AdsDDRemoveTrigger    → AdsDDDropTrigger
 *   AdsDDGetPermissions   → AdsDDGetUserTableRights(hDict, table, user, &rights)
 *   revoke+grant pair     → AdsDDSetUserTableRights(hDict, table, user, rights)
 *   AdsDDSetTableProperty: 5 args (no validateOption/failTable)
 *   AdsDDSetFieldProperty: 6 args (no validateOption/failTable)
 *   AdsDDCreateTrigger:    8 args (no eventTypes, containerType, comment, options)
 *   AdsDDSetTriggerProperty: supported (SAP had none)
 */
#include "openads.h"

/* ======================================================================
 * AdsTransaction
 * ====================================================================== */

#define CHECK_TX(self) \
    do { \
        if (!(self)->active || (self)->hConn == 0) { \
            PyErr_SetString(AdsError, "transaction is not active"); \
            return NULL; \
        } \
    } while (0)

static PyObject *AdsTransaction_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    AdsTransactionObject *self = (AdsTransactionObject *)type->tp_alloc(type, 0);
    if (self) {
        self->hConn  = 0;
        self->active = 0;
    }
    return (PyObject *)self;
}

static void AdsTransaction_dealloc(AdsTransactionObject *self)
{
    if (self->active && self->hConn)
        AdsRollbackTransaction(self->hConn);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *AdsTransaction_commit(AdsTransactionObject *self,
                                        PyObject *Py_UNUSED(ignored))
{
    UNSIGNED32 ulRet;
    CHECK_TX(self);
    ulRet = AdsCommitTransaction(self->hConn);
    self->active = 0;
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "commit"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsTransaction_rollback(AdsTransactionObject *self,
                                          PyObject *Py_UNUSED(ignored))
{
    UNSIGNED32 ulRet;
    CHECK_TX(self);
    ulRet = AdsRollbackTransaction(self->hConn);
    self->active = 0;
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "rollback"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsTransaction_is_active(AdsTransactionObject *self,
                                           PyObject *Py_UNUSED(ignored))
{
    return PyBool_FromLong(self->active);
}

static PyObject *AdsTransaction_enter(AdsTransactionObject *self,
                                       PyObject *Py_UNUSED(ignored))
{
    CHECK_TX(self);
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *AdsTransaction_exit(AdsTransactionObject *self, PyObject *args)
{
    PyObject *exc_type, *exc_val, *exc_tb;
    if (!PyArg_ParseTuple(args, "OOO", &exc_type, &exc_val, &exc_tb))
        return NULL;

    if (self->active && self->hConn) {
        if (exc_type != Py_None) {
            AdsRollbackTransaction(self->hConn);
        } else {
            UNSIGNED32 ulRet = AdsCommitTransaction(self->hConn);
            if (ulRet != AE_SUCCESS) {
                ads_set_error(ulRet, "transaction commit on exit");
                self->active = 0;
                return NULL;
            }
        }
        self->active = 0;
    }

    Py_RETURN_FALSE;
}

static PyObject *AdsTransaction_repr(AdsTransactionObject *self)
{
    return PyUnicode_FromFormat("<AdsTransaction active=%s>",
                                self->active ? "True" : "False");
}

static PyMethodDef AdsTransaction_methods[] = {
    {"commit",    (PyCFunction)AdsTransaction_commit,    METH_NOARGS,  "Commit the transaction."},
    {"rollback",  (PyCFunction)AdsTransaction_rollback,  METH_NOARGS,  "Roll back the transaction."},
    {"is_active", (PyCFunction)AdsTransaction_is_active, METH_NOARGS,  "Return True if transaction is still active."},
    {"__enter__", (PyCFunction)AdsTransaction_enter,     METH_NOARGS,  NULL},
    {"__exit__",  (PyCFunction)AdsTransaction_exit,      METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}
};

PyTypeObject AdsTransactionType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "openads.AdsTransaction",
    .tp_basicsize = sizeof(AdsTransactionObject),
    .tp_itemsize  = 0,
    .tp_dealloc   = (destructor)AdsTransaction_dealloc,
    .tp_repr      = (reprfunc)AdsTransaction_repr,
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_doc       = "AdsTransaction — returned by AdsConnection.begin_transaction().",
    .tp_methods   = AdsTransaction_methods,
    .tp_new       = AdsTransaction_new,
};

/* ======================================================================
 * AdsDictionary
 * ====================================================================== */

#define CHECK_DICT(self) \
    do { \
        if ((self)->hDict == 0) { \
            PyErr_SetString(AdsError, "dictionary is closed"); \
            return NULL; \
        } \
    } while (0)

static PyObject *AdsDictionary_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    AdsDictionaryObject *self = (AdsDictionaryObject *)type->tp_alloc(type, 0);
    if (self) {
        self->hDict = 0;
        self->owned = 0;
    }
    return (PyObject *)self;
}

static void AdsDictionary_dealloc(AdsDictionaryObject *self)
{
    if (self->hDict && self->owned)
        AdsDisconnect(self->hDict);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *AdsDictionary_open(PyObject *cls, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"path", "server_type", "user", "password", "options", NULL};

    const char *path       = NULL;
    int         serverType = ADS_REMOTE_SERVER;
    const char *user       = "";
    const char *password   = "";
    int         options    = ADS_CHECKRIGHTS;
    ADSHANDLE   hConn = 0;
    UNSIGNED32  ulRet;
    AdsDictionaryObject *self;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|issi", kwlist,
                                     &path, &serverType, &user, &password, &options))
        return NULL;

    ulRet = AdsConnect60(
        (UNSIGNED8 *)path,
        (UNSIGNED16)serverType,
        (UNSIGNED8 *)user,
        (UNSIGNED8 *)password,
        (UNSIGNED32)options,
        &hConn);

    if (ulRet != AE_SUCCESS) {
        ads_set_error(ulRet, "AdsDictionary.open");
        return NULL;
    }

    self = (AdsDictionaryObject *)((PyTypeObject *)cls)->tp_alloc((PyTypeObject *)cls, 0);
    if (!self) { AdsDisconnect(hConn); return NULL; }

    self->hDict = hConn;
    self->owned = 1;
    return (PyObject *)self;
}

static PyObject *AdsDictionary_from_connection(PyObject *cls, PyObject *args)
{
    PyObject *connObj = NULL;
    AdsDictionaryObject *self;

    if (!PyArg_ParseTuple(args, "O", &connObj))
        return NULL;

    if (!PyObject_IsInstance(connObj, (PyObject *)&AdsConnectionType)) {
        PyErr_SetString(PyExc_TypeError, "expected AdsConnection");
        return NULL;
    }

    {
        AdsConnectionObject *conn = (AdsConnectionObject *)connObj;
        if (conn->closed || !conn->hConn) {
            PyErr_SetString(AdsError, "connection is closed");
            return NULL;
        }

        self = (AdsDictionaryObject *)((PyTypeObject *)cls)->tp_alloc((PyTypeObject *)cls, 0);
        if (!self) return NULL;
        self->hDict = conn->hConn;
        self->owned = 0;
    }

    return (PyObject *)self;
}

static PyObject *AdsDictionary_close(AdsDictionaryObject *self, PyObject *Py_UNUSED(ignored))
{
    if (self->hDict && self->owned) {
        AdsDisconnect(self->hDict);
        self->hDict = 0;
    } else {
        self->hDict = 0;
    }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_enter(AdsDictionaryObject *self, PyObject *Py_UNUSED(ignored))
{
    CHECK_DICT(self);
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *AdsDictionary_exit(AdsDictionaryObject *self, PyObject *args)
{
    PyObject *exc_type, *exc_val, *exc_tb;
    if (!PyArg_ParseTuple(args, "OOO", &exc_type, &exc_val, &exc_tb))
        return NULL;
    AdsDictionary_close(self, NULL);
    Py_RETURN_FALSE;
}

static PyObject *AdsDictionary_repr(AdsDictionaryObject *self)
{
    if (!self->hDict)
        return PyUnicode_FromString("<AdsDictionary (closed)>");
    return PyUnicode_FromFormat("<AdsDictionary handle=%p owned=%d>",
                                (void *)self->hDict, self->owned);
}

/* ======================================================================
 * Database properties
 * ====================================================================== */

static PyObject *AdsDictionary_get_database_property(AdsDictionaryObject *self, PyObject *args)
{
    unsigned long prop;
    char          buf[4096];
    UNSIGNED16    usLen = (UNSIGNED16)(sizeof(buf) - 1);
    UNSIGNED32    ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "k", &prop))
        return NULL;

    ulRet = AdsDDGetDatabaseProperty(self->hDict,
                                      (UNSIGNED16)prop,
                                      (UNSIGNED8 *)buf, &usLen);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "get_database_property"); return NULL; }
    buf[usLen] = '\0';
    return PyUnicode_DecodeUTF8(buf, usLen, "replace");
}

static PyObject *AdsDictionary_set_database_property(AdsDictionaryObject *self, PyObject *args)
{
    unsigned long prop;
    const char   *val;
    Py_ssize_t    val_len;
    UNSIGNED32    ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "ks#", &prop, &val, &val_len))
        return NULL;

    ulRet = AdsDDSetDatabaseProperty(self->hDict, (UNSIGNED16)prop,
                                      (VOID *)val, (UNSIGNED16)val_len);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "set_database_property"); return NULL; }
    Py_RETURN_NONE;
}

/* ======================================================================
 * Table management
 * ====================================================================== */

static PyObject *AdsDictionary_add_table(AdsDictionaryObject *self, PyObject *args, PyObject *kwds)
{
    static char *kw[] = {"alias","path","table_type","char_type","index_path","comment",NULL};
    const char   *alias, *path;
    unsigned long table_type = 3;  /* ADS_ADT */
    unsigned long char_type  = 1;  /* ADS_ANSI */
    const char   *index_path = "", *comment = "";
    UNSIGNED32    ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ss|kkss", kw,
                                     &alias, &path, &table_type, &char_type,
                                     &index_path, &comment))
        return NULL;

    ulRet = AdsDDAddTable(self->hDict,
                          (UNSIGNED8 *)alias, (UNSIGNED8 *)path,
                          (UNSIGNED16)table_type, (UNSIGNED16)char_type,
                          (UNSIGNED8 *)index_path, (UNSIGNED8 *)comment);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "add_table"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_remove_table(AdsDictionaryObject *self, PyObject *args)
{
    const char *alias;
    int         delete_files = 0;
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "s|p", &alias, &delete_files))
        return NULL;

    ulRet = AdsDDRemoveTable(self->hDict, (UNSIGNED8 *)alias,
                              (UNSIGNED16)(delete_files ? 1 : 0));
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "remove_table"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_get_table_property(AdsDictionaryObject *self, PyObject *args)
{
    const char *tableName;
    unsigned long prop;
    char       buf[4096];
    UNSIGNED16 usLen = (UNSIGNED16)(sizeof(buf) - 1);
    UNSIGNED32 ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "sk", &tableName, &prop))
        return NULL;

    ulRet = AdsDDGetTableProperty(self->hDict,
                                   (UNSIGNED8 *)tableName,
                                   (UNSIGNED16)prop,
                                   (UNSIGNED8 *)buf, &usLen);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "get_table_property"); return NULL; }
    buf[usLen] = '\0';
    return PyUnicode_DecodeUTF8(buf, usLen, "replace");
}

/* OpenADS: AdsDDSetTableProperty(hDict, table, prop, val, len) — 5 args, no validateOption */
static PyObject *AdsDictionary_set_table_property(AdsDictionaryObject *self, PyObject *args)
{
    const char *tableName, *val;
    unsigned long prop;
    Py_ssize_t    val_len;
    UNSIGNED32    ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "sks#", &tableName, &prop, &val, &val_len))
        return NULL;

    ulRet = AdsDDSetTableProperty(self->hDict, (UNSIGNED8 *)tableName,
                                   (UNSIGNED16)prop, (VOID *)val,
                                   (UNSIGNED16)val_len);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "set_table_property"); return NULL; }
    Py_RETURN_NONE;
}

/* ======================================================================
 * Field properties
 * ====================================================================== */

static PyObject *AdsDictionary_get_field_property(AdsDictionaryObject *self, PyObject *args)
{
    const char *tableName, *field;
    unsigned long prop;
    char       buf[4096];
    UNSIGNED16 usLen = (UNSIGNED16)(sizeof(buf) - 1);
    UNSIGNED32 ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "ssk", &tableName, &field, &prop))
        return NULL;

    ulRet = AdsDDGetFieldProperty(self->hDict,
                                   (UNSIGNED8 *)tableName, (UNSIGNED8 *)field,
                                   (UNSIGNED16)prop, (UNSIGNED8 *)buf, &usLen);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "get_field_property"); return NULL; }
    buf[usLen] = '\0';
    return PyUnicode_DecodeUTF8(buf, usLen, "replace");
}

/* OpenADS: AdsDDSetFieldProperty(hDict, table, field, prop, val, len) — 6 args */
static PyObject *AdsDictionary_set_field_property(AdsDictionaryObject *self, PyObject *args)
{
    const char *tableName, *field, *val;
    unsigned long prop;
    Py_ssize_t    val_len;
    UNSIGNED32    ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "ssks#", &tableName, &field, &prop, &val, &val_len))
        return NULL;

    ulRet = AdsDDSetFieldProperty(self->hDict,
                                   (UNSIGNED8 *)tableName, (UNSIGNED8 *)field,
                                   (UNSIGNED16)prop, (VOID *)val,
                                   (UNSIGNED16)val_len);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "set_field_property"); return NULL; }
    Py_RETURN_NONE;
}

/* ======================================================================
 * Index file management
 * ====================================================================== */

static PyObject *AdsDictionary_add_index_file(AdsDictionaryObject *self, PyObject *args)
{
    const char *tableName, *indexPath;
    const char *comment = "";
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "ss|s", &tableName, &indexPath, &comment))
        return NULL;

    ulRet = AdsDDAddIndexFile(self->hDict,
                               (UNSIGNED8 *)tableName,
                               (UNSIGNED8 *)indexPath,
                               (UNSIGNED8 *)comment);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "add_index_file"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_remove_index_file(AdsDictionaryObject *self, PyObject *args)
{
    const char *tableName, *indexPath;
    int         delete_file = 0;
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "ss|p", &tableName, &indexPath, &delete_file))
        return NULL;

    ulRet = AdsDDRemoveIndexFile(self->hDict,
                                  (UNSIGNED8 *)tableName,
                                  (UNSIGNED8 *)indexPath,
                                  (UNSIGNED16)(delete_file ? 1 : 0));
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "remove_index_file"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_get_index_property(AdsDictionaryObject *self, PyObject *args)
{
    const char *tableName, *indexName;
    unsigned long prop;
    char       buf[4096];
    UNSIGNED16 usLen = (UNSIGNED16)(sizeof(buf) - 1);
    UNSIGNED32 ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "ssk", &tableName, &indexName, &prop))
        return NULL;

    ulRet = AdsDDGetIndexProperty(self->hDict,
                                   (UNSIGNED8 *)tableName, (UNSIGNED8 *)indexName,
                                   (UNSIGNED16)prop, (UNSIGNED8 *)buf, &usLen);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "get_index_property"); return NULL; }
    buf[usLen] = '\0';
    return PyUnicode_DecodeUTF8(buf, usLen, "replace");
}

static PyObject *AdsDictionary_set_index_property(AdsDictionaryObject *self, PyObject *args)
{
    const char *tableName, *indexName, *val;
    unsigned long prop;
    Py_ssize_t    val_len;
    UNSIGNED32    ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "ssks#", &tableName, &indexName, &prop, &val, &val_len))
        return NULL;

    ulRet = AdsDDSetIndexProperty(self->hDict,
                                   (UNSIGNED8 *)tableName, (UNSIGNED8 *)indexName,
                                   (UNSIGNED16)prop, (VOID *)val, (UNSIGNED16)val_len);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "set_index_property"); return NULL; }
    Py_RETURN_NONE;
}

/* ======================================================================
 * User management
 * ====================================================================== */

static PyObject *AdsDictionary_create_user(AdsDictionaryObject *self, PyObject *args)
{
    const char *user;
    const char *password = "", *group = "", *desc = "";
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "s|sss", &user, &password, &group, &desc))
        return NULL;

    ulRet = AdsDDCreateUser(self->hDict,
                             (UNSIGNED8 *)group,
                             (UNSIGNED8 *)user,
                             (UNSIGNED8 *)password,
                             (UNSIGNED8 *)desc);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "create_user"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_delete_user(AdsDictionaryObject *self, PyObject *args)
{
    const char *user;
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "s", &user))
        return NULL;

    ulRet = AdsDDDeleteUser(self->hDict, (UNSIGNED8 *)user);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "delete_user"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_get_user_property(AdsDictionaryObject *self, PyObject *args)
{
    const char *user;
    unsigned long prop;
    char       buf[4096];
    UNSIGNED16 usLen = (UNSIGNED16)(sizeof(buf) - 1);
    UNSIGNED32 ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "sk", &user, &prop))
        return NULL;

    ulRet = AdsDDGetUserProperty(self->hDict, (UNSIGNED8 *)user,
                                  (UNSIGNED16)prop, (UNSIGNED8 *)buf, &usLen);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "get_user_property"); return NULL; }
    buf[usLen] = '\0';
    return PyUnicode_DecodeUTF8(buf, usLen, "replace");
}

static PyObject *AdsDictionary_set_user_property(AdsDictionaryObject *self, PyObject *args)
{
    const char *user, *val;
    unsigned long prop;
    Py_ssize_t    val_len;
    UNSIGNED32    ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "sks#", &user, &prop, &val, &val_len))
        return NULL;

    ulRet = AdsDDSetUserProperty(self->hDict, (UNSIGNED8 *)user,
                                  (UNSIGNED16)prop, (VOID *)val, (UNSIGNED16)val_len);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "set_user_property"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_add_user_to_group(AdsDictionaryObject *self, PyObject *args)
{
    const char *user, *group;
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "ss", &user, &group))
        return NULL;

    ulRet = AdsDDAddUserToGroup(self->hDict, (UNSIGNED8 *)group, (UNSIGNED8 *)user);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "add_user_to_group"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_remove_user_from_group(AdsDictionaryObject *self, PyObject *args)
{
    const char *user, *group;
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "ss", &user, &group))
        return NULL;

    ulRet = AdsDDRemoveUserFromGroup(self->hDict, (UNSIGNED8 *)group, (UNSIGNED8 *)user);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "remove_user_from_group"); return NULL; }
    Py_RETURN_NONE;
}

/* OpenADS: AdsDDGetUserTableRights(hDict, tableName, user, &rights) */
static PyObject *AdsDictionary_get_user_table_rights(AdsDictionaryObject *self, PyObject *args)
{
    const char *user, *table;
    UNSIGNED32  ulRights = 0;
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "ss", &user, &table))
        return NULL;

    ulRet = AdsDDGetUserTableRights(self->hDict,
                                     (UNSIGNED8 *)table,
                                     (UNSIGNED8 *)user,
                                     &ulRights);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "get_user_table_rights"); return NULL; }
    return PyLong_FromUnsignedLong((unsigned long)ulRights);
}

/* OpenADS: AdsDDSetUserTableRights(hDict, tableName, user, rights) — single call */
static PyObject *AdsDictionary_set_user_table_rights(AdsDictionaryObject *self, PyObject *args)
{
    const char   *user, *table;
    unsigned long level;
    UNSIGNED32    ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "ssk", &user, &table, &level))
        return NULL;

    ulRet = AdsDDSetUserTableRights(self->hDict,
                                     (UNSIGNED8 *)table,
                                     (UNSIGNED8 *)user,
                                     (UNSIGNED32)level);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "set_user_table_rights"); return NULL; }
    Py_RETURN_NONE;
}

/* ======================================================================
 * Views — OpenADS: AdsDDCreateView / AdsDDDropView
 * ====================================================================== */

static PyObject *AdsDictionary_create_view(AdsDictionaryObject *self, PyObject *args)
{
    const char *name, *sql;
    const char *comment = "";
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "ss|s", &name, &sql, &comment))
        return NULL;

    ulRet = AdsDDCreateView(self->hDict,
                             (UNSIGNED8 *)name,
                             (UNSIGNED8 *)comment,
                             (UNSIGNED8 *)sql);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "create_view"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_drop_view(AdsDictionaryObject *self, PyObject *args)
{
    const char *name;
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "s", &name))
        return NULL;

    ulRet = AdsDDDropView(self->hDict, (UNSIGNED8 *)name);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "drop_view"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_get_view_property(AdsDictionaryObject *self, PyObject *args)
{
    const char *view;
    unsigned long prop;
    char       buf[4096];
    UNSIGNED16 usLen = (UNSIGNED16)(sizeof(buf) - 1);
    UNSIGNED32 ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "sk", &view, &prop))
        return NULL;

    ulRet = AdsDDGetViewProperty(self->hDict, (UNSIGNED8 *)view,
                                  (UNSIGNED16)prop, (UNSIGNED8 *)buf, &usLen);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "get_view_property"); return NULL; }
    buf[usLen] = '\0';
    return PyUnicode_DecodeUTF8(buf, usLen, "replace");
}

static PyObject *AdsDictionary_set_view_property(AdsDictionaryObject *self, PyObject *args)
{
    const char *view, *val;
    unsigned long prop;
    Py_ssize_t    val_len;
    UNSIGNED32    ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "sks#", &view, &prop, &val, &val_len))
        return NULL;

    ulRet = AdsDDSetViewProperty(self->hDict, (UNSIGNED8 *)view,
                                  (UNSIGNED16)prop, (VOID *)val, (UNSIGNED16)val_len);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "set_view_property"); return NULL; }
    Py_RETURN_NONE;
}

/* ======================================================================
 * Stored procedures — OpenADS: AdsDDCreateProcedure / AdsDDDropProcedure
 *                              AdsDDGetProcProperty / AdsDDSetProcProperty
 * ====================================================================== */

static PyObject *AdsDictionary_create_procedure(AdsDictionaryObject *self, PyObject *args)
{
    const char *name, *container, *procedure;
    const char *input = "", *output = "", *comment = "";
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "sss|sss",
                          &name, &container, &procedure,
                          &input, &output, &comment))
        return NULL;

    ulRet = AdsDDCreateProcedure(self->hDict,
                                  (UNSIGNED8 *)name,
                                  (UNSIGNED8 *)container,
                                  (UNSIGNED8 *)procedure,
                                  0,
                                  (UNSIGNED8 *)input,
                                  (UNSIGNED8 *)output,
                                  (UNSIGNED8 *)comment);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "create_procedure"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_drop_procedure(AdsDictionaryObject *self, PyObject *args)
{
    const char *name;
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "s", &name))
        return NULL;

    ulRet = AdsDDDropProcedure(self->hDict, (UNSIGNED8 *)name);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "drop_procedure"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_get_proc_property(AdsDictionaryObject *self, PyObject *args)
{
    const char *name;
    unsigned long prop;
    char       buf[4096];
    UNSIGNED16 usLen = (UNSIGNED16)(sizeof(buf) - 1);
    UNSIGNED32 ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "sk", &name, &prop))
        return NULL;

    ulRet = AdsDDGetProcProperty(self->hDict, (UNSIGNED8 *)name,
                                  (UNSIGNED16)prop, (UNSIGNED8 *)buf, &usLen);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "get_proc_property"); return NULL; }
    buf[usLen] = '\0';
    return PyUnicode_DecodeUTF8(buf, usLen, "replace");
}

static PyObject *AdsDictionary_set_proc_property(AdsDictionaryObject *self, PyObject *args)
{
    const char *name, *val;
    unsigned long prop;
    Py_ssize_t    val_len;
    UNSIGNED32    ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "sks#", &name, &prop, &val, &val_len))
        return NULL;

    ulRet = AdsDDSetProcProperty(self->hDict, (UNSIGNED8 *)name,
                                  (UNSIGNED16)prop, (VOID *)val, (UNSIGNED16)val_len);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "set_proc_property"); return NULL; }
    Py_RETURN_NONE;
}

/* ======================================================================
 * Triggers — OpenADS: AdsDDCreateTrigger (8 args), AdsDDDropTrigger,
 *                     AdsDDGetTriggerProperty, AdsDDSetTriggerProperty (supported)
 * OpenADS signature: (hDict, name, table, type, 0, container, procedure, priority)
 * ====================================================================== */

static PyObject *AdsDictionary_create_trigger(AdsDictionaryObject *self, PyObject *args)
{
    const char   *name, *table, *container, *procedure;
    unsigned long trigger_type;
    unsigned long priority = 1;
    UNSIGNED32    ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "sskss|k",
                          &name, &table, &trigger_type,
                          &container, &procedure, &priority))
        return NULL;

    ulRet = AdsDDCreateTrigger(self->hDict,
                                (UNSIGNED8 *)name,
                                (UNSIGNED8 *)table,
                                (UNSIGNED32)trigger_type,
                                0,
                                (UNSIGNED8 *)container,
                                (UNSIGNED8 *)procedure,
                                (UNSIGNED32)priority);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "create_trigger"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_drop_trigger(AdsDictionaryObject *self, PyObject *args)
{
    const char *name;
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "s", &name))
        return NULL;

    ulRet = AdsDDDropTrigger(self->hDict, (UNSIGNED8 *)name);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "drop_trigger"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_get_trigger_property(AdsDictionaryObject *self, PyObject *args)
{
    const char *name;
    unsigned long prop;
    char       buf[4096];
    UNSIGNED16 usLen = (UNSIGNED16)(sizeof(buf) - 1);
    UNSIGNED32 ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "sk", &name, &prop))
        return NULL;

    ulRet = AdsDDGetTriggerProperty(self->hDict, (UNSIGNED8 *)name,
                                     (UNSIGNED16)prop, (UNSIGNED8 *)buf, &usLen);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "get_trigger_property"); return NULL; }
    buf[usLen] = '\0';
    return PyUnicode_DecodeUTF8(buf, usLen, "replace");
}

/* OpenADS supports AdsDDSetTriggerProperty (unlike SAP ACE) */
static PyObject *AdsDictionary_set_trigger_property(AdsDictionaryObject *self, PyObject *args)
{
    const char *name, *val;
    unsigned long prop;
    Py_ssize_t    val_len;
    UNSIGNED32    ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "sks#", &name, &prop, &val, &val_len))
        return NULL;

    ulRet = AdsDDSetTriggerProperty(self->hDict, (UNSIGNED8 *)name,
                                     (UNSIGNED16)prop, (VOID *)val, (UNSIGNED16)val_len);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "set_trigger_property"); return NULL; }
    Py_RETURN_NONE;
}

/* ======================================================================
 * Referential integrity
 * ====================================================================== */

static PyObject *AdsDictionary_create_ref_integrity(AdsDictionaryObject *self, PyObject *args)
{
    const char   *name, *fail_table, *parent, *parent_tag, *child, *child_tag;
    unsigned long update_rule = 0, delete_rule = 0;
    UNSIGNED32    ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "ssssss|kk",
                          &name, &fail_table,
                          &parent, &parent_tag,
                          &child, &child_tag,
                          &update_rule, &delete_rule))
        return NULL;

    ulRet = AdsDDCreateRefIntegrity(self->hDict,
                                     (UNSIGNED8 *)name,
                                     (UNSIGNED8 *)fail_table,
                                     (UNSIGNED8 *)parent,
                                     (UNSIGNED8 *)parent_tag,
                                     (UNSIGNED8 *)child,
                                     (UNSIGNED8 *)child_tag,
                                     (UNSIGNED16)update_rule,
                                     (UNSIGNED16)delete_rule);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "create_ref_integrity"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_remove_ref_integrity(AdsDictionaryObject *self, PyObject *args)
{
    const char *name;
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "s", &name))
        return NULL;

    ulRet = AdsDDRemoveRefIntegrity(self->hDict, (UNSIGNED8 *)name);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "remove_ref_integrity"); return NULL; }
    Py_RETURN_NONE;
}

/* ======================================================================
 * Links
 * ====================================================================== */

static PyObject *AdsDictionary_create_link(AdsDictionaryObject *self, PyObject *args)
{
    const char *alias, *path;
    const char *user = "", *password = "";
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "ss|ss", &alias, &path, &user, &password))
        return NULL;

    ulRet = AdsDDCreateLink(self->hDict,
                             (UNSIGNED8 *)alias,
                             (UNSIGNED8 *)path,
                             (UNSIGNED8 *)user,
                             (UNSIGNED8 *)password,
                             0);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "create_link"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_drop_link(AdsDictionaryObject *self, PyObject *args)
{
    const char *alias;
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "s", &alias))
        return NULL;

    ulRet = AdsDDDropLink(self->hDict, (UNSIGNED8 *)alias, 0);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "drop_link"); return NULL; }
    Py_RETURN_NONE;
}

static PyObject *AdsDictionary_modify_link(AdsDictionaryObject *self, PyObject *args)
{
    const char *alias;
    const char *path = "", *user = "", *password = "";
    UNSIGNED32  ulRet;

    CHECK_DICT(self);
    if (!PyArg_ParseTuple(args, "s|sss", &alias, &path, &user, &password))
        return NULL;

    ulRet = AdsDDModifyLink(self->hDict,
                             (UNSIGNED8 *)alias,
                             (UNSIGNED8 *)path,
                             (UNSIGNED8 *)user,
                             (UNSIGNED8 *)password,
                             0);
    if (ulRet != AE_SUCCESS) { ads_set_error(ulRet, "modify_link"); return NULL; }
    Py_RETURN_NONE;
}

/* ======================================================================
 * Method table and type definition
 * ====================================================================== */

static PyMethodDef AdsDictionary_methods[] = {
    {"open",                    (PyCFunction)AdsDictionary_open,                    METH_CLASS|METH_VARARGS|METH_KEYWORDS, "open(path, ...) -> AdsDictionary"},
    {"from_connection",         (PyCFunction)AdsDictionary_from_connection,         METH_CLASS|METH_VARARGS,               "from_connection(conn) -> AdsDictionary"},
    {"close",                   (PyCFunction)AdsDictionary_close,                   METH_NOARGS,  "Close the dictionary connection."},
    {"__enter__",               (PyCFunction)AdsDictionary_enter,                   METH_NOARGS,  NULL},
    {"__exit__",                (PyCFunction)AdsDictionary_exit,                    METH_VARARGS, NULL},
    {"get_database_property",   (PyCFunction)AdsDictionary_get_database_property,   METH_VARARGS, "get_database_property(prop) -> str"},
    {"set_database_property",   (PyCFunction)AdsDictionary_set_database_property,   METH_VARARGS, "set_database_property(prop, val)"},
    {"add_table",               (PyCFunction)AdsDictionary_add_table,               METH_VARARGS|METH_KEYWORDS, "add_table(alias, path, ...)"},
    {"remove_table",            (PyCFunction)AdsDictionary_remove_table,            METH_VARARGS, "remove_table(alias, delete_files=False)"},
    {"get_table_property",      (PyCFunction)AdsDictionary_get_table_property,      METH_VARARGS, "get_table_property(table, prop) -> str"},
    {"set_table_property",      (PyCFunction)AdsDictionary_set_table_property,      METH_VARARGS, "set_table_property(table, prop, val)"},
    {"get_field_property",      (PyCFunction)AdsDictionary_get_field_property,      METH_VARARGS, "get_field_property(table, field, prop) -> str"},
    {"set_field_property",      (PyCFunction)AdsDictionary_set_field_property,      METH_VARARGS, "set_field_property(table, field, prop, val)"},
    {"add_index_file",          (PyCFunction)AdsDictionary_add_index_file,          METH_VARARGS, "add_index_file(table, index_path, comment='')"},
    {"remove_index_file",       (PyCFunction)AdsDictionary_remove_index_file,       METH_VARARGS, "remove_index_file(table, index_path, delete_file=False)"},
    {"get_index_property",      (PyCFunction)AdsDictionary_get_index_property,      METH_VARARGS, "get_index_property(table, index, prop) -> str"},
    {"set_index_property",      (PyCFunction)AdsDictionary_set_index_property,      METH_VARARGS, "set_index_property(table, index, prop, val)"},
    {"create_user",             (PyCFunction)AdsDictionary_create_user,             METH_VARARGS, "create_user(user, password='', group='', desc='')"},
    {"delete_user",             (PyCFunction)AdsDictionary_delete_user,             METH_VARARGS, "delete_user(user)"},
    {"get_user_property",       (PyCFunction)AdsDictionary_get_user_property,       METH_VARARGS, "get_user_property(user, prop) -> str"},
    {"set_user_property",       (PyCFunction)AdsDictionary_set_user_property,       METH_VARARGS, "set_user_property(user, prop, val)"},
    {"add_user_to_group",       (PyCFunction)AdsDictionary_add_user_to_group,       METH_VARARGS, "add_user_to_group(user, group)"},
    {"remove_user_from_group",  (PyCFunction)AdsDictionary_remove_user_from_group,  METH_VARARGS, "remove_user_from_group(user, group)"},
    {"get_user_table_rights",   (PyCFunction)AdsDictionary_get_user_table_rights,   METH_VARARGS, "get_user_table_rights(user, table) -> int"},
    {"set_user_table_rights",   (PyCFunction)AdsDictionary_set_user_table_rights,   METH_VARARGS, "set_user_table_rights(user, table, rights)"},
    {"create_view",             (PyCFunction)AdsDictionary_create_view,             METH_VARARGS, "create_view(name, sql, comment='')"},
    {"drop_view",               (PyCFunction)AdsDictionary_drop_view,               METH_VARARGS, "drop_view(name)"},
    {"get_view_property",       (PyCFunction)AdsDictionary_get_view_property,       METH_VARARGS, "get_view_property(view, prop) -> str"},
    {"set_view_property",       (PyCFunction)AdsDictionary_set_view_property,       METH_VARARGS, "set_view_property(view, prop, val)"},
    {"create_procedure",        (PyCFunction)AdsDictionary_create_procedure,        METH_VARARGS, "create_procedure(name, container, procedure, ...)"},
    {"drop_procedure",          (PyCFunction)AdsDictionary_drop_procedure,          METH_VARARGS, "drop_procedure(name)"},
    {"get_proc_property",       (PyCFunction)AdsDictionary_get_proc_property,       METH_VARARGS, "get_proc_property(name, prop) -> str"},
    {"set_proc_property",       (PyCFunction)AdsDictionary_set_proc_property,       METH_VARARGS, "set_proc_property(name, prop, val)"},
    {"create_trigger",          (PyCFunction)AdsDictionary_create_trigger,          METH_VARARGS, "create_trigger(name, table, type, container, proc, priority=1)"},
    {"drop_trigger",            (PyCFunction)AdsDictionary_drop_trigger,            METH_VARARGS, "drop_trigger(name)"},
    {"get_trigger_property",    (PyCFunction)AdsDictionary_get_trigger_property,    METH_VARARGS, "get_trigger_property(name, prop) -> str"},
    {"set_trigger_property",    (PyCFunction)AdsDictionary_set_trigger_property,    METH_VARARGS, "set_trigger_property(name, prop, val)"},
    {"create_ref_integrity",    (PyCFunction)AdsDictionary_create_ref_integrity,    METH_VARARGS, "create_ref_integrity(name, fail_table, parent, parent_tag, child, child_tag, ...)"},
    {"remove_ref_integrity",    (PyCFunction)AdsDictionary_remove_ref_integrity,    METH_VARARGS, "remove_ref_integrity(name)"},
    {"create_link",             (PyCFunction)AdsDictionary_create_link,             METH_VARARGS, "create_link(alias, path, user='', password='')"},
    {"drop_link",               (PyCFunction)AdsDictionary_drop_link,               METH_VARARGS, "drop_link(alias)"},
    {"modify_link",             (PyCFunction)AdsDictionary_modify_link,             METH_VARARGS, "modify_link(alias, path='', user='', password='')"},
    {NULL, NULL, 0, NULL}
};

PyTypeObject AdsDictionaryType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "openads.AdsDictionary",
    .tp_basicsize = sizeof(AdsDictionaryObject),
    .tp_itemsize  = 0,
    .tp_dealloc   = (destructor)AdsDictionary_dealloc,
    .tp_repr      = (reprfunc)AdsDictionary_repr,
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_doc       = "AdsDictionary — OpenADS data dictionary access (full CRUD).",
    .tp_methods   = AdsDictionary_methods,
    .tp_new       = AdsDictionary_new,
};
