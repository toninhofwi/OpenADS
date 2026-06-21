#pragma once

#include "sql_backend/sql_common.h"

#include <string>

namespace openads::sql_backend {

struct OdbcTable;

struct OdbcIndex {
    OdbcTable*    parent          = nullptr;
    std::string   column;
    IndexExprKind expr_kind       = IndexExprKind::Column;
    bool          last_seek_found = false;
};

} // namespace openads::sql_backend
