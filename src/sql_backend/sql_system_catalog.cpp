#include "sql_backend/sql_system_catalog.h"

#include "sql/parser.h"
#include "sql_backend/sql_acl_store.h"

#include <cctype>
#include <cstring>

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
            case SqlDdlDialect::Oracle:
                return std::string(
                    "SELECT table_name AS \"Name\", "
                    "table_name AS \"Table_Relative_Path\", "
                    "'1' AS \"Table_Type\"") +
                       k_tables_tail +
                       " FROM user_tables "
                       "WHERE table_name NOT LIKE 'OPENADS$%' "
                       "ORDER BY table_name";
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
            case SqlDdlDialect::Oracle:
                return R"(
SELECT utc.table_name AS "TABLE_NAME",
       utc.column_name AS "COL_NAME",
       CAST(utc.column_id AS VARCHAR2(10)) AS "COL_NUM",
       CASE
         WHEN utc.data_type IN ('NUMBER','FLOAT','BINARY_FLOAT',
                                'BINARY_DOUBLE') THEN 'N'
         ELSE 'C'
       END AS "COL_TYPE",
       CAST(COALESCE(utc.data_length,
         CASE WHEN utc.data_type IN ('NUMBER','FLOAT','BINARY_FLOAT',
                                     'BINARY_DOUBLE') THEN 0 ELSE 10 END)
         AS VARCHAR2(10)) AS "COL_LEN",
       CAST(COALESCE(utc.data_scale, 0) AS VARCHAR2(10)) AS "COL_DEC"
FROM user_tab_columns utc
JOIN user_tables ut ON ut.table_name = utc.table_name
WHERE ut.table_name NOT LIKE 'OPENADS$%'
ORDER BY utc.table_name, utc.column_id)";
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
            case SqlDdlDialect::Oracle:
                return R"(
SELECT ucc.table_name AS "TABLE_NAME",
       ucc.column_name AS "COLUMN_NAME",
       CAST(ucc.position AS VARCHAR2(10)) AS "KEY_SEQ",
       uc.constraint_name AS "PK_NAME"
FROM user_constraints uc
JOIN user_cons_columns ucc
  ON uc.constraint_name = ucc.constraint_name
WHERE uc.constraint_type = 'P'
ORDER BY ucc.table_name, ucc.position)";
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
            case SqlDdlDialect::Oracle:
                return R"(
SELECT ui.table_name AS "TABLE_NAME",
       ui.index_name AS "INDEX_FILE",
       '' AS "COMMENT"
FROM user_indexes ui
ORDER BY ui.table_name, ui.index_name)";
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
            case SqlDdlDialect::Oracle:
                return R"(
SELECT fk.constraint_name AS "RI_NAME",
       pk.table_name AS "PARENT",
       fk.table_name AS "CHILD",
       '' AS "PARENT_TAG",
       '' AS "CHILD_TAG",
       '' AS "UPDATE_OPT",
       fk.delete_rule AS "DELETE_OPT",
       '' AS "FAIL_TABLE"
FROM user_constraints fk
JOIN user_constraints pk
  ON pk.constraint_name = fk.r_constraint_name
WHERE fk.constraint_type = 'R'
ORDER BY fk.constraint_name)";
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
            case SqlDdlDialect::Oracle:
                return R"(
SELECT fk.constraint_name AS "RI_NAME",
       pk.table_name AS "PARENT_TABLE",
       fk.table_name AS "CHILD_TABLE",
       '' AS "PARENT_TAG",
       '' AS "CHILD_TAG",
       '0' AS "UPDATE_RULE",
       '0' AS "DELETE_RULE",
       '' AS "FAIL_TABLE"
FROM user_constraints fk
JOIN user_constraints pk
  ON pk.constraint_name = fk.r_constraint_name
