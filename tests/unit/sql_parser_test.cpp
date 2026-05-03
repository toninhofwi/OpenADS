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

TEST_CASE("parse_select recognises a single-equality WHERE clause") {
    auto r = parse_select("SELECT * FROM data.dbf WHERE TAG = 'BAR'");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "data.dbf");
    REQUIRE(r.value().where.has_value());
    CHECK(r.value().where->column  == "TAG");
    CHECK(r.value().where->literal == "BAR");
}

TEST_CASE("parse_select WHERE accepts case-insensitive keyword and tight whitespace") {
    auto r = parse_select("select * from x where  Name='Anna'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where.has_value());
    CHECK(r.value().where->column  == "Name");
    CHECK(r.value().where->literal == "Anna");
}

TEST_CASE("parse_select WHERE rejects non-equality operators") {
    auto r = parse_select("SELECT * FROM x WHERE TAG > 'A'");
    CHECK_FALSE(r.has_value());
    CHECK(r.error().code == 7200);
}

TEST_CASE("parse_select WHERE rejects unterminated string literal") {
    auto r = parse_select("SELECT * FROM x WHERE TAG = 'oops");
    CHECK_FALSE(r.has_value());
    CHECK(r.error().code == 7200);
}
