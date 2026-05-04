#pragma once

#include "util/result.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openads::sql {

// SELECT * FROM <table> [WHERE <expr>]
//
// Where <expr> is either a column comparison (`<col> op <lit>` —
// six infix operators), a CONTAINS(<col>, '<lit>') FTS predicate,
// or a boolean tree of those built from AND / OR / NOT / parens.
// String and numeric literals are supported. Projection lists,
// joins, aggregates, subqueries, ORDER BY, INSERT / UPDATE / DELETE
// land in subsequent milestones.

enum class WhereOp { Eq, Ne, Lt, Gt, Le, Ge, Contains };

struct WhereCmp {
    std::string column;
    WhereOp     op = WhereOp::Eq;
    std::string literal;       // raw string content, unquoted
    bool        is_numeric = false;
    double      number     = 0.0;
};

struct WhereExpr {
    // Tagged tree node.
    enum class Kind { Cmp, And, Or, Not };
    Kind                       kind = Kind::Cmp;
    WhereCmp                   cmp;             // Kind::Cmp
    std::vector<std::unique_ptr<WhereExpr>> children; // And/Or
    std::unique_ptr<WhereExpr> child;           // Not
};

struct OrderBy {
    std::string column;
    bool        descending = false;
};

struct SelectStmt {
    std::string                table;
    // Optional WHERE — tree form. nullptr means "no filter".
    std::unique_ptr<WhereExpr> where;
    // Optional ORDER BY — single column ascending or descending (M10.6).
    std::optional<OrderBy>     order_by;
};

util::Result<SelectStmt> parse_select(const std::string& sql);

// M10.5 — `INSERT INTO <table> (<col>, …) VALUES (<lit>, …)`.
struct InsertLiteral {
    bool        is_numeric = false;
    std::string text;          // raw string content if !is_numeric
    double      number     = 0.0;
};

struct InsertStmt {
    std::string                  table;
    std::vector<std::string>     columns;
    std::vector<InsertLiteral>   values;
};

util::Result<InsertStmt> parse_insert(const std::string& sql);

// Returns true when `sql`'s first non-whitespace keyword is INSERT.
// Used by AdsExecuteSQLDirect to pick the right parser.
bool sql_is_insert(const std::string& sql);

} // namespace openads::sql