WHERE fk.constraint_type = 'R'
ORDER BY fk.constraint_name)";
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
        return acl_groups_catalog_sql(dialect);
    }
    if (sys_name == "users") {
        return acl_users_catalog_sql(dialect);
    }
    if (sys_name == "usergroupmembers") {
        return acl_members_catalog_sql(dialect);
    }
    if (sys_name == "permissions") {
        const std::string acl_union =
            " UNION ALL " + acl_permissions_select_sql(dialect);
        switch (dialect) {
            case SqlDdlDialect::Sqlite:
                return std::string(
                    "SELECT m.name AS \"OBJ_NAME\", '1' AS \"OBJ_TYPE\"") +
                       k_perm_tail +
                       " FROM sqlite_master m "
                       "WHERE m.type='table' AND m.name NOT LIKE 'sqlite_%' "
                       "AND m.name NOT LIKE 'OPENADS$%' "
                       "UNION ALL "
                       "SELECT p.name AS \"OBJ_NAME\", '4' AS \"OBJ_TYPE\", "
                       "m.name AS \"PARENT\", 'PUBLIC' AS \"GRANTEE\", "
                       "'2' AS \"SELECT\", '2' AS \"UPDATE\", '2' AS \"INSERT\", "
                       "'2' AS \"DELETE\", '' AS \"EXECUTE\", '2' AS \"ACCESS\", "
                       "'' AS \"INHERIT\", '2' AS \"CREATE\", '2' AS \"ALTER\", "
                       "'2' AS \"DROP\" "
                       "FROM sqlite_master m "
                       "JOIN pragma_table_info(m.name) p "
                       "WHERE m.type='table' AND m.name NOT LIKE 'sqlite_%' "
                       "AND m.name NOT LIKE 'OPENADS$%'" +
                       acl_union;
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
                       "WHERE table_schema = ANY (current_schemas(true))" +
                       acl_union;
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
                       "WHERE table_schema = DATABASE()" +
                       acl_union;
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
                       "FROM INFORMATION_SCHEMA.COLUMNS" +
                       acl_union;
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
                       "WHERE r.rdb$view_blr IS NULL AND r.rdb$system_flag = 0" +
                       acl_union;
            case SqlDdlDialect::Oracle:
                return std::string(
                    "SELECT table_name AS \"OBJ_NAME\", '1' AS \"OBJ_TYPE\"") +
                       k_perm_tail +
                       " FROM user_tables "
                       "WHERE table_name NOT LIKE 'OPENADS$%' "
                       "UNION ALL "
                       "SELECT column_name AS \"OBJ_NAME\", '4' AS \"OBJ_TYPE\", "
                       "table_name AS \"PARENT\", 'PUBLIC' AS \"GRANTEE\", "
                       "'2' AS \"SELECT\", '2' AS \"UPDATE\", '2' AS \"INSERT\", "
                       "'2' AS \"DELETE\", '' AS \"EXECUTE\", '2' AS \"ACCESS\", "
                       "'' AS \"INHERIT\", '2' AS \"CREATE\", '2' AS \"ALTER\", "
                       "'2' AS \"DROP\" "
                       "FROM user_tab_columns "
                       "WHERE table_name NOT LIKE 'OPENADS$%'" +
                       acl_union;
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
            case SqlDdlDialect::Oracle:
                return R"(
SELECT table_name AS "OBJ_NAME", '1' AS "OBJ_TYPE", 'PUBLIC' AS "GRANTEE",
       '2' AS "SELECT", '2' AS "UPDATE", '2' AS "INSERT", '2' AS "DELETE",
       '' AS "EXECUTE", '2' AS "ACCESS", '2' AS "INHERIT",
       '2' AS "CREATE", '2' AS "ALTER", '2' AS "DROP"
FROM user_tables
WHERE table_name NOT LIKE 'OPENADS$%')";
        }
    }
    if (sys_name == "links") {
        return "SELECT '' AS \"LINK_NAME\", '' AS \"LINK_PATH\", "
               "'' AS \"LINK_USER\" WHERE 1=0";
    }
    if (sys_name == "views") {
        switch (dialect) {
            case SqlDdlDialect::Sqlite:
                return R"(
SELECT name AS "VIEW_NAME", IFNULL(sql, '') AS "VIEW_SQL", '' AS "COMMENT"
FROM sqlite_master
WHERE type = 'view'
ORDER BY name)";
            case SqlDdlDialect::Postgres:
                return R"(
SELECT table_name AS "VIEW_NAME",
       COALESCE(view_definition, '') AS "VIEW_SQL", '' AS "COMMENT"
FROM information_schema.views
WHERE table_schema = ANY (current_schemas(true))
ORDER BY table_name)";
            case SqlDdlDialect::Maria:
                return R"(
