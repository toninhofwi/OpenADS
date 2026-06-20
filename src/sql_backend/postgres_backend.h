#pragma once

#include "sql_backend/postgres_table.h"
#include "util/result.h"

struct pg_result;

namespace openads::sql_backend {

PostgresTable::FieldDesc map_pg_column(const char* name,
                                       const char* data_type,
                                       bool nullable,
                                       int char_max_len,
                                       int numeric_precision,
                                       int numeric_scale);

std::string format_pg_value(pg_result* res, int row, int col, bool& is_null);

util::Error postgres_error(const char* context, const char* msg);

std::size_t field_index_ci(const PostgresTable& tbl, const std::string& name);

} // namespace openads::sql_backend