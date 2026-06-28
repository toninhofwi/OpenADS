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
    if (sys_name == "primarykeys") {
        switch (dialect) {
            case SqlDdlDialect::Sqlite:
                return R"(
SELECT m.name AS "TABLE_NAME", p.name AS "COLUMN_NAME",
       CAST(p.pk AS TEXT) AS "KEY_SEQ",
       (m.name || '_pk') AS "PK_NAME"
FROM sqlite_master m
JOIN pragma_table_info(m.name) p ON p.pk > 0
WHERE m.type='table' AND m.name NOT LIKE 'sqlite_%'
ORDER BY m.name, p.pk)";
            case SqlDdlDialect::Postgres:
                return R"(
SELECT tc.table_name AS "TABLE_NAME",
       kcu.column_name AS "COLUMN_NAME",
       CAST(kcu.ordinal_position AS TEXT) AS "KEY_SEQ",
       tc.constraint_name AS "PK_NAME"
FROM information_schema.table_constraints tc
JOIN information_schema.key_column_usage kcu
  ON tc.constraint_schema = kcu.table_schema
 AND tc.constraint_name = kcu.constraint_name
WHERE tc.constraint_type = 'PRIMARY KEY'
  AND tc.table_schema = ANY (current_schemas(true))
ORDER BY tc.table_name, kcu.ordinal_position)";
            case SqlDdlDialect::Maria:
                return R"(
SELECT tc.table_name AS "TABLE_NAME",
       kcu.column_name AS "COLUMN_NAME",
       CAST(kcu.ordinal_position AS TEXT) AS "KEY_SEQ",
       tc.constraint_name AS "PK_NAME"
FROM information_schema.table_constraints tc
JOIN information_schema.key_column_usage kcu
  ON tc.constraint_schema = kcu.table_schema
 AND tc.constraint_name = kcu.constraint_name
WHERE tc.constraint_type = 'PRIMARY KEY'
  AND tc.table_schema = DATABASE()
ORDER BY tc.table_name, kcu.ordinal_position)";
            case SqlDdlDialect::Mssql:
                return R"(
SELECT ku.TABLE_NAME AS "TABLE_NAME",
       ku.COLUMN_NAME AS "COLUMN_NAME",
       CAST(ku.ORDINAL_POSITION AS VARCHAR(10)) AS "KEY_SEQ",
       ku.CONSTRAINT_NAME AS "PK_NAME"
FROM INFORMATION_SCHEMA.TABLE_CONSTRAINTS tc
JOIN INFORMATION_SCHEMA.KEY_COLUMN_USAGE ku
  ON tc.CONSTRAINT_NAME = ku.CONSTRAINT_NAME
 AND tc.TABLE_SCHEMA = ku.TABLE_SCHEMA
WHERE tc.CONSTRAINT_TYPE = 'PRIMARY KEY'
ORDER BY ku.TABLE_NAME, ku.ORDINAL_POSITION)";
            case SqlDdlDialect::Firebird:
                return R"(
SELECT TRIM(rc.rdb$relation_name) AS "TABLE_NAME",
       TRIM(s.rdb$field_name) AS "COLUMN_NAME",
       CAST(s.rdb$field_position + 1 AS VARCHAR(10)) AS "KEY_SEQ",
       TRIM(i.rdb$index_name) AS "PK_NAME"
FROM rdb$relation_constraints rc
JOIN rdb$indices i ON i.rdb$index_name = rc.rdb$index_name
JOIN rdb$index_segments s ON s.rdb$index_name = i.rdb$index_name
WHERE rc.rdb$constraint_type = 'PRIMARY KEY'
ORDER BY rc.rdb$relation_name, s.rdb$field_position)";
        }
    }
    if (sys_name == "indexes") {
        switch (dialect) {
            case SqlDdlDialect::Sqlite:
                return R"(
SELECT m.name AS "TABLE_NAME", il.name AS "INDEX_FILE", '' AS "COMMENT"
FROM sqlite_master m, pragma_index_list(m.name) il
WHERE m.type='table' AND m.name NOT LIKE 'sqlite_%'
ORDER BY m.name, il.name)";
            case SqlDdlDialect::Postgres:
                return R"(
SELECT tablename AS "TABLE_NAME", indexname AS "INDEX_FILE", '' AS "COMMENT"
FROM pg_indexes
WHERE schemaname = ANY (current_schemas(true))
ORDER BY tablename, indexname)";
            case SqlDdlDialect::Maria:
                return R"(
