// Tests for the OpenADS ODBC driver (bindings/odbc/openads_odbc.cpp).
// Drives the driver's exported SQL* entry points directly (no Driver Manager,
// no registry) to prove a full SELECT round-trip against the native engine:
// connect via connection string -> CREATE/INSERT -> SELECT -> describe ->
// fetch -> SQLGetData.
#include "doctest.h"

#ifdef _WIN32
#  include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static void exec_ok(SQLHDBC dbc, const char* sql) {
    SQLHSTMT st = SQL_NULL_HSTMT;
    REQUIRE(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st) == SQL_SUCCESS);
    REQUIRE(SQLExecDirect(st, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql)),
                          SQL_NTS) == SQL_SUCCESS);
    SQLFreeHandle(SQL_HANDLE_STMT, st);
}

TEST_CASE("openads ODBC driver: SELECT round-trip") {
    auto dir = fs::temp_directory_path() / "openads_odbc_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    SQLHENV env = SQL_NULL_HENV;
    REQUIRE(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env) == SQL_SUCCESS);
    REQUIRE(SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION,
                          reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0)
            == SQL_SUCCESS);

    SQLHDBC dbc = SQL_NULL_HDBC;
    REQUIRE(SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc) == SQL_SUCCESS);

    std::string cs = "DRIVER={OpenADS};DataDir=" + dir.string()
                   + ";ServerType=local";
    REQUIRE(SQLDriverConnect(dbc, nullptr,
                             reinterpret_cast<SQLCHAR*>(const_cast<char*>(cs.c_str())),
                             SQL_NTS, nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT)
            == SQL_SUCCESS);

    exec_ok(dbc, "CREATE TABLE people (NAME Character(20), AGE Numeric(3,0))");
    exec_ok(dbc, "INSERT INTO people (NAME, AGE) VALUES ('alice', 30)");
    exec_ok(dbc, "INSERT INTO people (NAME, AGE) VALUES ('bob', 41)");

    SQLHSTMT st = SQL_NULL_HSTMT;
    REQUIRE(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st) == SQL_SUCCESS);
    REQUIRE(SQLExecDirect(st,
            reinterpret_cast<SQLCHAR*>(const_cast<char*>("SELECT NAME, AGE FROM people")),
            SQL_NTS) == SQL_SUCCESS);

    SQLSMALLINT ncols = 0;
    REQUIRE(SQLNumResultCols(st, &ncols) == SQL_SUCCESS);
    CHECK(ncols == 2);

    SQLCHAR cname[64] = {0};
    SQLSMALLINT nlen = 0, dtype = 0, ddec = 0, dnull = 0;
    SQLULEN dsize = 0;
    REQUIRE(SQLDescribeCol(st, 1, cname, sizeof(cname), &nlen,
                           &dtype, &dsize, &ddec, &dnull) == SQL_SUCCESS);
    CHECK(std::string(reinterpret_cast<char*>(cname)) == "NAME");

    int rows = 0;
    while (SQLFetch(st) == SQL_SUCCESS) {
        SQLCHAR val[64] = {0};
        SQLLEN ind = 0;
        REQUIRE(SQLGetData(st, 1, SQL_C_CHAR, val, sizeof(val), &ind)
                == SQL_SUCCESS);
        CHECK(ind > 0);
        ++rows;
    }
    CHECK(rows == 2);
    CHECK(SQLFetch(st) == SQL_NO_DATA);

    SQLFreeHandle(SQL_HANDLE_STMT, st);
    REQUIRE(SQLDisconnect(dbc) == SQL_SUCCESS);
    SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    SQLFreeHandle(SQL_HANDLE_ENV, env);
}
