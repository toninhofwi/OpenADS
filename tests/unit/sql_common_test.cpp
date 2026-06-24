#include "doctest.h"
#include "sql_backend/sql_common.h"

using openads::sql_backend::IndexExprKind;
using openads::sql_backend::ParsedIndexExpr;
using openads::sql_backend::is_safe_identifier;
using openads::sql_backend::parse_index_expr;

TEST_CASE("is_safe_identifier: valid identifiers") {
    CHECK(is_safe_identifier("name"));
    CHECK(is_safe_identifier("NAME"));
    CHECK(is_safe_identifier("Name123"));
    CHECK(is_safe_identifier("_private"));
    CHECK(is_safe_identifier("a"));
    CHECK(is_safe_identifier("col_1"));
    CHECK(is_safe_identifier("MY_TABLE"));
    CHECK(is_safe_identifier("x9"));
}

TEST_CASE("is_safe_identifier: accepts digits anywhere") {
    CHECK(is_safe_identifier("1name"));
    CHECK(is_safe_identifier("123"));
    CHECK(is_safe_identifier("col1"));
}

TEST_CASE("is_safe_identifier: rejects invalid") {
    CHECK_FALSE(is_safe_identifier(""));
    CHECK_FALSE(is_safe_identifier(" "));
    CHECK_FALSE(is_safe_identifier("col name"));
    CHECK_FALSE(is_safe_identifier("col-name"));
    CHECK_FALSE(is_safe_identifier("col.name"));
    CHECK_FALSE(is_safe_identifier("col@name"));
    CHECK_FALSE(is_safe_identifier("col$name"));
    CHECK_FALSE(is_safe_identifier("col\u00e9"));  // accented
}

TEST_CASE("parse_index_expr: bare column") {
    auto r = parse_index_expr("NAME");
    REQUIRE(r);
    CHECK(r.value().kind == IndexExprKind::Column);
    CHECK(r.value().column == "NAME");
}

TEST_CASE("parse_index_expr: bare column trimmed") {
    auto r = parse_index_expr("  name  ");
    REQUIRE(r);
    CHECK(r.value().kind == IndexExprKind::Column);
    CHECK(r.value().column == "name");
}

TEST_CASE("parse_index_expr: UPPER(column)") {
    auto r = parse_index_expr("UPPER(NAME)");
    REQUIRE(r);
    CHECK(r.value().kind == IndexExprKind::UpperColumn);
    CHECK(r.value().column == "NAME");
}

TEST_CASE("parse_index_expr: upper() case-insensitive") {
    auto r = parse_index_expr("upper(name)");
    REQUIRE(r);
    CHECK(r.value().kind == IndexExprKind::UpperColumn);
    CHECK(r.value().column == "name");
}

TEST_CASE("parse_index_expr: UPPER with spaces") {
    auto r = parse_index_expr("  UPPER( name )  ");
    REQUIRE(r);
    CHECK(r.value().kind == IndexExprKind::UpperColumn);
    CHECK(r.value().column == "name");
}

TEST_CASE("parse_index_expr: rejects empty") {
    CHECK_FALSE(parse_index_expr(""));
    CHECK_FALSE(parse_index_expr("  "));
}

TEST_CASE("parse_index_expr: accepts numeric-only column") {
    auto r = parse_index_expr("123");
    REQUIRE(r);
    CHECK(r.value().kind == IndexExprKind::Column);
    CHECK(r.value().column == "123");
}

TEST_CASE("parse_index_expr: rejects unsafe column") {
    CHECK_FALSE(parse_index_expr("col name"));
    CHECK_FALSE(parse_index_expr("UPPER(col name)"));
    CHECK_FALSE(parse_index_expr("col-name"));
    CHECK_FALSE(parse_index_expr("col.name"));
}

TEST_CASE("parse_index_expr: rejects non-UPPER function") {
    CHECK_FALSE(parse_index_expr("LOWER(NAME)"));
    CHECK_FALSE(parse_index_expr("TRIM(NAME)"));
    CHECK_FALSE(parse_index_expr("ABS(1)"));
}

TEST_CASE("parse_index_expr: rejects UPPER without closing paren") {
    CHECK_FALSE(parse_index_expr("UPPER(NAME"));
}

TEST_CASE("parse_index_expr: rejects bare parentheses") {
    CHECK_FALSE(parse_index_expr("(NAME)"));
}

TEST_CASE("parse_index_expr: rejects UPPER with empty inner") {
    // "UPPER()" -> column would be empty -> is_safe_identifier fails
    CHECK_FALSE(parse_index_expr("UPPER()"));
}

TEST_CASE("parse_index_expr: single char column") {
    auto r = parse_index_expr("X");
    REQUIRE(r);
    CHECK(r.value().kind == IndexExprKind::Column);
    CHECK(r.value().column == "X");
}

TEST_CASE("parse_index_expr: column with digits and underscores") {
    auto r = parse_index_expr("col_123_name");
    REQUIRE(r);
    CHECK(r.value().kind == IndexExprKind::Column);
    CHECK(r.value().column == "col_123_name");
}
