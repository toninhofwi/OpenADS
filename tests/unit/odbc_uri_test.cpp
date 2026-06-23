#include "doctest.h"

#include "sql_backend/odbc_uri.h"

using openads::sql_backend::OdbcUri;
using openads::sql_backend::parse_odbc_uri;

TEST_CASE("odbc URI: scheme stripping passes the connection string verbatim") {
    OdbcUri u;
    REQUIRE(parse_odbc_uri("odbc://Driver={Some Driver};DBQ=C:/a.accdb;", u));
    CHECK(u.connstr == "Driver={Some Driver};DBQ=C:/a.accdb;");

    OdbcUri u2;
    REQUIRE(parse_odbc_uri("odbc:DSN=mydsn;UID=u;PWD=p", u2));
    CHECK(u2.connstr == "DSN=mydsn;UID=u;PWD=p");
}

TEST_CASE("odbc URI: rejects non-odbc schemes and empty connection strings") {
    OdbcUri u;
    CHECK_FALSE(parse_odbc_uri("mariadb://host/db", u));
    CHECK_FALSE(parse_odbc_uri("/local/data/dir", u));
    CHECK_FALSE(parse_odbc_uri("odbc://", u));
    CHECK_FALSE(parse_odbc_uri("odbc:", u));
}