SELECT TABLE_NAME AS "TABLE_NAME",
       INDEX_NAME AS "INDEX_FILE",
       '' AS "COMMENT"
FROM information_schema.statistics
WHERE TABLE_SCHEMA = DATABASE()
GROUP BY TABLE_NAME, INDEX_NAME
ORDER BY TABLE_NAME, INDEX_NAME)";
            case SqlDdlDialect::Mssql:
                return R"(
SELECT t.name AS "TABLE_NAME", i.name AS "INDEX_FILE", '' AS "COMMENT"
FROM sys.indexes i
JOIN sys.tables t ON i.object_id = t.object_id
WHERE i.index_id > 0
ORDER BY t.name, i.name)";
            case SqlDdlDialect::Firebird:
                return R"(
SELECT TRIM(r.rdb$relation_name) AS "TABLE_NAME",
       TRIM(i.rdb$index_name) AS "INDEX_FILE",
       '' AS "COMMENT"
FROM rdb$indices i
JOIN rdb$relations r ON r.rdb$relation_name = i.rdb$relation_name
WHERE r.rdb$system_flag = 0
ORDER BY r.rdb$relation_name, i.rdb$index_name)";
        }
    }
    if (sys_name == "relations") {
        switch (dialect) {
            case SqlDdlDialect::Sqlite:
                return R"(
SELECT '' AS "RI_NAME", '' AS "PARENT", '' AS "CHILD",
       '' AS "PARENT_TAG", '' AS "CHILD_TAG",
       '' AS "UPDATE_OPT", '' AS "DELETE_OPT", '' AS "FAIL_TABLE"
WHERE 0)";
            case SqlDdlDialect::Postgres:
                return R"(
SELECT rc.constraint_name AS "RI_NAME",
       ccu.table_name AS "PARENT",
       tc.table_name AS "CHILD",
       '' AS "PARENT_TAG",
       '' AS "CHILD_TAG",
       rc.update_rule AS "UPDATE_OPT",
       rc.delete_rule AS "DELETE_OPT",
       '' AS "FAIL_TABLE"
FROM information_schema.referential_constraints rc
JOIN information_schema.table_constraints tc
  ON rc.constraint_name = tc.constraint_name
 AND rc.constraint_schema = tc.table_schema
JOIN information_schema.constraint_column_usage ccu
  ON rc.unique_constraint_name = ccu.constraint_name
 AND rc.unique_constraint_schema = ccu.table_schema
WHERE tc.table_schema = ANY (current_schemas(true))
ORDER BY rc.constraint_name)";
            case SqlDdlDialect::Maria:
                return R"(
SELECT rc.constraint_name AS "RI_NAME",
       ccu.table_name AS "PARENT",
       tc.table_name AS "CHILD",
       '' AS "PARENT_TAG",
       '' AS "CHILD_TAG",
       rc.update_rule AS "UPDATE_OPT",
       rc.delete_rule AS "DELETE_OPT",
       '' AS "FAIL_TABLE"
FROM information_schema.referential_constraints rc
JOIN information_schema.table_constraints tc
  ON rc.constraint_name = tc.constraint_name
 AND rc.constraint_schema = tc.table_schema
JOIN information_schema.constraint_column_usage ccu
  ON rc.unique_constraint_name = ccu.constraint_name
 AND rc.unique_constraint_schema = ccu.table_schema
WHERE tc.table_schema = DATABASE()
ORDER BY rc.constraint_name)";
            case SqlDdlDialect::Mssql:
                return R"(
