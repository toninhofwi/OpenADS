#include "doctest.h"

#include "sql_backend/oracle_uri.h"

using openads::sql_backend::OracleUri;
using openads::sql_backend::oracle_to_odbc_connstr;
using openads::sql_backend::parse_oracle_uri;

TEST_CASE("oracle URI: user:pass@host/service") {
    OracleUri u;
    REQUIRE(parse_oracle_uri("oracle://scott:tiger@dbhost/ORCL", u));
    CHECK(u.user == "scott");
    CHECK(u.password == "tiger");
    CHECK(u.host == "dbhost");
    CHECK(u.port == 1521);
    CHECK(u.service == "ORCL");
}

TEST_CASE("oracle URI: explicit port and percent-encoded credentials") {
    OracleUri u;
    REQUIRE(parse_oracle_uri(
        "oracle://user%40corp:p%40ss@10.0.0.5:1522/XEPDB1", u));
    CHECK(u.user == "user@corp");
    CHECK(u.password == "p@ss");
    CHECK(u.host == "10.0.0.5");
    CHECK(u.port == 1522);
    CHECK(u.service == "XEPDB1");
}

TEST_CASE("oracle URI: password-less user@host/service") {
    OracleUri u;
    REQUIRE(parse_oracle_uri("oracle://SYS@localhost/XE", u));
    CHECK(u.user == "SYS");
    CHECK(u.password.empty());
    CHECK(u.host == "localhost");
    CHECK(u.port == 1521);
    CHECK(u.service == "XE");
}

TEST_CASE("oracle URI: maps to Oracle ODBC connection string") {
    OracleUri u;
    REQUIRE(parse_oracle_uri("oracle://u:p@host:1521/SVC", u));
    CHECK(oracle_to_odbc_connstr(u) ==
          "DRIVER={Oracle ODBC Driver};DBQ=//host:1521/SVC;UID=u;PWD=p");
}

TEST_CASE("oracle URI: rejects non-oracle schemes and malformed URIs") {
    OracleUri u;
    CHECK_FALSE(parse_oracle_uri("odbc://Driver={Oracle ODBC Driver}", u));
    CHECK_FALSE(parse_oracle_uri("postgresql://host/db", u));
    CHECK_FALSE(parse_oracle_uri("oracle://user@host", u));
    CHECK_FALSE(parse_oracle_uri("oracle://", u));
    CHECK_FALSE(parse_oracle_uri("", u));
}