#pragma once

#include "sql_backend/sql_ddl.h"
#include "util/result.h"

#include <functional>
#include <optional>
#include <string>

namespace openads::sql_backend {

// Persisted ACL for SQL URI connections (no Advantage DD).  Rows live in
// OPENADS$ACL and surface through system.permissions via catalog UNION.

using SqlExecFn = std::function<util::Result<void>(const std::string&)>;

// CREATE TABLE IF NOT EXISTS for the dialect.
std::string acl_table_ddl(SqlDdlDialect dialect);

// SELECT fragment (system.permissions column layout) from OPENADS$ACL.
std::string acl_permissions_select_sql(SqlDdlDialect dialect);

// If `sql` is GRANT/REVOKE, apply it via `exec_sql` and return the result;
// otherwise return std::nullopt so the caller continues normal dispatch.
std::optional<util::Result<void>> try_sql_acl_statement(
    const std::string& sql,
    SqlDdlDialect dialect,
    SqlExecFn exec_sql);

}  // namespace openads::sql_backend