SELECT rc.CONSTRAINT_NAME AS "RI_NAME",
       ccu.TABLE_NAME AS "PARENT",
       tc.TABLE_NAME AS "CHILD",
       '' AS "PARENT_TAG",
       '' AS "CHILD_TAG",
       rc.UPDATE_RULE AS "UPDATE_OPT",
       rc.DELETE_RULE AS "DELETE_OPT",
       '' AS "FAIL_TABLE"
FROM INFORMATION_SCHEMA.REFERENTIAL_CONSTRAINTS rc
JOIN INFORMATION_SCHEMA.TABLE_CONSTRAINTS tc
  ON rc.CONSTRAINT_NAME = tc.CONSTRAINT_NAME
 AND rc.CONSTRAINT_SCHEMA = tc.TABLE_SCHEMA
JOIN INFORMATION_SCHEMA.CONSTRAINT_COLUMN_USAGE ccu
  ON rc.UNIQUE_CONSTRAINT_NAME = ccu.CONSTRAINT_NAME
 AND rc.UNIQUE_CONSTRAINT_SCHEMA = ccu.TABLE_SCHEMA
ORDER BY rc.CONSTRAINT_NAME)";
            case SqlDdlDialect::Firebird:
                return R"(
SELECT TRIM(rc.rdb$constraint_name) AS "RI_NAME",
       TRIM(rc.rdb$relation_name) AS "PARENT",
       TRIM(rc.rdb$const_name_uq) AS "CHILD",
       '' AS "PARENT_TAG",
       '' AS "CHILD_TAG",
       '' AS "UPDATE_OPT",
       '' AS "DELETE_OPT",
       '' AS "FAIL_TABLE"
FROM rdb$relation_constraints rc
WHERE rc.rdb$constraint_type = 'FOREIGN KEY'
ORDER BY rc.rdb$constraint_name)";
        }
    }
    if (sys_name == "referentialintegrity") {
        switch (dialect) {
            case SqlDdlDialect::Sqlite:
                return R"(
SELECT '' AS "RI_NAME", '' AS "PARENT_TABLE", '' AS "CHILD_TABLE",
       '' AS "PARENT_TAG", '' AS "CHILD_TAG",
       '0' AS "UPDATE_RULE", '0' AS "DELETE_RULE", '' AS "FAIL_TABLE"
WHERE 0)";
            case SqlDdlDialect::Postgres:
                return R"(
SELECT rc.constraint_name AS "RI_NAME",
       ccu.table_name AS "PARENT_TABLE",
       tc.table_name AS "CHILD_TABLE",
       '' AS "PARENT_TAG",
       '' AS "CHILD_TAG",
       '0' AS "UPDATE_RULE",
       '0' AS "DELETE_RULE",
       '' AS "FAIL_TABLE"
FROM information_schema.referential_constraints rc
JOIN information_schema.table_constraints tc
  ON rc.constraint_name = tc.constraint_name
 AND rc.constraint_schema = tc.table_schema
JOIN information_schema.constraint_column_usage ccu
  ON rc.unique_constraint_name = ccu.constraint_name
 AND rc.unique_constraint_schema = ccu.table_schema
WHERE tc.table_schema = ANY (current_schemas(true))
ORDER BY rc.constraint_name)";
            case SqlDdlDialect::Maria:
                return R"(
SELECT rc.constraint_name AS "RI_NAME",
       ccu.table_name AS "PARENT_TABLE",
       tc.table_name AS "CHILD_TABLE",
       '' AS "PARENT_TAG",
       '' AS "CHILD_TAG",
       '0' AS "UPDATE_RULE",
       '0' AS "DELETE_RULE",
       '' AS "FAIL_TABLE"
FROM information_schema.referential_constraints rc
JOIN information_schema.table_constraints tc
  ON rc.constraint_name = tc.constraint_name
 AND rc.constraint_schema = tc.table_schema
JOIN information_schema.constraint_column_usage ccu
  ON rc.unique_constraint_name = ccu.constraint_name
 AND rc.unique_constraint_schema = ccu.table_schema
WHERE tc.table_schema = DATABASE()
ORDER BY rc.constraint_name)";
            case SqlDdlDialect::Mssql:
                return R"(
