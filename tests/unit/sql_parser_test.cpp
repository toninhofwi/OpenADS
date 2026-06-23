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

// --- ADS dialect compatibility (feat/sql-ads-dialect) ---------------------
// SAP ADS lets apps write SQL with constructs the strict-SQL parser used to
// reject. These three keep that legacy syntax parsing without forcing a
// rewrite. All three appear together in real ADS queries like:
//   SELECT {static} * FROM [articulo.dat] AS a WHERE ... ORDER BY a.col

TEST_CASE("ADS dialect: {static} cursor hint after SELECT is ignored") {
    auto r = parse_select("SELECT {static} * FROM articulo");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "articulo");
    CHECK(r.value().projection.empty());   // `*` => no explicit projection
}

TEST_CASE("ADS dialect: {static} hint with a projection list") {
    auto r = parse_select("SELECT {static} name, age FROM x");
    REQUIRE(r.has_value());
    REQUIRE(r.value().projection.size() == 2);
    CHECK(r.value().projection[0] == "name");
    CHECK(r.value().projection[1] == "age");
}

TEST_CASE("ADS dialect: bracketed table/file name in FROM") {
    auto r = parse_select("SELECT * FROM [articulo.dat]");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "articulo.dat");
}

TEST_CASE("ADS dialect: FROM <table> AS <alias>") {
    auto r = parse_select("SELECT * FROM articulo AS a WHERE a.cnombre <> 'N'");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "articulo");
    CHECK(r.value().table_alias == "a");
    // The WHERE survives the alias and resolves the qualified column.
    REQUIRE(r.value().where != nullptr);
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->cmp.column == "cnombre");
    CHECK(r.value().where->cmp.op == WhereOp::Ne);
}

TEST_CASE("ADS dialect: UPPER(col) on the WHERE left-hand side") {
    auto r = parse_select("SELECT * FROM x WHERE UPPER(name) <> 'N'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where != nullptr);
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->cmp.column == "name");
    CHECK(r.value().where->cmp.op == WhereOp::Ne);
    CHECK(r.value().where->cmp.lhs_fn == openads::sql::WhereFn::Upper);
    CHECK(r.value().where->cmp.literal == "N");
}

TEST_CASE("ADS dialect: LOWER(col) on the WHERE left-hand side") {
    auto r = parse_select("SELECT * FROM x WHERE LOWER(a.tag) = 'n'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->cmp.column == "tag");   // alias dropped
    CHECK(r.value().where->cmp.lhs_fn == openads::sql::WhereFn::Lower);
    CHECK(r.value().where->cmp.op == WhereOp::Eq);
}

TEST_CASE("ADS dialect: bare column LHS keeps lhs_fn None") {
    auto r = parse_select("SELECT * FROM x WHERE tag = 'a'");
    REQUIRE(r.has_value());
    CHECK(r.value().where->cmp.lhs_fn == openads::sql::WhereFn::None);
}

TEST_CASE("ADS dialect: UPPER(col) LIKE pattern in WHERE") {
    auto r = parse_select("SELECT * FROM x WHERE UPPER(name) LIKE 'A%'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->cmp.column == "name");
    CHECK(r.value().where->cmp.lhs_fn == openads::sql::WhereFn::Upper);
    CHECK(r.value().where->cmp.op == WhereOp::Like);
}

TEST_CASE("ADS dialect: full legacy query (hint + bracket + alias)") {
    auto r = parse_select(
        "SELECT {static} * FROM [articulo.dat] AS a "
        "WHERE a.crefhabart <> 'N' AND a.cnombreart LIKE 'A%' "
        "ORDER BY a.cnombreart");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "articulo.dat");
    CHECK(r.value().table_alias == "a");
    REQUIRE(r.value().where != nullptr);
    REQUIRE(r.value().order_by.has_value());
    CHECK(r.value().order_by.value().column == "cnombreart");
}
