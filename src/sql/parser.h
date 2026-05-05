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

enum class WhereOp { Eq, Ne, Lt, Gt, Le, Ge, Contains, Between, Like };

// Forward declaration so WhereCmp + InClause + WhereExpr can hold
// optional sub-SelectStmts without seeing the full struct yet.
struct SelectStmt;

struct WhereCmp {
    std::string column;
    WhereOp     op = WhereOp::Eq;
    std::string literal;       // raw string content, unquoted
    bool        is_numeric = false;
    double      number     = 0.0;
    // M10.18: scalar subquery — `<col> op (SELECT <col> FROM <t>)`.
    // When present, the literal / number fields are populated at
    // compile time from the subquery's first projected value.
    std::unique_ptr<SelectStmt> subquery;
    // M10.24: correlated subquery RHS — `<col> op <outer_column>`.
    // When set, the executor reads `outer_column` from the outer
    // cursor's current row at each evaluation instead of using
    // `literal`/`number`.
    bool        is_outer_ref = false;
    std::string outer_column;
    // M10.33 — BETWEEN's upper bound (`<col> BETWEEN lit1 AND lit2`).
    // When op == WhereOp::Between, `literal`/`number` hold the lower
    // bound and `literal2`/`number2` hold the upper bound.
    std::string literal2;
    double      number2 = 0.0;
};

// M10.15 — `<col> IN (<lit>, <lit>, …)` or `<col> IN (<sub-SELECT>)`.
struct InClause {
    std::string                  column;
    std::vector<std::string>     literals;        // compile-time list
    std::unique_ptr<SelectStmt>  subquery;        // optional nested SELECT
};

struct WhereExpr {
    // Tagged tree node.
    enum class Kind { Cmp, And, Or, Not, In, Exists };
    Kind                       kind = Kind::Cmp;
    WhereCmp                   cmp;             // Kind::Cmp
    std::vector<std::unique_ptr<WhereExpr>> children; // And/Or
    std::unique_ptr<WhereExpr> child;           // Not
    InClause                   in_clause;       // Kind::In
    std::unique_ptr<SelectStmt> exists_subquery; // Kind::Exists (M10.17)
};

struct OrderBy {
    std::string column;
    bool        descending = false;
};

// M10.10 — `COUNT(*) / COUNT(col) / SUM / AVG / MIN / MAX(col)`.
enum class AggregateKind { CountStar, Count, Sum, Avg, Min, Max };

struct Aggregate {
    AggregateKind kind;
    std::string   column;   // empty for CountStar
};

// M10.25 — `HAVING <agg>(<col_or_*>) <op> <number>` leaf comparison.
struct HavingCmp {
    Aggregate agg;
    WhereOp   op  = WhereOp::Eq;
    double    num = 0.0;
};

// M10.30 — full boolean tree over HAVING comparisons (AND / OR / NOT
// / parens). Each leaf is a HavingCmp.
struct HavingExpr {
    enum class Kind { Cmp, And, Or, Not };
    Kind                                     kind = Kind::Cmp;
    HavingCmp                                cmp;
    std::vector<std::unique_ptr<HavingExpr>> children;   // And / Or
    std::unique_ptr<HavingExpr>              child;      // Not
};

// M10.13 — `INNER JOIN <table> ON <left_col> = <right_col>`.
struct JoinClause {
    std::string  table;
    std::string  left_column;
    std::string  right_column;
    bool         is_left  = false;  // M10.16 — true for LEFT OUTER JOIN
    bool         is_right = false;  // M10.21 — true for RIGHT OUTER JOIN
    bool         is_full  = false;  // M10.22 — true for FULL OUTER JOIN
};

struct SelectStmt {
    std::string                table;
    // M10.31 — DISTINCT keyword right after SELECT. When set, the
    // result cursor dedups by projected column values.
    bool                       distinct = false;
    // Empty `projection` means `SELECT *` (every column visible);
    // otherwise the cursor exposes only the listed columns in order.
    std::vector<std::string>   projection;
    // Aggregate calls in the projection (M10.10). Mutually exclusive
    // with `projection` — apps either select columns or aggregate.
    std::vector<Aggregate>     aggregates;
    // Optional INNER JOIN (M10.13). Single equality predicate.
    std::optional<JoinClause>  inner_join;
    // Optional WHERE — tree form. nullptr means "no filter".
    std::unique_ptr<WhereExpr> where;
    // ORDER BY — first column (M10.6 single, M10.37 multi-column).
    // Additional columns land in `order_by_extra`; `order_by` set
    // means at least one ORDER BY column is in effect.
    std::optional<OrderBy>     order_by;
    std::vector<OrderBy>       order_by_extra;
    // M10.25 — GROUP BY columns + optional HAVING (M10.30 tree).
    std::vector<std::string>      group_by;
    std::unique_ptr<HavingExpr>   having;
    // M10.32 — LIMIT [OFFSET]. `limit < 0` means no limit; `offset`
    // skips that many surviving rows before counting toward limit.
    std::int64_t                  limit  = -1;
    std::int64_t                  offset = 0;
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

// M10.7 — `UPDATE <table> SET <col>=<lit>, … [WHERE <expr>]`.
struct UpdateAssign {
    std::string   column;
    InsertLiteral value;
};

struct UpdateStmt {
    std::string                  table;
    std::vector<UpdateAssign>    assignments;
    std::unique_ptr<WhereExpr>   where;
};

util::Result<UpdateStmt> parse_update(const std::string& sql);

// M10.7 — `DELETE FROM <table> [WHERE <expr>]`.
struct DeleteStmt {
    std::string                  table;
    std::unique_ptr<WhereExpr>   where;
};

util::Result<DeleteStmt> parse_delete(const std::string& sql);

// M10.9 — `CREATE TABLE <name> (<col> <type> [(<len> [, <dec>])] …)`.
struct CreateTableColumn {
    std::string  name;
    std::string  type;          // 'Character' / 'Numeric' / 'Memo' / …
    std::uint32_t length    = 0;
    std::uint32_t decimals  = 0;
};

struct CreateTableStmt {
    std::string                       table;
    std::vector<CreateTableColumn>    columns;
};

util::Result<CreateTableStmt> parse_create_table(const std::string& sql);

// M10.9 — `CREATE INDEX <tag> ON <table> (<expr>) [DESCENDING] [UNIQUE]`.
struct CreateIndexStmt {
    std::string  table;
    std::string  tag;
    std::string  expression;
    bool         descending = false;
    bool         unique     = false;
};

util::Result<CreateIndexStmt> parse_create_index(const std::string& sql);

// Leading-keyword peeks (used by AdsExecuteSQLDirect to dispatch).
bool sql_is_insert(const std::string& sql);
bool sql_is_update(const std::string& sql);
bool sql_is_delete(const std::string& sql);
bool sql_is_create_table(const std::string& sql);
bool sql_is_create_index(const std::string& sql);

} // namespace openads::sql
