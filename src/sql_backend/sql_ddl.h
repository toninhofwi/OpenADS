#pragma once

#include "util/result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openads::sql_backend {

enum class SqlDdlDialect {
    Sqlite,
    Postgres,
    Maria,
    Mssql,
    Firebird,
};

struct SqlDdlColumn {
    std::string  name;
    char         xbase_type = 'C';
    std::uint8_t length     = 10;
    std::uint8_t dec        = 0;
};

util::Result<std::string> build_create_table_ddl(
    SqlDdlDialect dialect,
    const std::string& table_name,
    const std::vector<SqlDdlColumn>& columns);

// One ALTER TABLE … ADD COLUMN statement per column (portable across dialects).
util::Result<std::vector<std::string>> build_alter_table_add_ddl(
    SqlDdlDialect dialect,
    const std::string& table_name,
    const std::vector<SqlDdlColumn>& columns);

}  // namespace openads::sql_backend