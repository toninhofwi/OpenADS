// openads_odbc.cpp — OpenADS ODBC driver.
//
// Implements the ODBC entry points by delegating to the OpenADS thin SQL C API
// (openads_sql.h), so any ODBC consumer (pyodbc, PDO_ODBC, Power BI, Excel, ...)
// can talk to OpenADS. No engine behaviour lives here.
//
// Coverage: connection (string), SQLExecDirect / SQLPrepare+SQLExecute, result
// describe + forward fetch + SQLGetData (character), catalog (SQLTables /
// SQLColumns / SQLGetTypeInfo / SQLPrimaryKeys), SQLGetInfo and the attribute
// accept-stubs a real Driver-Manager client needs to get going.
#include "openads/openads_sql.h"

#ifdef _WIN32
#  include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
#include <vector>

namespace {

enum class HKind { Env, Dbc, Stmt };

struct EnvH { HKind kind = HKind::Env; };
struct DbcH {
    HKind kind = HKind::Dbc;
    openads_conn* conn = nullptr;
    std::string   datadir;   // for catalog enumeration of free tables
    std::string   err;
};

struct StmtH {
    HKind         kind = HKind::Stmt;
    DbcH*         dbc  = nullptr;
    openads_stmt* st   = nullptr;   // engine-backed result (null in synthetic mode)
    std::string   err;
    // Synthetic result set (used by the catalog functions).
    bool                                  synth = false;
    std::vector<std::string>              scols;
    std::vector<std::vector<std::string>> srows;
    long                                  spos = 0;   // 0 = before first row
};

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

std::string conn_get(const std::string& cs, const char* key) {
    std::string want = lower(key);
    size_t i = 0;
    while (i <= cs.size()) {
        size_t semi = cs.find(';', i);
        std::string tok = cs.substr(i, semi == std::string::npos ? std::string::npos
                                                                  : semi - i);
        size_t eq = tok.find('=');
        if (eq != std::string::npos && lower(trim(tok.substr(0, eq))) == want)
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

// Map an openads_sql column type code (OPENADS_COLTYPE_*) to the closest ODBC
// SQL type. Unknown/character types fall back to SQL_VARCHAR.
SQLSMALLINT ads_to_sql_type(int col_type) {
    switch (col_type) {
        case OPENADS_COLTYPE_LOGICAL:           return SQL_BIT;
        case OPENADS_COLTYPE_NUMERIC:           return SQL_NUMERIC;
        case OPENADS_COLTYPE_DOUBLE:
        case OPENADS_COLTYPE_CURDOUBLE:
        case OPENADS_COLTYPE_MONEY:             return SQL_DOUBLE;
        case OPENADS_COLTYPE_INTEGER:
        case OPENADS_COLTYPE_AUTOINC:
        case OPENADS_COLTYPE_ROWVERSION:        return SQL_INTEGER;
        case OPENADS_COLTYPE_SHORTINT:          return SQL_SMALLINT;
        case OPENADS_COLTYPE_LONGLONG:          return SQL_BIGINT;
        case OPENADS_COLTYPE_DATE:
        case OPENADS_COLTYPE_COMPACTDATE:       return SQL_TYPE_DATE;
        case OPENADS_COLTYPE_TIME:              return SQL_TYPE_TIME;
        case OPENADS_COLTYPE_TIMESTAMP:
        case OPENADS_COLTYPE_MODTIME:           return SQL_TYPE_TIMESTAMP;
        case OPENADS_COLTYPE_MEMO:              return SQL_LONGVARCHAR;
        case OPENADS_COLTYPE_BINARY:
        case OPENADS_COLTYPE_VARBINARY:         return SQL_VARBINARY;
        case OPENADS_COLTYPE_IMAGE:
        case OPENADS_COLTYPE_RAW:               return SQL_LONGVARBINARY;
        default:                                return SQL_VARCHAR;
    }
}

// Convert a column value already materialised as a string into the C buffer the
// caller asked for (SQLGetData's TargetType). SQL_C_CHAR keeps the textual form;
// the numeric C types parse the string. For fixed-size C types the indicator is
// the type's byte size (per the ODBC contract).
SQLRETURN put_typed(const std::string& v, SQLSMALLINT ctype,
                    SQLPOINTER buf, SQLLEN buflen, SQLLEN* ind) {
    switch (ctype) {
        case SQL_C_SHORT:
        case SQL_C_SSHORT:
        case SQL_C_USHORT:
            *reinterpret_cast<SQLSMALLINT*>(buf) =
                static_cast<SQLSMALLINT>(std::strtol(v.c_str(), nullptr, 10));
            if (ind) *ind = static_cast<SQLLEN>(sizeof(SQLSMALLINT));
            return SQL_SUCCESS;
        case SQL_C_LONG:
        case SQL_C_SLONG:
        case SQL_C_ULONG:
            *reinterpret_cast<SQLINTEGER*>(buf) =
                static_cast<SQLINTEGER>(std::strtol(v.c_str(), nullptr, 10));
            if (ind) *ind = static_cast<SQLLEN>(sizeof(SQLINTEGER));
            return SQL_SUCCESS;
        case SQL_C_SBIGINT:
        case SQL_C_UBIGINT:
            *reinterpret_cast<SQLBIGINT*>(buf) =
                static_cast<SQLBIGINT>(std::strtoll(v.c_str(), nullptr, 10));
            if (ind) *ind = static_cast<SQLLEN>(sizeof(SQLBIGINT));
            return SQL_SUCCESS;
        case SQL_C_FLOAT:
            *reinterpret_cast<SQLREAL*>(buf) =
                static_cast<SQLREAL>(std::strtod(v.c_str(), nullptr));
            if (ind) *ind = static_cast<SQLLEN>(sizeof(SQLREAL));
            return SQL_SUCCESS;
        case SQL_C_DOUBLE:
            *reinterpret_cast<SQLDOUBLE*>(buf) =
                static_cast<SQLDOUBLE>(std::strtod(v.c_str(), nullptr));
            if (ind) *ind = static_cast<SQLLEN>(sizeof(SQLDOUBLE));
            return SQL_SUCCESS;
        case SQL_C_CHAR:
        default: {
            SQLSMALLINT n = 0;
            copy_out(reinterpret_cast<SQLCHAR*>(buf),
                     static_cast<SQLSMALLINT>(buflen > 0x7FFF ? 0x7FFF : buflen),
                     v, &n);
            if (ind) *ind = static_cast<SQLLEN>(n);
            return SQL_SUCCESS;
        }
    }
}

void reset_result(StmtH* s) {
    if (s->st) { openads_finalize(s->st); s->st = nullptr; }
    s->synth = false;
    s->scols.clear();
    s->srows.clear();
    s->spos = 0;
    s->err.clear();
}

// Read one string column of the current row of an engine cursor.
std::string cell(openads_stmt* q, int col) {
    char buf[1024];
    size_t n = 0;
    if (openads_get_str(q, col, buf, sizeof(buf), &n) != OPENADS_OK) return "";
    std::string v(buf, n);
    return trim(v);
}

// Collect the table names visible on a connection: the data directory's free
// tables (.dbf / .adt) plus any dictionary-managed tables (system.tables),
// deduplicated case-insensitively.
std::vector<std::string> list_tables(DbcH* dbc) {
    std::vector<std::string> out;
    auto known = [&](const std::string& n) {
        std::string l = lower(n);
        for (const std::string& e : out) if (lower(e) == l) return true;
        return false;
    };
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!dbc->datadir.empty()) {
        for (fs::directory_iterator it(dbc->datadir, ec), end; !ec && it != end;
             it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string ext = lower(it->path().extension().string());
            if (ext == ".dbf" || ext == ".adt") {
                std::string base = it->path().stem().string();
                if (!base.empty() && !known(base)) out.push_back(base);
            }
        }
    }
    openads_stmt* q = nullptr;
    if (openads_exec_direct(dbc->conn, "SELECT Name FROM system.tables", &q)
        == OPENADS_OK) {
        while (openads_fetch_next(q) == OPENADS_OK) {
            std::string n = cell(q, 1);
            if (!n.empty() && !known(n)) out.push_back(n);
        }
        openads_finalize(q);
    }
    return out;
}

} // namespace

extern "C" {

SQLRETURN SQL_API SQLAllocHandle(SQLSMALLINT type, SQLHANDLE input,
                                 SQLHANDLE* out) {
    if (!out) return SQL_ERROR;
    *out = SQL_NULL_HANDLE;
    switch (type) {                       // nothrow: no exception leaves extern "C"
        case SQL_HANDLE_ENV: {
            auto* e = new (std::nothrow) EnvH();
            if (!e) return SQL_ERROR;
            *out = e; return SQL_SUCCESS;
        }
        case SQL_HANDLE_DBC: {
            if (!input) return SQL_ERROR;
            auto* d = new (std::nothrow) DbcH();
            if (!d) return SQL_ERROR;
            *out = d; return SQL_SUCCESS;
        }
        case SQL_HANDLE_STMT: {
            if (!input) return SQL_ERROR;
            auto* s = new (std::nothrow) StmtH();
            if (!s) return SQL_ERROR;
            s->dbc = reinterpret_cast<DbcH*>(input);
            *out = s; return SQL_SUCCESS;
        }
        default: return SQL_ERROR;
    }
}

SQLRETURN SQL_API SQLSetEnvAttr(SQLHENV, SQLINTEGER, SQLPOINTER, SQLINTEGER) {
    return SQL_SUCCESS;
}
SQLRETURN SQL_API SQLSetConnectAttr(SQLHDBC, SQLINTEGER, SQLPOINTER, SQLINTEGER) {
    return SQL_SUCCESS;
}
SQLRETURN SQL_API SQLGetConnectAttr(SQLHDBC, SQLINTEGER, SQLPOINTER, SQLINTEGER,
                                    SQLINTEGER* out) {
    if (out) *out = 0;
    return SQL_SUCCESS;
}
SQLRETURN SQL_API SQLSetStmtAttr(SQLHSTMT, SQLINTEGER, SQLPOINTER, SQLINTEGER) {
    return SQL_SUCCESS;
}
SQLRETURN SQL_API SQLGetStmtAttr(SQLHSTMT, SQLINTEGER, SQLPOINTER, SQLINTEGER,
                                 SQLINTEGER* out) {
    if (out) *out = 0;
    return SQL_SUCCESS;
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
    dbc->datadir = dir;
    copy_out(outconn, outmax, cs, outlen);
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLConnect(SQLHDBC hdbc, SQLCHAR* dsn, SQLSMALLINT dsnlen,
                             SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT) {
    if (!hdbc) return SQL_ERROR;
    auto* dbc = reinterpret_cast<DbcH*>(hdbc);
    std::string dir = to_string(dsn, dsnlen);
    openads_conn* c = nullptr;
    if (openads_connect(dir.c_str(), "local", nullptr, nullptr, &c) != OPENADS_OK) {
        dbc->err = "connect failed";
        return SQL_ERROR;
    }
    dbc->conn = c;
    dbc->datadir = dir;
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLExecDirect(SQLHSTMT hstmt, SQLCHAR* sql, SQLINTEGER len) {
    if (!hstmt) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    if (!s->dbc || !s->dbc->conn) return SQL_ERROR;
    reset_result(s);
    std::string q = to_string(sql, len);
    openads_stmt* st = nullptr;
    if (openads_exec_direct(s->dbc->conn, q.c_str(), &st) != OPENADS_OK) {
        s->err = "exec failed";
        return SQL_ERROR;
    }
    s->st = st;
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLPrepare(SQLHSTMT hstmt, SQLCHAR* sql, SQLINTEGER len) {
    if (!hstmt) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    if (!s->dbc || !s->dbc->conn) return SQL_ERROR;
    reset_result(s);
    std::string q = to_string(sql, len);
    openads_stmt* st = nullptr;
    if (openads_prepare(s->dbc->conn, q.c_str(), &st) != OPENADS_OK) {
        s->err = "prepare failed";
        return SQL_ERROR;
    }
    s->st = st;
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLExecute(SQLHSTMT hstmt) {
    if (!hstmt) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    if (!s->st) return SQL_ERROR;
    return openads_execute(s->st) == OPENADS_OK ? SQL_SUCCESS : SQL_ERROR;
}

SQLRETURN SQL_API SQLNumResultCols(SQLHSTMT hstmt, SQLSMALLINT* count) {
    if (!hstmt || !count) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    if (s->synth) { *count = static_cast<SQLSMALLINT>(s->scols.size()); return SQL_SUCCESS; }
    int n = 0;
    *count = (s->st && openads_num_cols(s->st, &n) == OPENADS_OK)
                 ? static_cast<SQLSMALLINT>(n) : 0;
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLDescribeCol(SQLHSTMT hstmt, SQLUSMALLINT col, SQLCHAR* name,
                                 SQLSMALLINT namemax, SQLSMALLINT* namelen,
                                 SQLSMALLINT* dtype, SQLULEN* dsize,
                                 SQLSMALLINT* ddec, SQLSMALLINT* dnull) {
    if (!hstmt) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    if (s->synth) {
        if (col < 1 || static_cast<size_t>(col) > s->scols.size()) return SQL_ERROR;
        copy_out(name, namemax, s->scols[col - 1], namelen);
    } else {
        if (!s->st) return SQL_ERROR;
        if (name && namemax > 0) {
            if (openads_col_name(s->st, static_cast<int>(col),
                                 reinterpret_cast<char*>(name),
                                 static_cast<size_t>(namemax)) != OPENADS_OK)
                return SQL_ERROR;
            if (namelen)
                *namelen = static_cast<SQLSMALLINT>(
                    std::strlen(reinterpret_cast<char*>(name)));
        } else if (namelen) *namelen = 0;
    }
    SQLSMALLINT sqltype = SQL_VARCHAR;   // catalog (synth) columns stay char
    if (!s->synth && s->st) {
        int at = 0;
        if (openads_col_type(s->st, static_cast<int>(col), &at) == OPENADS_OK)
            sqltype = ads_to_sql_type(at);
    }
    if (dtype) *dtype = sqltype;
    if (dsize) *dsize = 255;
    if (ddec)  *ddec  = 0;
    if (dnull) *dnull = SQL_NULLABLE_UNKNOWN;
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLColAttribute(SQLHSTMT hstmt, SQLUSMALLINT col,
                                  SQLUSMALLINT field, SQLPOINTER charattr,
                                  SQLSMALLINT bufmax, SQLSMALLINT* strlen,
                                  SQLLEN* numattr) {
    if (!hstmt) return SQL_ERROR;
    switch (field) {
        case SQL_DESC_NAME:
        case SQL_DESC_LABEL:
            return SQLDescribeCol(hstmt, col,
                                  reinterpret_cast<SQLCHAR*>(charattr), bufmax,
                                  strlen, nullptr, nullptr, nullptr, nullptr);
        case SQL_DESC_TYPE:
        case SQL_DESC_CONCISE_TYPE: {
            SQLSMALLINT sqltype = SQL_VARCHAR;
            auto* s = reinterpret_cast<StmtH*>(hstmt);
            if (!s->synth && s->st) {
                int at = 0;
                if (openads_col_type(s->st, static_cast<int>(col), &at) == OPENADS_OK)
                    sqltype = ads_to_sql_type(at);
            }
            if (numattr) *numattr = sqltype;
            return SQL_SUCCESS;
        }
        case SQL_DESC_LENGTH:
        case SQL_DESC_OCTET_LENGTH:
        case SQL_DESC_DISPLAY_SIZE:
            if (numattr) *numattr = 255;
            return SQL_SUCCESS;
        default:
            if (numattr) *numattr = 0;
            if (strlen)  *strlen = 0;
            return SQL_SUCCESS;
    }
}

SQLRETURN SQL_API SQLFetch(SQLHSTMT hstmt) {
    if (!hstmt) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    if (s->synth) {
        if (static_cast<size_t>(s->spos) >= s->srows.size()) return SQL_NO_DATA;
        s->spos += 1;
        return SQL_SUCCESS;
    }
    if (!s->st) return SQL_ERROR;
    int rc = openads_fetch_next(s->st);
    if (rc == OPENADS_OK) return SQL_SUCCESS;
    if (rc == OPENADS_NO_DATA) return SQL_NO_DATA;
    return SQL_ERROR;
}

SQLRETURN SQL_API SQLGetData(SQLHSTMT hstmt, SQLUSMALLINT col, SQLSMALLINT ctype,
                             SQLPOINTER buf, SQLLEN buflen, SQLLEN* ind) {
    if (!hstmt || !buf || buflen <= 0) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    if (s->synth) {
        if (s->spos < 1 || static_cast<size_t>(s->spos) > s->srows.size())
            return SQL_ERROR;
        if (col < 1 || static_cast<size_t>(col) > s->scols.size()) return SQL_ERROR;
        const std::string& v = s->srows[static_cast<size_t>(s->spos) - 1]
                                       [static_cast<size_t>(col) - 1];
        return put_typed(v, ctype, buf, buflen, ind);
    }
    if (!s->st) return SQL_ERROR;
    // Character target: read straight into the caller's buffer so large values
    // keep their truncation semantics.
    if (ctype == SQL_C_CHAR) {
        size_t n = 0;
        if (openads_get_str(s->st, static_cast<int>(col),
                            reinterpret_cast<char*>(buf),
                            static_cast<size_t>(buflen), &n) != OPENADS_OK)
            return SQL_ERROR;
        if (ind) *ind = static_cast<SQLLEN>(n);
        return SQL_SUCCESS;
    }
    // Typed target: materialise the value as text, then convert.
    char tmp[256];
    size_t n = 0;
    if (openads_get_str(s->st, static_cast<int>(col), tmp, sizeof(tmp), &n)
        != OPENADS_OK)
        return SQL_ERROR;
    return put_typed(std::string(tmp, n), ctype, buf, buflen, ind);
}

SQLRETURN SQL_API SQLRowCount(SQLHSTMT hstmt, SQLLEN* count) {
    if (!hstmt || !count) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    *count = -1;
    long rc = 0;
    if (s->st && openads_row_count(s->st, &rc) == OPENADS_OK)
        *count = static_cast<SQLLEN>(rc);
    else if (s->synth)
        *count = static_cast<SQLLEN>(s->srows.size());
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLMoreResults(SQLHSTMT) { return SQL_NO_DATA; }

// ---- catalog functions ---------------------------------------------------

SQLRETURN SQL_API SQLTables(SQLHSTMT hstmt, SQLCHAR*, SQLSMALLINT, SQLCHAR*,
                            SQLSMALLINT, SQLCHAR*, SQLSMALLINT, SQLCHAR*,
                            SQLSMALLINT) {
    if (!hstmt) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    if (!s->dbc || !s->dbc->conn) return SQL_ERROR;
    reset_result(s);
    s->synth = true;
    s->scols = {"TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "TABLE_TYPE", "REMARKS"};
    for (const std::string& t : list_tables(s->dbc))
        s->srows.push_back({"", "", t, "TABLE", ""});
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLColumns(SQLHSTMT hstmt, SQLCHAR*, SQLSMALLINT, SQLCHAR*,
                             SQLSMALLINT, SQLCHAR* table, SQLSMALLINT tablelen,
                             SQLCHAR*, SQLSMALLINT) {
    if (!hstmt) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    if (!s->dbc || !s->dbc->conn) return SQL_ERROR;
    reset_result(s);
    s->synth = true;
    s->scols = {"TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "COLUMN_NAME",
                "DATA_TYPE", "TYPE_NAME", "COLUMN_SIZE", "BUFFER_LENGTH",
                "DECIMAL_DIGITS", "NUM_PREC_RADIX", "NULLABLE", "REMARKS",
                "ORDINAL_POSITION"};
    std::string want = trim(to_string(table, tablelen));
    std::vector<std::string> tables =
        want.empty() ? list_tables(s->dbc) : std::vector<std::string>{want};
    const std::string vtype  = std::to_string(static_cast<int>(SQL_VARCHAR));
    const std::string vnull  = std::to_string(static_cast<int>(SQL_NULLABLE_UNKNOWN));
    for (const std::string& t : tables) {
        openads_stmt* q = nullptr;
        std::string sel = "SELECT * FROM " + t;
        if (openads_exec_direct(s->dbc->conn, sel.c_str(), &q) != OPENADS_OK)
            continue;
        int nc = 0;
        openads_num_cols(q, &nc);
        for (int c = 1; c <= nc; ++c) {
            char cn[256];
            if (openads_col_name(q, c, cn, sizeof(cn)) != OPENADS_OK) continue;
            s->srows.push_back({"", "", t, std::string(cn), vtype, "VARCHAR",
                                "255", "255", "0", "10", vnull, "",
                                std::to_string(c)});
        }
        openads_finalize(q);
    }
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLPrimaryKeys(SQLHSTMT hstmt, SQLCHAR*, SQLSMALLINT,
                                 SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT) {
    if (!hstmt) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    reset_result(s);
    s->synth = true;
    s->scols = {"TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "COLUMN_NAME",
                "KEY_SEQ", "PK_NAME"};
    return SQL_SUCCESS;   // empty set (PK reporting is a later slice)
}

SQLRETURN SQL_API SQLGetTypeInfo(SQLHSTMT hstmt, SQLSMALLINT) {
    if (!hstmt) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    reset_result(s);
    s->synth = true;
    s->scols = {"TYPE_NAME", "DATA_TYPE", "COLUMN_SIZE", "NULLABLE",
                "CASE_SENSITIVE", "SEARCHABLE"};
    s->srows.push_back({"VARCHAR",
                        std::to_string(static_cast<int>(SQL_VARCHAR)),
                        "255",
                        std::to_string(static_cast<int>(SQL_NULLABLE)),
                        "0",
                        std::to_string(static_cast<int>(SQL_SEARCHABLE))});
    return SQL_SUCCESS;
}

// ---- info / lifecycle ----------------------------------------------------

SQLRETURN SQL_API SQLGetInfo(SQLHDBC, SQLUSMALLINT type, SQLPOINTER buf,
                             SQLSMALLINT bufmax, SQLSMALLINT* strlen) {
    auto put = [&](const char* v) {
        copy_out(reinterpret_cast<SQLCHAR*>(buf), bufmax, v, strlen);
    };
    switch (type) {
        case SQL_DBMS_NAME:             put("OpenADS");      return SQL_SUCCESS;
        case SQL_DBMS_VER:              put("01.00.0000");   return SQL_SUCCESS;
        case SQL_DRIVER_NAME:           put("openads_odbc"); return SQL_SUCCESS;
        case SQL_DRIVER_VER:            put("01.00.0000");   return SQL_SUCCESS;
        case SQL_IDENTIFIER_QUOTE_CHAR: put("\"");           return SQL_SUCCESS;
        case SQL_CATALOG_NAME_SEPARATOR:put(".");            return SQL_SUCCESS;
        case SQL_SEARCH_PATTERN_ESCAPE: put("\\");           return SQL_SUCCESS;
        case SQL_CATALOG_NAME:          put("N");            return SQL_SUCCESS;
        default:
            if (buf && bufmax >= static_cast<SQLSMALLINT>(sizeof(SQLUSMALLINT)))
                std::memset(buf, 0, sizeof(SQLUSMALLINT));
            if (strlen) *strlen = 0;
            return SQL_SUCCESS;
    }
}

SQLRETURN SQL_API SQLEndTran(SQLSMALLINT, SQLHANDLE, SQLSMALLINT) {
    return SQL_SUCCESS;   // autocommit model in this slice
}

SQLRETURN SQL_API SQLFreeStmt(SQLHSTMT hstmt, SQLUSMALLINT option) {
    if (!hstmt) return SQL_ERROR;
    auto* s = reinterpret_cast<StmtH*>(hstmt);
    if (option == SQL_CLOSE || option == SQL_UNBIND || option == SQL_RESET_PARAMS)
        reset_result(s);
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLFreeHandle(SQLSMALLINT type, SQLHANDLE h) {
    if (!h) return SQL_ERROR;
    switch (type) {
        case SQL_HANDLE_STMT: {
            auto* s = reinterpret_cast<StmtH*>(h);
            if (s->st) openads_finalize(s->st);
            delete s; return SQL_SUCCESS;
        }
        case SQL_HANDLE_DBC: {
            auto* d = reinterpret_cast<DbcH*>(h);
            if (d->conn) openads_disconnect(d->conn);
            delete d; return SQL_SUCCESS;
        }
        case SQL_HANDLE_ENV:
            delete reinterpret_cast<EnvH*>(h); return SQL_SUCCESS;
        default: return SQL_ERROR;
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
