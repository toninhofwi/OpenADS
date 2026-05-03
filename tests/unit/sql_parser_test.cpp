#include "doctest.h"
#include "sql/parser.h"

using openads::sql::parse_select;

TEST_CASE("parse_select recognises SELECT * FROM <table>") {
    auto r = parse_select("SELECT * FROM clientes");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "clientes");
}

TEST_CASE("parse_select is case-insensitive on keywords") {
    auto r = parse_select("select * from VENTAS");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "VENTAS");
}

TEST_CASE("parse_select tolerates trailing semicolon and whitespace") {
    auto r = parse_select("   SELECT  *  FROM   data.dbf  ;  ");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "data.dbf");
}

TEST_CASE("parse_select rejects projection lists in M7.1") {
    auto r = parse_select("SELECT name FROM x");
    CHECK_FALSE(r.has_value());
    CHECK(r.error().code == 7200);
}

TEST_CASE("parse_select reports missing FROM") {
    auto r = parse_select("SELECT *");
    CHECK_FALSE(r.has_value());
    CHECK(r.error().code == 7200);
}