SELECT table_name AS "VIEW_NAME",
       COALESCE(view_definition, '') AS "VIEW_SQL", '' AS "COMMENT"
FROM information_schema.views
WHERE table_schema = DATABASE()
ORDER BY table_name)";
            case SqlDdlDialect::Mssql:
                return R"(
SELECT TABLE_NAME AS "VIEW_NAME",
       CAST('' AS NVARCHAR(250)) AS "VIEW_SQL", '' AS "COMMENT"
FROM INFORMATION_SCHEMA.VIEWS
ORDER BY TABLE_NAME)";
            case SqlDdlDialect::Firebird:
                return R"(
SELECT TRIM(r.rdb$relation_name) AS "VIEW_NAME",
       '' AS "VIEW_SQL", '' AS "COMMENT"
FROM rdb$relations r
WHERE r.rdb$view_blr IS NOT NULL AND r.rdb$system_flag = 0
ORDER BY r.rdb$relation_name)";
            case SqlDdlDialect::Oracle:
                return R"(
SELECT view_name AS "VIEW_NAME",
       COALESCE(text, '') AS "VIEW_SQL", '' AS "COMMENT"
FROM user_views
ORDER BY view_name)";
        }
    }
    if (sys_name == "triggers") {
        switch (dialect) {
            case SqlDdlDialect::Sqlite:
                return R"(
SELECT name AS "TRIG_NAME", tbl_name AS "TABLE_NAME",
       '0' AS "EVENT_MASK", '' AS "TIMING", '' AS "EVENT",
       IFNULL(sql, '') AS "CONTAINER", '' AS "PROC",
       '0' AS "PRIORITY", 'T' AS "ENABLED", '0' AS "TRIG_OPTIONS"
FROM sqlite_master
WHERE type = 'trigger'
ORDER BY name)";
            case SqlDdlDialect::Postgres:
                return R"(
SELECT trigger_name AS "TRIG_NAME",
       event_object_table AS "TABLE_NAME",
       CASE event_manipulation
         WHEN 'INSERT' THEN '1' WHEN 'UPDATE' THEN '2' WHEN 'DELETE' THEN '3'
         ELSE '0' END AS "EVENT_MASK",
       CASE action_timing
         WHEN 'BEFORE' THEN 'BEFORE' WHEN 'AFTER' THEN 'AFTER' ELSE '' END
         AS "TIMING",
       event_manipulation AS "EVENT",
       COALESCE(action_statement, '') AS "CONTAINER",
       '' AS "PROC", '0' AS "PRIORITY", 'T' AS "ENABLED", '0' AS "TRIG_OPTIONS"
FROM information_schema.triggers
WHERE trigger_schema = ANY (current_schemas(true))
ORDER BY trigger_name)";
            case SqlDdlDialect::Maria:
                return R"(
SELECT trigger_name AS "TRIG_NAME",
       event_object_table AS "TABLE_NAME",
       CASE event_manipulation
         WHEN 'INSERT' THEN '1' WHEN 'UPDATE' THEN '2' WHEN 'DELETE' THEN '3'
         ELSE '0' END AS "EVENT_MASK",
       CASE action_timing
         WHEN 'BEFORE' THEN 'BEFORE' WHEN 'AFTER' THEN 'AFTER' ELSE '' END
         AS "TIMING",
       event_manipulation AS "EVENT",
       COALESCE(action_statement, '') AS "CONTAINER",
       '' AS "PROC", '0' AS "PRIORITY", 'T' AS "ENABLED", '0' AS "TRIG_OPTIONS"
FROM information_schema.triggers
WHERE trigger_schema = DATABASE()
ORDER BY trigger_name)";
            case SqlDdlDialect::Mssql:
                return R"(
SELECT tr.name AS "TRIG_NAME",
       OBJECT_NAME(tr.parent_id) AS "TABLE_NAME",
       '0' AS "EVENT_MASK", '' AS "TIMING", '' AS "EVENT",
       '' AS "CONTAINER", '' AS "PROC",
       '0' AS "PRIORITY", 'T' AS "ENABLED", '0' AS "TRIG_OPTIONS"
FROM sys.triggers tr
WHERE tr.parent_class = 1
ORDER BY tr.name)";
            case SqlDdlDialect::Firebird:
                return R"(