SELECT rc.CONSTRAINT_NAME AS "RI_NAME",
       ccu.TABLE_NAME AS "PARENT_TABLE",
       tc.TABLE_NAME AS "CHILD_TABLE",
       '' AS "PARENT_TAG",
       '' AS "CHILD_TAG",
       '0' AS "UPDATE_RULE",
       '0' AS "DELETE_RULE",
       '' AS "FAIL_TABLE"
FROM INFORMATION_SCHEMA.REFERENTIAL_CONSTRAINTS rc
JOIN INFORMATION_SCHEMA.TABLE_CONSTRAINTS tc
  ON rc.CONSTRAINT_NAME = tc.CONSTRAINT_NAME
JOIN INFORMATION_SCHEMA.CONSTRAINT_COLUMN_USAGE ccu
  ON rc.UNIQUE_CONSTRAINT_NAME = ccu.CONSTRAINT_NAME
ORDER BY rc.CONSTRAINT_NAME)";
            case SqlDdlDialect::Firebird:
                return R"(
SELECT TRIM(rc.rdb$constraint_name) AS "RI_NAME",
       TRIM(rc.rdb$relation_name) AS "PARENT_TABLE",
       TRIM(rc.rdb$const_name_uq) AS "CHILD_TABLE",
       '' AS "PARENT_TAG",
       '' AS "CHILD_TAG",
       '0' AS "UPDATE_RULE",
       '0' AS "DELETE_RULE",
       '' AS "FAIL_TABLE"
