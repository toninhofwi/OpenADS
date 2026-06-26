// openads_odbc.cpp — OpenADS ODBC driver.
//
// Implements the core ODBC entry points by delegating to the OpenADS thin SQL
// C API (openads_sql.h). This first slice covers a forward SELECT round-trip:
// connect (connection string) -> SQLExecDirect -> describe -> SQLFetch ->
// SQLGetData (character), plus CREATE/INSERT/DELETE passthrough.
//
// The functions are the standard ODBC ABI, so a Driver Manager (or a direct
// caller) drives them unchanged. No engine behaviour lives here.
#include "openads/openads_sql.h"

#ifdef _WIN32
#  include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

#include <cctype>
#include <cstring>
#include <new>
#include <string>

namespace {

enum class HKind { Env, Dbc, Stmt };

struct EnvH { HKind kind = HKind::Env; };
struct DbcH { HKind kind = HKind::Dbc; openads_conn* conn = nullptr; std::string err; };
struct StmtH {
    HKind         kind = HKind::Stmt;
    DbcH*         dbc  = nullptr;
    openads_stmt* st   = nullptr;
    std::string   err;
};

// Every handle struct starts with HKind, so the kind is readable from the raw
// handle pointer regardless of which struct it is.
HKind kind_of(SQLHANDLE h) { return *reinterpret_cast<HKind*>(h); }

std::string to_string(SQLCHAR* s, SQLINTEGER len) {
    if (!s) return std::string();
    if (len == SQL_NTS) return std::string(reinterpret_cast<char*>(s));
    return std::string(reinterpret_cast<char*>(s), static_cast<size_t>(len));
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back()  == ' ') s.pop_back();
    return s;
}

// Look up KEY in a "k1=v1;k2=v2" connection string (case-insensitive key).
std::string conn_get(const std::string& cs, const char* key) {
    std::string want = lower(key);
    size_t i = 0;
    while (i <= cs.size()) {
        size_t semi = cs.find(';', i);
        std::string tok = cs.substr(i, semi == std::string::npos ? std::string::npos
                                                                  : semi - i);
        size_t eq = tok.find('=');
        if (eq != std::string::npos &&
            lower(trim(tok.substr(0, eq))) == want)
            return trim(tok.substr(eq + 1));
        if (semi == std::string::npos) break;
        i = semi + 1;
    }
    return std::string();
}

// Truncating, always-NUL-terminating copy into an ODBC output buffer.
void copy_out(SQLCHAR* dst, SQLSMALLINT cap, const std::string& src,
              SQLSMALLINT* out_len) {
    if (dst && cap > 0) {
        size_t n = src.size() < static_cast<size_t>(cap - 1)
                       ? src.size() : static_cast<size_t>(cap - 1);
        if (n) std::memcpy(dst, src.data(), n);
        dst[n] = 0;
        if (out_len) *out_len = static_cast<SQLSMALLINT>(n);
    } else if (out_len) {
        *out_len = 0;
    }
}

} // namespace

