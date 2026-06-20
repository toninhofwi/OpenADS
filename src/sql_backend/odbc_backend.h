#pragma once

#include "sql_backend/odbc_table.h"
#include "util/result.h"

#include <cstdint>
#include <string>

namespace openads::sql_backend {

// Map an ODBC SQL type code (SQLColumns.DATA_TYPE) + size/scale to the
// ADS field descriptor surfaced through the ABI.
OdbcTable::FieldDesc map_odbc_column(const std::string& name,
                                     int sql_type,
                                     bool nullable,
                                     int column_size,
                                     int decimal_digits);

util::Error odbc_error(const char* context, const std::string& detail);

std::size_t field_index_ci(const OdbcTable& tbl, const std::string& name);

} // namespace openads::sql_backend