FROM rdb$relation_constraints rc
WHERE rc.rdb$constraint_type = 'FOREIGN KEY'
ORDER BY rc.rdb$constraint_name)";
        }
    }
    // SR_MGMNT: synthetic open-access ACL (no SQL DD on URI connections).
    const char* k_perm_tail =
        ", '' AS \"PARENT\", 'PUBLIC' AS \"GRANTEE\", "
        "'2' AS \"SELECT\", '2' AS \"UPDATE\", '2' AS \"INSERT\", "
        "'2' AS \"DELETE\", '' AS \"EXECUTE\", '2' AS \"ACCESS\", "
        "'2' AS \"INHERIT\", '2' AS \"CREATE\", '2' AS \"ALTER\", "
        "'2' AS \"DROP\"";
    if (sys_name == "usergroups") {
        return "SELECT 'PUBLIC' AS \"GROUP_NAME\"";
    }
    if (sys_name == "users") {
        return "SELECT 'PUBLIC' AS \"USER_NAME\"";
    }
    if (sys_name == "usergroupmembers") {
        return "SELECT 'PUBLIC' AS \"GROUP_NAME\", 'PUBLIC' AS \"USER_NAME\"";
    }
    if (sys_name == "permissions") {
        switch (dialect) {
            case SqlDdlDialect::Sqlite:
                return std::string(
                    "SELECT m.name AS \"OBJ_NAME\", '1' AS \"OBJ_TYPE\"") +
                       k_perm_tail +
                       " FROM sqlite_master m "
                       "WHERE m.type='table' AND m.name NOT LIKE 'sqlite_%' "
                       "UNION ALL "
                       "SELECT p.name AS \"OBJ_NAME\", '4' AS \"OBJ_TYPE\", "
                       "m.name AS \"PARENT\", 'PUBLIC' AS \"GRANTEE\", "
                       "'2' AS \"SELECT\", '2' AS \"UPDATE\", '2' AS \"INSERT\", "
                       "'2' AS \"DELETE\", '' AS \"EXECUTE\", '2' AS \"ACCESS\", "
                       "'' AS \"INHERIT\", '2' AS \"CREATE\", '2' AS \"ALTER\", "
                       "'2' AS \"DROP\" "
                       "FROM sqlite_master m "
                       "JOIN pragma_table_info(m.name) p "
                       "WHERE m.type='table' AND m.name NOT LIKE 'sqlite_%'";
            case SqlDdlDialect::Postgres:
                return std::string(
                    "SELECT table_name AS \"OBJ_NAME\", '1' AS \"OBJ_TYPE\"") +
                       k_perm_tail +
                       " FROM information_schema.tables "
                       "WHERE table_schema = ANY (current_schemas(true)) "
                       "AND table_type = 'BASE TABLE' "
                       "UNION ALL "
                       "SELECT column_name AS \"OBJ_NAME\", '4' AS \"OBJ_TYPE\", "
                       "table_name AS \"PARENT\", 'PUBLIC' AS \"GRANTEE\", "
                       "'2' AS \"SELECT\", '2' AS \"UPDATE\", '2' AS \"INSERT\", "
                       "'2' AS \"DELETE\", '' AS \"EXECUTE\", '2' AS \"ACCESS\", "
                       "'' AS \"INHERIT\", '2' AS \"CREATE\", '2' AS \"ALTER\", "
                       "'2' AS \"DROP\" "
                       "FROM information_schema.columns "
                       "WHERE table_schema = ANY (current_schemas(true))";
            case SqlDdlDialect::Maria:
                return std::string(
                    "SELECT table_name AS \"OBJ_NAME\", '1' AS \"OBJ_TYPE\"") +
                       k_perm_tail +
                       " FROM information_schema.tables "
                       "WHERE table_schema = DATABASE() "
                       "AND table_type = 'BASE TABLE' "
                       "UNION ALL "
                       "SELECT column_name AS \"OBJ_NAME\", '4' AS \"OBJ_TYPE\", "
                       "table_name AS \"PARENT\", 'PUBLIC' AS \"GRANTEE\", "
                       "'2' AS \"SELECT\", '2' AS \"UPDATE\", '2' AS \"INSERT\", "
                       "'2' AS \"DELETE\", '' AS \"EXECUTE\", '2' AS \"ACCESS\", "
                       "'' AS \"INHERIT\", '2' AS \"CREATE\", '2' AS \"ALTER\", "
                       "'2' AS \"DROP\" "
                       "FROM information_schema.columns "
                       "WHERE table_schema = DATABASE()";
            case SqlDdlDialect::Mssql:
                return std::string(
                    "SELECT TABLE_NAME AS \"OBJ_NAME\", '1' AS \"OBJ_TYPE\"") +
                       k_perm_tail +
                       " FROM INFORMATION_SCHEMA.TABLES "
                       "WHERE TABLE_TYPE = 'BASE TABLE' "
                       "UNION ALL "
                       "SELECT COLUMN_NAME AS \"OBJ_NAME\", '4' AS \"OBJ_TYPE\", "
                       "TABLE_NAME AS \"PARENT\", 'PUBLIC' AS \"GRANTEE\", "
                       "'2' AS \"SELECT\", '2' AS \"UPDATE\", '2' AS \"INSERT\", "
                       "'2' AS \"DELETE\", '' AS \"EXECUTE\", '2' AS \"ACCESS\", "
                       "'' AS \"INHERIT\", '2' AS \"CREATE\", '2' AS \"ALTER\", "
                       "'2' AS \"DROP\" "
                       "FROM INFORMATION_SCHEMA.COLUMNS";
            case SqlDdlDialect::Firebird:
                return std::string(
                    "SELECT TRIM(r.rdb$relation_name) AS \"OBJ_NAME\", "
                    "'1' AS \"OBJ_TYPE\"") +
                       k_perm_tail +
                       " FROM rdb$relations r "
                       "WHERE r.rdb$view_blr IS NULL AND r.rdb$system_flag = 0 "
                       "UNION ALL "
                       "SELECT TRIM(rf.rdb$field_name) AS \"OBJ_NAME\", "
                       "'4' AS \"OBJ_TYPE\", "
                       "TRIM(rf.rdb$relation_name) AS \"PARENT\", "
                       "'PUBLIC' AS \"GRANTEE\", "
                       "'2' AS \"SELECT\", '2' AS \"UPDATE\", '2' AS \"INSERT\", "
                       "'2' AS \"DELETE\", '' AS \"EXECUTE\", '2' AS \"ACCESS\", "
                       "'' AS \"INHERIT\", '2' AS \"CREATE\", '2' AS \"ALTER\", "
                       "'2' AS \"DROP\" "
                       "FROM rdb$relation_fields rf "
                       "JOIN rdb$relations r ON r.rdb$relation_name = "
                       "rf.rdb$relation_name "
                       "WHERE r.rdb$view_blr IS NULL AND r.rdb$system_flag = 0";
        }
    }
    if (sys_name == "effectivepermissions") {
        switch (dialect) {
            case SqlDdlDialect::Sqlite:
                return std::string(
                    "SELECT m.name AS \"OBJ_NAME\", '1' AS \"OBJ_TYPE\", "
                    "'PUBLIC' AS \"GRANTEE\", "
                    "'2' AS \"SELECT\", '2' AS \"UPDATE\", '2' AS \"INSERT\", "
                    "'2' AS \"DELETE\", '' AS \"EXECUTE\", '2' AS \"ACCESS\", "
                    "'2' AS \"INHERIT\", '2' AS \"CREATE\", '2' AS \"ALTER\", "
                    "'2' AS \"DROP\" "
                    "FROM sqlite_master m "
                    "WHERE m.type='table' AND m.name NOT LIKE 'sqlite_%'");
            case SqlDdlDialect::Postgres:
                return R"(
SELECT table_name AS "OBJ_NAME", '1' AS "OBJ_TYPE", 'PUBLIC' AS "GRANTEE",
       '2' AS "SELECT", '2' AS "UPDATE", '2' AS "INSERT", '2' AS "DELETE",
       '' AS "EXECUTE", '2' AS "ACCESS", '2' AS "INHERIT",
       '2' AS "CREATE", '2' AS "ALTER", '2' AS "DROP"
FROM information_schema.tables
WHERE table_schema = ANY (current_schemas(true))
  AND table_type = 'BASE TABLE')";
            case SqlDdlDialect::Maria:
                return R"(
SELECT table_name AS "OBJ_NAME", '1' AS "OBJ_TYPE", 'PUBLIC' AS "GRANTEE",
       '2' AS "SELECT", '2' AS "UPDATE", '2' AS "INSERT", '2' AS "DELETE",
       '' AS "EXECUTE", '2' AS "ACCESS", '2' AS "INHERIT",
       '2' AS "CREATE", '2' AS "ALTER", '2' AS "DROP"
FROM information_schema.tables
WHERE table_schema = DATABASE()
  AND table_type = 'BASE TABLE')";
            case SqlDdlDialect::Mssql:
                return R"(