extern "C" {

SQLRETURN SQL_API SQLAllocHandle(SQLSMALLINT type, SQLHANDLE input,
                                 SQLHANDLE* out) {
    if (!out) return SQL_ERROR;
    *out = SQL_NULL_HANDLE;
    // nothrow allocation: no C++ exception may escape this extern "C" boundary.
    switch (type) {
        case SQL_HANDLE_ENV: {
            auto* e = new (std::nothrow) EnvH();
            if (!e) return SQL_ERROR;
            *out = e;
            return SQL_SUCCESS;
        }
        case SQL_HANDLE_DBC: {
            if (!input) return SQL_ERROR;
            auto* d = new (std::nothrow) DbcH();
            if (!d) return SQL_ERROR;
            *out = d;
            return SQL_SUCCESS;
        }
        case SQL_HANDLE_STMT: {
            if (!input) return SQL_ERROR;
            auto* s = new (std::nothrow) StmtH();
            if (!s) return SQL_ERROR;
            s->dbc = reinterpret_cast<DbcH*>(input);
            *out = s;
            return SQL_SUCCESS;
        }
        default:
            return SQL_ERROR;
    }
}

SQLRETURN SQL_API SQLSetEnvAttr(SQLHENV, SQLINTEGER, SQLPOINTER, SQLINTEGER) {
    return SQL_SUCCESS;   // accept ODBC version negotiation, etc.
}

SQLRETURN SQL_API SQLDriverConnect(SQLHDBC hdbc, SQLHWND, SQLCHAR* inconn,
                                   SQLSMALLINT inlen, SQLCHAR* outconn,
                                   SQLSMALLINT outmax, SQLSMALLINT* outlen,
                                   SQLUSMALLINT) {
    if (!hdbc) return SQL_ERROR;
    auto* dbc = reinterpret_cast<DbcH*>(hdbc);
    std::string cs = to_string(inconn, inlen);

    std::string dir = conn_get(cs, "DataDir");
    if (dir.empty()) dir = conn_get(cs, "Database");
    std::string stype = conn_get(cs, "ServerType");
    if (stype.empty()) stype = "local";

    openads_conn* c = nullptr;
    if (openads_connect(dir.c_str(), stype.c_str(), nullptr, nullptr, &c)
        != OPENADS_OK) {
        dbc->err = "connect failed for DataDir=" + dir;
        return SQL_ERROR;
    }
    dbc->conn = c;
    copy_out(outconn, outmax, cs, outlen);
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLConnect(SQLHDBC hdbc, SQLCHAR* dsn, SQLSMALLINT dsnlen,
                             SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT) {
    if (!hdbc) return SQL_ERROR;
    auto* dbc = reinterpret_cast<DbcH*>(hdbc);
    std::string dir = to_string(dsn, dsnlen);   // DSN string taken as data dir
    openads_conn* c = nullptr;
    if (openads_connect(dir.c_str(), "local", nullptr, nullptr, &c)
        != OPENADS_OK) {
        dbc->err = "connect failed";
        return SQL_ERROR;
    }
    dbc->conn = c;
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLExecDirect(SQLHSTMT hstmt, SQLCHAR* sql, SQLINTEGER len) {
    if (!hstmt) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    if (!s->dbc || !s->dbc->conn) return SQL_ERROR;
    if (s->st) { openads_finalize(s->st); s->st = nullptr; }

    std::string q = to_string(sql, len);
    openads_stmt* st = nullptr;
    if (openads_exec_direct(s->dbc->conn, q.c_str(), &st) != OPENADS_OK) {
        s->err = "exec failed";
        return SQL_ERROR;
    }
    s->st = st;
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLNumResultCols(SQLHSTMT hstmt, SQLSMALLINT* count) {
    if (!hstmt || !count) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    int n = 0;
    if (s->st && openads_num_cols(s->st, &n) == OPENADS_OK)
        *count = static_cast<SQLSMALLINT>(n);
    else
        *count = 0;   // DML / no result set
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLDescribeCol(SQLHSTMT hstmt, SQLUSMALLINT col, SQLCHAR* name,
                                 SQLSMALLINT namemax, SQLSMALLINT* namelen,
                                 SQLSMALLINT* dtype, SQLULEN* dsize,
                                 SQLSMALLINT* ddec, SQLSMALLINT* dnull) {
    if (!hstmt) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    if (!s->st) return SQL_ERROR;
    if (name && namemax > 0) {
        if (openads_col_name(s->st, static_cast<int>(col),
                             reinterpret_cast<char*>(name),
                             static_cast<size_t>(namemax)) != OPENADS_OK)
            return SQL_ERROR;
        if (namelen)
            *namelen = static_cast<SQLSMALLINT>(
                std::strlen(reinterpret_cast<char*>(name)));
    } else if (namelen) {
        *namelen = 0;
    }
    if (dtype) *dtype = SQL_VARCHAR;   // slice 1: surface every column as char
    if (dsize) *dsize = 255;
    if (ddec)  *ddec  = 0;
    if (dnull) *dnull = SQL_NULLABLE_UNKNOWN;
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLFetch(SQLHSTMT hstmt) {
    if (!hstmt) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    if (!s->st) return SQL_ERROR;
    int rc = openads_fetch_next(s->st);
    if (rc == OPENADS_OK) return SQL_SUCCESS;
    if (rc == OPENADS_NO_DATA) return SQL_NO_DATA;
    return SQL_ERROR;
}

SQLRETURN SQL_API SQLGetData(SQLHSTMT hstmt, SQLUSMALLINT col, SQLSMALLINT,
                             SQLPOINTER buf, SQLLEN buflen, SQLLEN* ind) {
    if (!hstmt || !buf || buflen <= 0) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    if (!s->st) return SQL_ERROR;
    size_t n = 0;
    if (openads_get_str(s->st, static_cast<int>(col),
                        reinterpret_cast<char*>(buf),
                        static_cast<size_t>(buflen), &n) != OPENADS_OK)
        return SQL_ERROR;
    if (ind) *ind = static_cast<SQLLEN>(n);
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLRowCount(SQLHSTMT hstmt, SQLLEN* count) {
    if (!hstmt || !count) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    *count = -1;
    long rc = 0;
    if (s->st && openads_row_count(s->st, &rc) == OPENADS_OK)
        *count = static_cast<SQLLEN>(rc);
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLFreeHandle(SQLSMALLINT type, SQLHANDLE h) {
    if (!h) return SQL_ERROR;
    switch (type) {
        case SQL_HANDLE_STMT: {
            auto* s = reinterpret_cast<StmtH*>(h);
            if (s->st) openads_finalize(s->st);
            delete s;
            return SQL_SUCCESS;
        }
        case SQL_HANDLE_DBC: {
            auto* d = reinterpret_cast<DbcH*>(h);
            if (d->conn) openads_disconnect(d->conn);
            delete d;
            return SQL_SUCCESS;
        }
        case SQL_HANDLE_ENV:
            delete reinterpret_cast<EnvH*>(h);
            return SQL_SUCCESS;
        default:
            return SQL_ERROR;
    }
}

SQLRETURN SQL_API SQLDisconnect(SQLHDBC hdbc) {
    if (!hdbc) return SQL_ERROR;
    auto* d = reinterpret_cast<DbcH*>(hdbc);
    if (d->conn) { openads_disconnect(d->conn); d->conn = nullptr; }
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLGetDiagRec(SQLSMALLINT, SQLHANDLE h, SQLSMALLINT rec,
                                SQLCHAR* state, SQLINTEGER* native, SQLCHAR* msg,
                                SQLSMALLINT msgmax, SQLSMALLINT* msglen) {
    if (rec != 1 || !h) return SQL_NO_DATA;
    std::string e;
    switch (kind_of(h)) {
        case HKind::Stmt: e = reinterpret_cast<StmtH*>(h)->err; break;
        case HKind::Dbc:  e = reinterpret_cast<DbcH*>(h)->err;  break;
        default: break;
    }
    if (e.empty()) return SQL_NO_DATA;
    if (state) std::memcpy(state, "HY000", 6);
    if (native) *native = 0;
    copy_out(msg, msgmax, e, msglen);
    return SQL_SUCCESS;
}

} // extern "C"
