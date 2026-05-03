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
    REQUIRE(r.value().where.size() == 1);
    CHECK(r.value().where[0].column  == "TAG");
    CHECK(r.value().where[0].literal == "BAR");
}

TEST_CASE("parse_select WHERE accepts case-insensitive keyword and tight whitespace") {
    auto r = parse_select("select * from x where  Name='Anna'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where.size() == 1);
    CHECK(r.value().where[0].column  == "Name");
    CHECK(r.value().where[0].literal == "Anna");
}

TEST_CASE("parse_select WHERE supports AND-joined comparisons") {
    auto r = parse_select(
        "SELECT * FROM x WHERE A = 'foo' AND B != 'bar' AND C >= 'z'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where.size() == 3);
    CHECK(r.value().where[0].column  == "A");
    CHECK(r.value().where[0].op      == openads::sql::WhereOp::Eq);
    CHECK(r.value().where[1].column  == "B");
    CHECK(r.value().where[1].op      == openads::sql::WhereOp::Ne);
    CHECK(r.value().where[2].column  == "C");
    CHECK(r.value().where[2].op      == openads::sql::WhereOp::Ge);
}

TEST_CASE("parse_select WHERE accepts each comparison operator") {
    using openads::sql::WhereOp;
    struct Case { const char* op; WhereOp expected; };
    Case cases[] = {
        {"=",  WhereOp::Eq},
        {"!=", WhereOp::Ne},
        {"<>", WhereOp::Ne},
        {"<",  WhereOp::Lt},
        {">",  WhereOp::Gt},
        {"<=", WhereOp::Le},
        {">=", WhereOp::Ge},
    };
    for (const auto& tc : cases) {
        std::string sql = std::string("SELECT * FROM x WHERE TAG ") + tc.op + " 'A'";
        auto r = parse_select(sql);
        REQUIRE(r.has_value());
        REQUIRE(r.value().where.size() == 1);
        CHECK(r.value().where[0].op == tc.expected);
    }
}

TEST_CASE("parse_select WHERE rejects unterminated string literal") {
    auto r = parse_select("SELECT * FROM x WHERE TAG = 'oops");
    CHECK_FALSE(r.has_value());
    CHECK(r.error().code == 7200);
}
