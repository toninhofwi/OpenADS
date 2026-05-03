#pragma once

#include "util/result.h"

#include <string>

namespace openads::sql {

// Minimal SQL parser. Phase 1 covers `SELECT * FROM <table_name>`
// only — no WHERE, no projection list, no joins. The result of parsing
// is the bare table name (relative path or DD alias). Subsequent
// milestones (M7.x) extend the grammar to projection lists, WHERE
// clauses, ORDER BY, joins, aggregates, and subqueries.

struct SelectStmt {
    std::string table;
};

util::Result<SelectStmt> parse_select(const std::string& sql);

} // namespace openads::sql
