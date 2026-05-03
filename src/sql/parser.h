#pragma once

#include "util/result.h"

#include <optional>
#include <string>

namespace openads::sql {

// SELECT * FROM <table> [WHERE <col> = '<literal>']
//
// Phase scope: SELECT *, single optional WHERE with one equality
// comparison between a column name and a string literal. Multi-clause
// WHERE, AND/OR, ORDER BY, projection lists, joins, subqueries, and
// aggregates land in subsequent milestones.

struct WhereEq {
    std::string column;
    std::string literal;   // raw string content, unquoted
};

struct SelectStmt {
    std::string             table;
    std::optional<WhereEq>  where;
};

util::Result<SelectStmt> parse_select(const std::string& sql);

} // namespace openads::sql
