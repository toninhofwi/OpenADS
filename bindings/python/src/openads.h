#ifndef OPENADS_H
#define OPENADS_H

#ifndef WIN32
#define WIN32
#endif
#ifndef x64
#define x64
#endif

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <windows.h>
#include "ace.h"

/* Constants that SAP ace.h defines but OpenADS ace.h omits */
#ifndef VOID
#  define VOID void
#endif
#ifndef DOUBLE
#  define DOUBLE double
#endif
#ifndef ADS_FALSE
#  define ADS_FALSE 0
#endif
#ifndef ADS_TRUE
#  define ADS_TRUE  1
#endif
/* String-trim option flags for AdsGetString usOption */
#ifndef ADS_RTRIM
#  define ADS_RTRIM 1
#endif
#ifndef ADS_LTRIM
#  define ADS_LTRIM 2
#endif
#ifndef ADS_TRIM
#  define ADS_TRIM  4
#endif

extern PyObject *AdsError;

extern PyTypeObject AdsConnectionType;
extern PyTypeObject AdsStatementType;
extern PyTypeObject AdsTableType;
extern PyTypeObject AdsTransactionType;
extern PyTypeObject AdsDictionaryType;
extern PyTypeObject AdsPreparedType;

typedef struct {
    PyObject_HEAD
    ADSHANDLE hConn;
    int       closed;
} AdsConnectionObject;

typedef struct {
    PyObject_HEAD
    ADSHANDLE  hStmt;
    ADSHANDLE  hCursor;
    UNSIGNED16 numFields;
    int        eof;
} AdsStatementObject;

typedef struct {
    PyObject_HEAD
    ADSHANDLE hTable;
    int       closed;
} AdsTableObject;

typedef struct {
    PyObject_HEAD
    ADSHANDLE hConn;
    int       active;
} AdsTransactionObject;

typedef struct {
    PyObject_HEAD
    ADSHANDLE hDict;
    int       owned;
} AdsDictionaryObject;

typedef struct {
    PyObject_HEAD
    ADSHANDLE hStmt;
    int       closed;
} AdsPreparedObject;

void      ads_set_error(UNSIGNED32 code, const char *ctx);
PyObject *ads_field_to_pyobject(ADSHANDLE hCursor, const char *fieldName);
PyObject *ads_cursor_row_dict(ADSHANDLE hCursor, UNSIGNED16 numFields);

#endif /* OPENADS_H */
