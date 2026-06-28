#pragma once

#include "engine/data_dict.h"
#include "sql_backend/sql_ddl.h"
#include "util/result.h"

#include <functional>
#include <optional>
#include <string>

namespace openads::sql_backend {

// Persisted ACL for SQL URI connections (no Advantage DD).  Rows live in
// OPENADS$ACL and surface through system.permissions via catalog UNION.

using SqlExecFn = std::function<util::Result<void>(const std::string&)>;

// First cell of the first row, or nullopt when the result set is empty.
using SqlQueryFn =
    std::function<util::Result<std::optional<std::string>>(const std::string&)>;

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

// True when OPENADS$ACL contains at least one row.
bool sql_acl_has_any(SqlDdlDialect dialect, const SqlQueryFn& query);

// Effective per-operation rights for `username` on table `object_name`.
// Matches Advantage DD semantics: empty ACL table → open; object without
// any ACL row → open; otherwise bitmask rows for the user and PUBLIC apply.
openads::engine::DataDict::EffectiveOps sql_acl_effective_ops(
    SqlDdlDialect dialect,
    const SqlQueryFn& query,
    const std::string& username,
    const std::string& object_name);

}  // namespace openads::sql_backend