SELECT TRIM(tg.rdb$trigger_name) AS "TRIG_NAME",
       TRIM(tg.rdb$relation_name) AS "TABLE_NAME",
       '0' AS "EVENT_MASK", '' AS "TIMING", '' AS "EVENT",
       '' AS "CONTAINER", '' AS "PROC",
       '0' AS "PRIORITY", 'T' AS "ENABLED", '0' AS "TRIG_OPTIONS"
FROM rdb$triggers tg
WHERE tg.rdb$system_flag = 0
ORDER BY tg.rdb$trigger_name)";
            case SqlDdlDialect::Oracle:
                return R"(
SELECT trigger_name AS "TRIG_NAME",
       table_name AS "TABLE_NAME",
       '0' AS "EVENT_MASK",
       CASE triggering_event
         WHEN 'INSERT' THEN 'BEFORE'
         WHEN 'UPDATE' THEN 'BEFORE'
         WHEN 'DELETE' THEN 'BEFORE'
         ELSE '' END AS "TIMING",
       triggering_event AS "EVENT",
       '' AS "CONTAINER", '' AS "PROC",
       '0' AS "PRIORITY", 'T' AS "ENABLED", '0' AS "TRIG_OPTIONS"
FROM user_triggers
ORDER BY trigger_name)";
        }
    }
    if (sys_name == "storedprocedures") {
        switch (dialect) {
            case SqlDdlDialect::Sqlite:
                return "SELECT '' AS \"PROC_NAME\", '' AS \"CONTAINER\", "
                       "'' AS \"PROCEDURE\", '' AS \"INPUT\", '' AS \"OUTPUT\" "
                       "WHERE 1=0";
            case SqlDdlDialect::Postgres:
                return R"(
SELECT routine_name AS "PROC_NAME", '' AS "CONTAINER",
       routine_name AS "PROCEDURE", '' AS "INPUT", '' AS "OUTPUT"
FROM information_schema.routines
WHERE routine_schema = ANY (current_schemas(true))
  AND routine_type = 'PROCEDURE'
ORDER BY routine_name)";
            case SqlDdlDialect::Maria:
                return R"(
SELECT routine_name AS "PROC_NAME", '' AS "CONTAINER",
       routine_name AS "PROCEDURE", '' AS "INPUT", '' AS "OUTPUT"
FROM information_schema.routines
WHERE routine_schema = DATABASE()
  AND routine_type = 'PROCEDURE'
ORDER BY routine_name)";
            case SqlDdlDialect::Mssql:
                return R"(
SELECT ROUTINE_NAME AS "PROC_NAME", '' AS "CONTAINER",
       ROUTINE_NAME AS "PROCEDURE", '' AS "INPUT", '' AS "OUTPUT"
FROM INFORMATION_SCHEMA.ROUTINES
WHERE ROUTINE_TYPE = 'PROCEDURE'
ORDER BY ROUTINE_NAME)";
            case SqlDdlDialect::Firebird:
                return R"(
SELECT TRIM(p.rdb$procedure_name) AS "PROC_NAME", '' AS "CONTAINER",
       TRIM(p.rdb$procedure_name) AS "PROCEDURE", '' AS "INPUT", '' AS "OUTPUT"
FROM rdb$procedures p
WHERE p.rdb$system_flag = 0
ORDER BY p.rdb$procedure_name)";
            case SqlDdlDialect::Oracle:
                return R"(
SELECT object_name AS "PROC_NAME", '' AS "CONTAINER",
       object_name AS "PROCEDURE", '' AS "INPUT", '' AS "OUTPUT"
