#include "sql_backend/sql_ddl.h"

#include "sql_backend/sql_common.h"

#include <string>

namespace openads::sql_backend {

namespace {

std::string quote_table(SqlDdlDialect d, const std::string& name) {
    switch (d) {
        case SqlDdlDialect::Mssql:
            return "[" + name + "]";
        case SqlDdlDialect::Maria:
            return "`" + name + "`";
        default:
            return "\"" + name + "\"";
    }
}

std::string quote_col(SqlDdlDialect d, const std::string& name) {
    return quote_table(d, name);
}

std::string sql_type_for(SqlDdlDialect d, const SqlDdlColumn& c) {
    const std::uint8_t len = c.length > 0 ? c.length : 10;
    switch (c.xbase_type) {
        case 'L':
            return (d == SqlDdlDialect::Mssql) ? "BIT" : "INTEGER";
        case 'D':
            if (d == SqlDdlDialect::Mssql) return "DATE";
            if (d == SqlDdlDialect::Postgres) return "DATE";
            return "TEXT";
        case 'M':
            if (d == SqlDdlDialect::Mssql) return "NVARCHAR(MAX)";
            return "TEXT";
        case 'N':
            if (c.dec > 0) {
                if (d == SqlDdlDialect::Mssql) {
                    return "DECIMAL(" + std::to_string(len) + "," +
                           std::to_string(c.dec) + ")";
                }
                return "REAL";
            }
            if (d == SqlDdlDialect::Mssql) return "BIGINT";
            return "INTEGER";
        case 'A':
            if (d == SqlDdlDialect::Sqlite) return "INTEGER";
            if (d == SqlDdlDialect::Postgres) return "SERIAL";
            if (d == SqlDdlDialect::Maria) return "INT AUTO_INCREMENT";
            if (d == SqlDdlDialect::Mssql) return "INT IDENTITY(1,1)";
            return "INTEGER";
        case 'C':
        default:
            if (d == SqlDdlDialect::Mssql) {
                return "NVARCHAR(" + std::to_string(len) + ")";
            }
            if (d == SqlDdlDialect::Postgres) {
                return "VARCHAR(" + std::to_string(len) + ")";
            }
            if (d == SqlDdlDialect::Maria) {
                return "VARCHAR(" + std::to_string(len) + ")";
            }
            if (d == SqlDdlDialect::Firebird) {
                return "VARCHAR(" + std::to_string(len) + ")";
            }
            return "TEXT";
    }
}

}  // namespace

util::Result<std::string> build_create_table_ddl(
    SqlDdlDialect dialect,
    const std::string& table_name,
    const std::vector<SqlDdlColumn>& columns) {
    if (!is_safe_identifier(table_name)) {
        return util::Error{5001, 0, "unsafe table name", table_name};
    }
    if (columns.empty()) {
        return util::Error{5001, 0, "CREATE TABLE: no columns", ""};
    }
    std::string sql = "CREATE TABLE " + quote_table(dialect, table_name) + " (";
    std::string pk_col;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        const auto& c = columns[i];
        if (!is_safe_identifier(c.name)) {
            return util::Error{5001, 0, "unsafe column name", c.name};
        }
        if (i) sql += ", ";
        sql += quote_col(dialect, c.name) + " " + sql_type_for(dialect, c);
        if (c.xbase_type == 'A' && pk_col.empty()) pk_col = c.name;
    }
    if (!pk_col.empty()) {
        sql += ", PRIMARY KEY (" + quote_col(dialect, pk_col) + ")";
    }
    sql += ")";
    return sql;
}

}  // namespace openads::sql_backend