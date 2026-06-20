#pragma once

#include "sql_backend/sqlite_table.h"
#include "util/result.h"

#include <sqlite3.h>

#include <string>

namespace openads::sql_backend {

bool is_safe_identifier(const std::string& name);

SqliteTable::FieldDesc map_sqlite_column(const char* name,
                                         const char* declared_type,
                                         bool notnull);

std::string format_sqlite_value(sqlite3_stmt* stmt, int col, bool& is_null);

util::Error sqlite_error(sqlite3* db, const char* context);

util::Result<void> apply_cipher_key(sqlite3* db, const std::string& key);

std::size_t field_index_ci(const SqliteTable& tbl, const std::string& name);

} // namespace openads::sql_backend