FROM user_procedures
WHERE object_type = 'PROCEDURE'
ORDER BY object_name)";
        }
    }
    if (sys_name == "functions") {
        switch (dialect) {
            case SqlDdlDialect::Sqlite:
                return "SELECT '' AS \"FUNC_NAME\", '' AS \"CONTAINER\", "
                       "'' AS \"RET_TYPE\", '' AS \"IN_PARAMS\", "
                       "'' AS \"FUNC_BODY\", '' AS \"COMMENT\" WHERE 1=0";
            case SqlDdlDialect::Postgres:
                return R"(
SELECT routine_name AS "FUNC_NAME", '' AS "CONTAINER",
       data_type AS "RET_TYPE", '' AS "IN_PARAMS",
       '' AS "FUNC_BODY", '' AS "COMMENT"
FROM information_schema.routines
WHERE routine_schema = ANY (current_schemas(true))
  AND routine_type = 'FUNCTION'
ORDER BY routine_name)";
            case SqlDdlDialect::Maria:
                return R"(
SELECT routine_name AS "FUNC_NAME", '' AS "CONTAINER",
       data_type AS "RET_TYPE", '' AS "IN_PARAMS",
       '' AS "FUNC_BODY", '' AS "COMMENT"
FROM information_schema.routines
WHERE routine_schema = DATABASE()
  AND routine_type = 'FUNCTION'
ORDER BY routine_name)";
            case SqlDdlDialect::Mssql:
                return R"(
SELECT ROUTINE_NAME AS "FUNC_NAME", '' AS "CONTAINER",
       DATA_TYPE AS "RET_TYPE", '' AS "IN_PARAMS",
       '' AS "FUNC_BODY", '' AS "COMMENT"
FROM INFORMATION_SCHEMA.ROUTINES
WHERE ROUTINE_TYPE = 'FUNCTION'
ORDER BY ROUTINE_NAME)";
            case SqlDdlDialect::Firebird:
                return R"(
SELECT TRIM(f.rdb$function_name) AS "FUNC_NAME", '' AS "CONTAINER",
       '' AS "RET_TYPE", '' AS "IN_PARAMS",
       '' AS "FUNC_BODY", '' AS "COMMENT"
FROM rdb$functions f
WHERE f.rdb$system_flag = 0
ORDER BY f.rdb$function_name)";
            case SqlDdlDialect::Oracle:
                return R"(
SELECT object_name AS "FUNC_NAME", '' AS "CONTAINER",
       '' AS "RET_TYPE", '' AS "IN_PARAMS",
       '' AS "FUNC_BODY", '' AS "COMMENT"
FROM user_objects
WHERE object_type = 'FUNCTION'
ORDER BY object_name)";
        }
    }
    return std::nullopt;
}

static std::optional<std::size_t> system_from_clause_end(
    const std::string& sql,
    const std::string& system_table) {
    const std::string lower    = lower_copy(sql);
    const std::string needle   = "from " + lower_copy(system_table);
    const std::size_t   from_pos = lower.find(needle);
    if (from_pos == std::string::npos) return std::nullopt;
    std::size_t end = from_pos + needle.size();
    while (end < sql.size() &&
           std::isspace(static_cast<unsigned char>(sql[end]))) {
        ++end;
    }
    if (end < sql.size() &&
        (std::isalnum(static_cast<unsigned char>(sql[end])) ||
         sql[end] == '_')) {
        const std::string rest = lower_copy(sql.substr(end));
        static constexpr const char* k_clause_keywords[] = {
            "where", "order", "group", "having", "limit", "union", "offset",
        };
        bool clause = false;
        for (const char* kw : k_clause_keywords) {
            const std::size_t n = std::strlen(kw);
            if (rest.size() >= n && rest.compare(0, n, kw) == 0 &&
                (rest.size() == n ||
                 std::isspace(static_cast<unsigned char>(rest[n])))) {
                clause = true;
                break;
            }
        }
        if (!clause) {
            while (end < sql.size() &&
                   (std::isalnum(static_cast<unsigned char>(sql[end])) ||
                    sql[end] == '_')) {
                ++end;
            }
        }
    }
    return end;
}

}  // namespace

std::optional<std::string> rewrite_system_select_sql(
    SqlDdlDialect dialect,
    const std::string& sql) {
    auto sys_name = system_table_from_select(sql);
    if (!sys_name) return std::nullopt;
    auto sel = openads::sql::parse_select(sql);
    if (!sel) return std::nullopt;
    const auto& st = sel.value();
    auto catalog = catalog_sql(dialect, *sys_name);
    if (!catalog) return std::nullopt;
    const std::size_t from_pos =
        lower_copy(sql).find("from " + lower_copy(st.table));
    if (from_pos == std::string::npos) return *catalog;
    auto end = system_from_clause_end(sql, st.table);
    if (!end) return *catalog;
    return sql.substr(0, from_pos) + " FROM (" + *catalog +
           ") AS _openads_sys " + sql.substr(*end);
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