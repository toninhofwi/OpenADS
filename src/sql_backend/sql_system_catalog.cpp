#include "sql_backend/sql_system_catalog.h"

#include "sql/parser.h"

namespace openads::sql_backend {

namespace {

std::string lower_copy(std::string s) {
    for (auto& ch : s) {
        ch = static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::optional<std::string> system_table_from_select(const std::string& sql) {
    if (!openads::sql::sql_is_select(sql)) return std::nullopt;
    auto sel = openads::sql::parse_select(sql);
    if (!sel) return std::nullopt;
    const auto& st = sel.value();
    if (!st.derived_sql.empty() || !st.unions.empty()) return std::nullopt;
    if (st.inner_join.has_value()) return std::nullopt;
    const std::string px = lower_copy(
        st.table.size() >= 7 ? st.table.substr(0, 7) : st.table);
    if (px != "system.") return std::nullopt;
    return lower_copy(st.table.substr(7));
}

// SAP-compatible filler columns for system.tables (Table_Type 1 = SQL table).
const char* k_tables_tail =
    ", 'True' AS \"Table_Auto_Create\", '' AS \"Table_Primary_Key\", "
    "'' AS \"Table_Default_Index\", 'False' AS \"Table_Encryption\", "
    "'4' AS \"Table_Permission_Level\", '0' AS \"Table_Memo_Block_Size\", "
    "'' AS \"Table_Validation_Expr\", '' AS \"Table_Validation_Msg\", "
    "'' AS \"Comment\", 'False' AS \"Triggers_Disabled\", "
    "'0' AS \"Table_Caching\", 'False' AS \"Table_Trans_Free\", "
    "'False' AS \"Table_WEB_delta\", "
    "'False' AS \"Table_Concurrency_Enabled\"";

std::optional<std::string> catalog_sql(SqlDdlDialect dialect,
                                       const std::string& sys_name) {
    if (sys_name == "iota") {
        return "SELECT ' ' AS \"IOTA\"";
    }
    if (sys_name == "tables") {
        switch (dialect) {
            case SqlDdlDialect::Sqlite:
                return std::string(
                    "SELECT name AS \"Name\", name AS \"Table_Relative_Path\", "
                    "'1' AS \"Table_Type\"") +
                       k_tables_tail +
                       " FROM sqlite_master "
                       "WHERE type='table' AND name NOT LIKE 'sqlite_%' "
                       "ORDER BY name";
            case SqlDdlDialect::Postgres:
            case SqlDdlDialect::Maria:
                return std::string(
                    "SELECT table_name AS \"Name\", "
                    "table_name AS \"Table_Relative_Path\", "
                    "'1' AS \"Table_Type\"") +
                       k_tables_tail +
                       " FROM information_schema.tables "
                       "WHERE table_schema = ANY (current_schemas(true)) "
                       "AND table_type = 'BASE TABLE' "
                       "ORDER BY table_name";
            case SqlDdlDialect::Mssql:
                return std::string(
                    "SELECT TABLE_NAME AS \"Name\", "
                    "TABLE_NAME AS \"Table_Relative_Path\", "
                    "'1' AS \"Table_Type\"") +
                       k_tables_tail +
                       " FROM INFORMATION_SCHEMA.TABLES "
                       "WHERE TABLE_TYPE = 'BASE TABLE' "
                       "ORDER BY TABLE_NAME";
            case SqlDdlDialect::Firebird:
                return std::string(
                    "SELECT TRIM(rdb$relation_name) AS \"Name\", "
                    "TRIM(rdb$relation_name) AS \"Table_Relative_Path\", "
                    "'1' AS \"Table_Type\"") +
                       k_tables_tail +
                       " FROM rdb$relations "
                       "WHERE rdb$view_blr IS NULL "
                       "AND rdb$system_flag = 0 "
                       "ORDER BY rdb$relation_name";
        }
    }
    if (sys_name == "columns") {
        switch (dialect) {
            case SqlDdlDialect::Sqlite:
                return R"(
SELECT m.name AS "TABLE_NAME", p.name AS "COL_NAME",
       CAST(p.cid + 1 AS TEXT) AS "COL_NUM",
       CASE
         WHEN UPPER(p.type) LIKE '%INT%' THEN 'N'
         WHEN UPPER(p.type) LIKE '%REAL%' OR UPPER(p.type) LIKE '%FLOA%'
              OR UPPER(p.type) LIKE '%DOUB%' OR UPPER(p.type) LIKE '%NUM%' THEN 'N'
         WHEN UPPER(p.type) LIKE '%BOOL%' THEN 'L'
         ELSE 'C'
       END AS "COL_TYPE",
       CASE
         WHEN UPPER(p.type) LIKE '%INT%' OR UPPER(p.type) LIKE '%REAL%'
              OR UPPER(p.type) LIKE '%FLOA%' OR UPPER(p.type) LIKE '%DOUB%'
              OR UPPER(p.type) LIKE '%NUM%' THEN '0'
         ELSE '10'
       END AS "COL_LEN",
       '0' AS "COL_DEC"
FROM sqlite_master m
JOIN pragma_table_info(m.name) p
WHERE m.type='table' AND m.name NOT LIKE 'sqlite_%'
ORDER BY m.name, p.cid)";
            case SqlDdlDialect::Postgres:
            case SqlDdlDialect::Maria:
                return R"(
SELECT table_name AS "TABLE_NAME", column_name AS "COL_NAME",
       CAST(ordinal_position AS TEXT) AS "COL_NUM",
       CASE
         WHEN data_type IN ('integer','bigint','smallint','numeric','real',
                            'double precision','decimal','float') THEN 'N'
         WHEN data_type IN ('boolean') THEN 'L'
         ELSE 'C'
       END AS "COL_TYPE",
       CAST(COALESCE(character_maximum_length,
         CASE WHEN data_type IN ('integer','bigint','smallint','numeric',
                                 'real','double precision','decimal','float')
              THEN 0 ELSE 10 END) AS TEXT) AS "COL_LEN",
       CAST(COALESCE(numeric_scale, 0) AS TEXT) AS "COL_DEC"
FROM information_schema.columns
WHERE table_schema = ANY (current_schemas(true))
ORDER BY table_name, ordinal_position)";
            case SqlDdlDialect::Mssql:
                return R"(
SELECT TABLE_NAME AS "TABLE_NAME", COLUMN_NAME AS "COL_NAME",
       CAST(ORDINAL_POSITION AS VARCHAR(10)) AS "COL_NUM",
       CASE
         WHEN DATA_TYPE IN ('int','bigint','smallint','tinyint','decimal',
                            'numeric','float','real','money') THEN 'N'
         WHEN DATA_TYPE IN ('bit') THEN 'L'
         ELSE 'C'
       END AS "COL_TYPE",
       CAST(COALESCE(CHARACTER_MAXIMUM_LENGTH,
         CASE WHEN DATA_TYPE IN ('int','bigint','smallint','tinyint',
                                 'decimal','numeric','float','real','money')
              THEN 0 ELSE 10 END) AS VARCHAR(10)) AS "COL_LEN",
       CAST(COALESCE(NUMERIC_SCALE, 0) AS VARCHAR(10)) AS "COL_DEC"
FROM INFORMATION_SCHEMA.COLUMNS
ORDER BY TABLE_NAME, ORDINAL_POSITION)";
            case SqlDdlDialect::Firebird:
                return R"(
SELECT TRIM(rf.rdb$relation_name) AS "TABLE_NAME",
       TRIM(rf.rdb$field_name) AS "COL_NAME",
       CAST(rf.rdb$field_position + 1 AS VARCHAR(10)) AS "COL_NUM",
       'C' AS "COL_TYPE", '10' AS "COL_LEN", '0' AS "COL_DEC"
FROM rdb$relation_fields rf
JOIN rdb$relations r ON r.rdb$relation_name = rf.rdb$relation_name
WHERE r.rdb$view_blr IS NULL AND r.rdb$system_flag = 0
ORDER BY rf.rdb$relation_name, rf.rdb$field_position)";
        }
    }
    return std::nullopt;
}

}  // namespace

std::optional<std::string> rewrite_system_select_sql(
    SqlDdlDialect dialect,
    const std::string& sql) {
    auto sys = system_table_from_select(sql);
    if (!sys) return std::nullopt;
    return catalog_sql(dialect, *sys);
}

}  // namespace openads::sql_backend