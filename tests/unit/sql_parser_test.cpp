#include "doctest.h"
#include "sql/parser.h"

#include <vector>

using openads::sql::parse_select;
using openads::sql::WhereExpr;
using openads::sql::WhereOp;

namespace {

// Walk a left-skewed AND tree and flatten it to its Cmp leaves so the
// tests can assert against indexed positions (matching the old vector
// shape).
void collect_and_leaves(const WhereExpr* node,
                        std::vector<const openads::sql::WhereCmp*>& out) {
    if (node == nullptr) return;
    if (node->kind == WhereExpr::Kind::And) {
        for (auto& c : node->children) collect_and_leaves(c.get(), out);
    } else if (node->kind == WhereExpr::Kind::Cmp) {
        out.push_back(&node->cmp);
    }
}

}  // namespace

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

TEST_CASE("parse_select accepts projection lists (M10.8)") {
    auto r = parse_select("SELECT name, age FROM x");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "x");
    REQUIRE(r.value().projection.size() == 2);
    CHECK(r.value().projection[0] == "name");
    CHECK(r.value().projection[1] == "age");
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
    REQUIRE(r.value().where != nullptr);
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->cmp.column  == "TAG");
    CHECK(r.value().where->cmp.literal == "BAR");
}

TEST_CASE("parse_select WHERE accepts case-insensitive keyword and tight whitespace") {
    auto r = parse_select("select * from x where  Name='Anna'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where != nullptr);
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->cmp.column  == "Name");
    CHECK(r.value().where->cmp.literal == "Anna");
}

TEST_CASE("parse_select WHERE supports AND-joined comparisons") {
    auto r = parse_select(
        "SELECT * FROM x WHERE A = 'foo' AND B != 'bar' AND C >= 'z'");
    REQUIRE(r.has_value());
    std::vector<const openads::sql::WhereCmp*> leaves;
    collect_and_leaves(r.value().where.get(), leaves);
    REQUIRE(leaves.size() == 3);
    CHECK(leaves[0]->column == "A");
    CHECK(leaves[0]->op     == WhereOp::Eq);
    CHECK(leaves[1]->column == "B");
    CHECK(leaves[1]->op     == WhereOp::Ne);
    CHECK(leaves[2]->column == "C");
    CHECK(leaves[2]->op     == WhereOp::Ge);
}

TEST_CASE("parse_select WHERE accepts each comparison operator") {
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
        REQUIRE(r.value().where != nullptr);
        REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
        CHECK(r.value().where->cmp.op == tc.expected);
    }
}

TEST_CASE("parse_select WHERE rejects unterminated string literal") {
    auto r = parse_select("SELECT * FROM x WHERE TAG = 'oops");
    CHECK_FALSE(r.has_value());
    CHECK(r.error().code == 7200);
}

TEST_CASE("parse_select WHERE decodes doubled '' as an escaped quote") {
    auto r = parse_select("SELECT * FROM x WHERE NAME = 'O''Brien'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where != nullptr);
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->cmp.literal == "O'Brien");
}

TEST_CASE("M10.3 parse_select WHERE supports OR + parens") {
    auto r = parse_select(
        "SELECT * FROM x WHERE (A = 'a' OR B = 'b') AND C = 'c'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where != nullptr);
    REQUIRE(r.value().where->kind == WhereExpr::Kind::And);
    REQUIRE(r.value().where->children.size() == 2);
    CHECK(r.value().where->children[0]->kind == WhereExpr::Kind::Or);
    CHECK(r.value().where->children[1]->kind == WhereExpr::Kind::Cmp);
}

TEST_CASE("M10.3 parse_select WHERE supports NOT") {
    auto r = parse_select("SELECT * FROM x WHERE NOT TAG = 'a'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Not);
    CHECK(r.value().where->child->kind == WhereExpr::Kind::Cmp);
}

TEST_CASE("M10.3 parse_select WHERE accepts numeric literals") {
    auto r = parse_select("SELECT * FROM x WHERE AGE >= 18");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->cmp.is_numeric);
    CHECK(r.value().where->cmp.number == doctest::Approx(18));
    CHECK(r.value().where->cmp.op == WhereOp::Ge);
}
