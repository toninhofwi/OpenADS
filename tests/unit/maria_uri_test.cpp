#include "doctest.h"

#include "sql_backend/maria_uri.h"

using openads::sql_backend::MariaUri;
using openads::sql_backend::parse_maria_uri;

TEST_CASE("mariadb URI: basic authority + database") {
    MariaUri u;
    REQUIRE(parse_maria_uri("mariadb://localhost/mydb", u));
    CHECK(u.host == "localhost");
    CHECK(u.port == 3306);
    CHECK(u.database == "mydb");
    CHECK(u.user.empty());
    CHECK(u.password.empty());
}

TEST_CASE("mariadb URI: inline credentials") {
    MariaUri u;
    REQUIRE(parse_maria_uri("mariadb://root:secret@dbhost:3307/appdb", u));
    CHECK(u.host == "dbhost");
    CHECK(u.port == 3307);
    CHECK(u.user == "root");
    CHECK(u.password == "secret");
    CHECK(u.database == "appdb");
}

TEST_CASE("mariadb URI: user without password") {
    MariaUri u;
    REQUIRE(parse_maria_uri("mariadb://admin@localhost/testdb", u));
    CHECK(u.host == "localhost");
    CHECK(u.user == "admin");
    CHECK(u.password.empty());
    CHECK(u.database == "testdb");
}

TEST_CASE("mariadb URI: mysql:// scheme") {
    MariaUri u;
    REQUIRE(parse_maria_uri("mysql://root:pwd@localhost/mydb", u));
    CHECK(u.host == "localhost");
    CHECK(u.user == "root");
    CHECK(u.password == "pwd");
    CHECK(u.database == "mydb");
}

TEST_CASE("mariadb URI: IPv6 host in brackets") {
    MariaUri u;
    REQUIRE(parse_maria_uri("mariadb://[::1]:3306/mydb", u));
    CHECK(u.host == "::1");
    CHECK(u.port == 3306);
    CHECK(u.database == "mydb");
}

TEST_CASE("mariadb URI: IPv6 host without port") {
    MariaUri u;
    REQUIRE(parse_maria_uri("mariadb://[::1]/mydb", u));
    CHECK(u.host == "::1");
    CHECK(u.port == 3306);
    CHECK(u.database == "mydb");
}

TEST_CASE("mariadb URI: host without port (default 3306)") {
    MariaUri u;
    REQUIRE(parse_maria_uri("mariadb://dbhost/mydb", u));
    CHECK(u.host == "dbhost");
    CHECK(u.port == 3306);
}

TEST_CASE("mariadb URI: database with query string") {
    MariaUri u;
    REQUIRE(parse_maria_uri("mariadb://host/mydb?charset=utf8mb4", u));
    CHECK(u.host == "host");
    CHECK(u.database == "mydb");
}

TEST_CASE("mariadb URI: no database, only authority") {
    MariaUri u;
    REQUIRE(parse_maria_uri("mariadb://localhost", u));
    CHECK(u.host == "localhost");
    CHECK(u.database.empty());
}

TEST_CASE("mariadb URI: rejects non-mariadb/mysql schemes") {
    MariaUri u;
    CHECK_FALSE(parse_maria_uri("postgresql://host/db", u));
    CHECK_FALSE(parse_maria_uri("sqlite:///tmp/db", u));
    CHECK_FALSE(parse_maria_uri("odbc://Driver={x}", u));
    CHECK_FALSE(parse_maria_uri("/local/path", u));
}

TEST_CASE("mariadb URI: rejects empty string") {
    MariaUri u;
    CHECK_FALSE(parse_maria_uri("", u));
}

TEST_CASE("mariadb URI: rejects empty authority") {
    MariaUri u;
    CHECK_FALSE(parse_maria_uri("mariadb:///mydb", u));
}

TEST_CASE("mariadb URI: port with non-digit suffix treated as part of host") {
    MariaUri u;
    REQUIRE(parse_maria_uri("mariadb://host:abc/mydb", u));
    CHECK(u.host == "host:abc");
    CHECK(u.port == 3306);
}

TEST_CASE("mariadb URI: multiple colons in password") {
    MariaUri u;
    REQUIRE(parse_maria_uri("mariadb://user:p:a:s:s@host/db", u));
    CHECK(u.user == "user");
    CHECK(u.password == "p:a:s:s");
    CHECK(u.host == "host");
}

TEST_CASE("mariadb URI: empty password after colon") {
    MariaUri u;
    REQUIRE(parse_maria_uri("mariadb://user:@host/db", u));
    CHECK(u.user == "user");
    CHECK(u.password.empty());
    CHECK(u.host == "host");
}