SELECT TABLE_NAME AS "OBJ_NAME", '1' AS "OBJ_TYPE", 'PUBLIC' AS "GRANTEE",
       '2' AS "SELECT", '2' AS "UPDATE", '2' AS "INSERT", '2' AS "DELETE",
       '' AS "EXECUTE", '2' AS "ACCESS", '2' AS "INHERIT",
       '2' AS "CREATE", '2' AS "ALTER", '2' AS "DROP"
FROM INFORMATION_SCHEMA.TABLES
WHERE TABLE_TYPE = 'BASE TABLE')";
            case SqlDdlDialect::Firebird:
                return R"(
SELECT TRIM(r.rdb$relation_name) AS "OBJ_NAME", '1' AS "OBJ_TYPE",
       'PUBLIC' AS "GRANTEE",
       '2' AS "SELECT", '2' AS "UPDATE", '2' AS "INSERT", '2' AS "DELETE",
       '' AS "EXECUTE", '2' AS "ACCESS", '2' AS "INHERIT",
       '2' AS "CREATE", '2' AS "ALTER", '2' AS "DROP"
FROM rdb$relations r
WHERE r.rdb$view_blr IS NULL AND r.rdb$system_flag = 0)";
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

std::optional<std::string> build_system_catalog_sql(
    SqlDdlDialect dialect,
    const std::string& sys_name) {
    std::string lower = sys_name;
    for (auto& ch : lower) {
        ch = static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch)));
    }
    return catalog_sql(dialect, lower);
}

}  // namespace openads::sql_backend