#pragma once

#include "sql_backend/sql_ddl.h"
#include "util/result.h"

#include <optional>
#include <string>

namespace openads::sql_backend {

// If `sql` is a simple SELECT FROM system.<name>, return the catalog SQL
// for the given dialect; otherwise return std::nullopt (pass through).
std::optional<std::string> rewrite_system_select_sql(
    SqlDdlDialect dialect,
    const std::string& sql);

// `sys_name` is the part after "system." (e.g. "tables", "columns", "iota").
std::optional<std::string> build_system_catalog_sql(
    SqlDdlDialect dialect,
    const std::string& sys_name);

}  // namespace openads::sql_backend