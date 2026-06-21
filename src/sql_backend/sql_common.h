#pragma once

#include "util/result.h"

#include <string>

namespace openads::sql_backend {

bool is_safe_identifier(const std::string& name);

enum class IndexExprKind {
    Column,
    UpperColumn,
};

struct ParsedIndexExpr {
    IndexExprKind kind     = IndexExprKind::Column;
    std::string   column;
};

// v1 Plus SQL index expressions: bare column or UPPER(column) only.
util::Result<ParsedIndexExpr> parse_index_expr(const std::string& expr);

} // namespace openads::sql_backend
