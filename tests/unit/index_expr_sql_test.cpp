// tests/unit/index_expr_sql_test.cpp
// Tier-2 SQL push-down spike: try_emit_sql_where() translates the
// safely-translatable subset of an xBase FOR / SET FILTER predicate into a
// SQL WHERE fragment, and returns nullopt for anything it can't model (so the
// caller falls back to the row interpreter — push-down never changes results).
#include "doctest.h"
#include "engine/index_expr.h"

#include <optional>
#include <string>

using openads::engine::SqlDialect;
using openads::engine::try_emit_sql_where;

namespace {
std::string emit(const std::string& e) {
    auto r = try_emit_sql_where(e);
    REQUIRE(r.has_value());
    return *r;
}
}

TEST_CASE("SQL push-down: simple comparisons map to SQL operators") {
    CHECK(emit("QTY >= 100")        == "QTY >= 100");
    CHECK(emit("QTY > 5")           == "QTY > 5");
    CHECK(emit("X == 'a'")          == "X = 'a'");      // == -> =
    CHECK(emit("X = 'a'")           == "X = 'a'");
    CHECK(emit("QTY # 5")           == "QTY <> 5");     // xBase # -> <>
    CHECK(emit("QTY != 5")          == "QTY <> 5");     // xHarbour != -> <>
    CHECK(emit("QTY <> 5")          == "QTY <> 5");
    CHECK(emit("QTY <= 9")          == "QTY <= 9");
}

TEST_CASE("SQL push-down: logical operators and grouping") {
    CHECK(emit("QTY >= 100 .AND. NM = 'X'") == "(QTY >= 100 AND NM = 'X')");
    CHECK(emit("A = 1 .OR. B = 2")          == "(A = 1 OR B = 2)");
    CHECK(emit("(A = 1 .OR. B = 2) .AND. C = 3")
          == "((A = 1 OR B = 2) AND C = 3)");
    CHECK(emit(".NOT. A = 1")               == "(NOT A = 1)");
    CHECK(emit("! (A = 1)")                 == "(NOT A = 1)");
}

TEST_CASE("SQL push-down: scalar functions") {
    CHECK(emit("UPPER(NM) = 'ABC'")       == "UPPER(NM) = 'ABC'");
    CHECK(emit("LOWER(NM) = 'abc'")       == "LOWER(NM) = 'abc'");
    CHECK(emit("ALLTRIM(NM) = 'x'")       == "TRIM(NM) = 'x'");
    CHECK(emit("LTRIM(NM) = 'x'")         == "LTRIM(NM) = 'x'");
    CHECK(emit("SUBSTR(NM, 1, 3) = 'abc'") == "SUBSTR(NM, 1, 3) = 'abc'");
    CHECK(emit("SUBS(NM, 2) = 'b'")        == "SUBSTR(NM, 2) = 'b'");
    CHECK(emit("UPPER(ALLTRIM(NM)) = 'X'") == "UPPER(TRIM(NM)) = 'X'");
}

TEST_CASE("SQL push-down: '$' contains operator maps to LIKE") {
    CHECK(emit("'abc' $ NM")        == "NM LIKE '%abc%'");
    CHECK(emit("\"O'Brien\" $ NM")  == "NM LIKE '%O''Brien%'");  // quote escaped
    // Combined with other predicates.
    CHECK(emit("'x' $ NM .AND. QTY > 0") == "(NM LIKE '%x%' AND QTY > 0)");
}

TEST_CASE("SQL push-down: LEFT maps to SUBSTR") {
    CHECK(emit("LEFT(NM, 3) = 'abc'") == "SUBSTR(NM, 1, 3) = 'abc'");
}

TEST_CASE("SQL push-down: string literals are re-quoted and escaped") {
    // Double-quoted xBase string with an embedded single quote -> SQL
    // single-quoted with the quote doubled.
    CHECK(emit("NM = \"O'Brien\"") == "NM = 'O''Brien'");
    CHECK(emit("NM = 'plain'")     == "NM = 'plain'");
}

TEST_CASE("SQL push-down: string concatenation") {
    CHECK(emit("FIRST + LAST = 'AB'") == "(FIRST || LAST) = 'AB'");

    SqlDialect mysql;
    mysql.use_concat_fn = true;
    auto r = try_emit_sql_where("FIRST + LAST = 'AB'", mysql);
    REQUIRE(r.has_value());
    CHECK(*r == "CONCAT(FIRST, LAST) = 'AB'");
}

TEST_CASE("SQL push-down: dialect overrides") {
    SqlDialect ansi;
    ansi.substr_fn = "SUBSTRING";
    auto r = try_emit_sql_where("SUBSTR(NM, 1, 3) = 'x'", ansi);
    REQUIRE(r.has_value());
    CHECK(*r == "SUBSTRING(NM, 1, 3) = 'x'");
}

TEST_CASE("SQL push-down: ALIAS-> qualifiers are stripped") {
    CHECK(emit("CUST->QTY >= 100") == "QTY >= 100");
}

TEST_CASE("SQL push-down: untranslatable predicates decline to nullopt") {
    // RECNO()/DELETED(): no portable column form here -> fall back.
    CHECK_FALSE(try_emit_sql_where("RECNO() > 5").has_value());
    CHECK_FALSE(try_emit_sql_where("DELETED()").has_value());
    CHECK_FALSE(try_emit_sql_where(".NOT. DELETED()").has_value());
    // Conversions not modelled.
    CHECK_FALSE(try_emit_sql_where("STR(QTY) = '1'").has_value());
    CHECK_FALSE(try_emit_sql_where("VAL(CODE) > 3").has_value());
    CHECK_FALSE(try_emit_sql_where("DTOS(DT) = '20260101'").has_value());
    // Unknown function.
    CHECK_FALSE(try_emit_sql_where("FOO(X) = 1").has_value());
    // '$' with a non-literal needle (field) can't be safely pushed.
    CHECK_FALSE(try_emit_sql_where("NM $ DESC").has_value());
    // '$' with a LIKE wildcard in the needle would change matching -> decline.
    CHECK_FALSE(try_emit_sql_where("'a%b' $ NM").has_value());
    // A bare scalar with no comparison can't be coerced to a SQL boolean.
    CHECK_FALSE(try_emit_sql_where("QTY").has_value());
    // Empty / whitespace.
    CHECK_FALSE(try_emit_sql_where("").has_value());
    CHECK_FALSE(try_emit_sql_where("   ").has_value());
}
