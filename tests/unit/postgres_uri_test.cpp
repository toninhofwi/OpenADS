#include "doctest.h"

#include "sql_backend/postgres_uri.h"

using openads::sql_backend::PostgresUri;
using openads::sql_backend::parse_postgres_uri;

TEST_CASE("postgres URI: postgresql:// scheme passes verbatim") {
    PostgresUri u;
    REQUIRE(parse_postgres_uri("postgresql://user:pass@host:5432/mydb", u));
    CHECK(u.conninfo == "postgresql://user:pass@host:5432/mydb");
}

TEST_CASE("postgres URI: postgres:// scheme normalizes to postgresql://") {
    PostgresUri u;
    REQUIRE(parse_postgres_uri("postgres://user:pass@host/db", u));
    CHECK(u.conninfo == "postgresql://user:pass@host/db");
}

TEST_CASE("postgres URI: pgsql:// scheme normalizes to postgresql://") {
    PostgresUri u;
    REQUIRE(parse_postgres_uri("pgsql://admin:secret@db.local:5433/mydb", u));
    CHECK(u.conninfo == "postgresql://admin:secret@db.local:5433/mydb");
}

TEST_CASE("postgres URI: bare host with no credentials") {
    PostgresUri u;
    REQUIRE(parse_postgres_uri("postgresql://dbhost/mydb", u));
    CHECK(u.conninfo == "postgresql://dbhost/mydb");
}

TEST_CASE("postgres URI: empty path after scheme is accepted") {
    PostgresUri u;
    REQUIRE(parse_postgres_uri("postgresql://", u));
    CHECK(u.conninfo == "postgresql://");
}

TEST_CASE("postgres URI: minimal valid URI") {
    PostgresUri u;
    REQUIRE(parse_postgres_uri("postgresql://localhost", u));
    CHECK(u.conninfo == "postgresql://localhost");
}

TEST_CASE("postgres URI: rejects non-postgres schemes") {
    PostgresUri u;
    CHECK_FALSE(parse_postgres_uri("sqlite:///tmp/db.sqlite", u));
    CHECK_FALSE(parse_postgres_uri("mariadb://host/db", u));
    CHECK_FALSE(parse_postgres_uri("mysql://host/db", u));
    CHECK_FALSE(parse_postgres_uri("odbc://Driver={x}", u));
    CHECK_FALSE(parse_postgres_uri("/local/path", u));
    CHECK_FALSE(parse_postgres_uri("postgres", u));
}

TEST_CASE("postgres URI: rejects empty string") {
    PostgresUri u;
    CHECK_FALSE(parse_postgres_uri("", u));
}

TEST_CASE("postgres URI: preserves query string and fragments") {
    PostgresUri u;
    REQUIRE(parse_postgres_uri(
        "postgresql://host/db?sslmode=require&connect_timeout=10", u));
    CHECK(u.conninfo ==
          "postgresql://host/db?sslmode=require&connect_timeout=10");
}
