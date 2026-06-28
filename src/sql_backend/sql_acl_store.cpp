#include "sql_backend/sql_acl_store.h"

#include "engine/data_dict.h"
#include "sql/parser.h"
#include "sql_backend/sql_common.h"

#include <cctype>
#include <cstdint>

namespace openads::sql_backend {

namespace {

using DD = openads::engine::DataDict;

constexpr const char* kAclTable = "OPENADS$ACL";

std::string quote_ident(SqlDdlDialect d, const std::string& name) {
    switch (d) {
        case SqlDdlDialect::Mssql:
            return "[" + name + "]";
        case SqlDdlDialect::Maria:
            return "`" + name + "`";
        default:
            return "\"" + name + "\"";
    }
}

std::string sql_escape(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "''";
        else           out += c;
    }
    out += '\'';
    return out;
}

std::string upper_copy(std::string s) {
    for (auto& ch : s) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return s;
}

uint32_t bits_from_right(const std::string& right) {
    const std::string r = upper_copy(right);
    if      (r == "ALL")       return DD::DD_PERM_FULL;
    else if (r == "SELECT")    return DD::DD_PERM_SELECT;
    else if (r == "INSERT")    return DD::DD_PERM_INSERT;
    else if (r == "UPDATE")    return DD::DD_PERM_UPDATE;
    else if (r == "DELETE")    return DD::DD_PERM_DELETE;
    else if (r == "EXECUTE")   return DD::DD_PERM_EXECUTE;
    else if (r == "REFERENCE" || r == "REFERENCES")
                               return DD::DD_PERM_REFERENCE;
    return DD::DD_PERM_FULL;
}

uint32_t revoke_mask_from_right(const std::string& right) {
    const std::string r = upper_copy(right);
    if (r == "ALL") return DD::DD_PERM_FULL;
    return bits_from_right(right);
}

bool grantee_is_group(const std::string& grantee) {
    if (grantee.empty()) return false;
    if (upper_copy(grantee) == "PUBLIC") return true;
    for (char c : grantee) {
        if (c >= 'a' && c <= 'z') return false;
    }
    return true;
}

std::string perm_col_sql(const char* col_name, uint32_t bit) {
    return std::string(
        "CASE WHEN (bitmask & ") + std::to_string(bit) +
        ") != 0 THEN CASE WHEN is_group != 0 THEN '1' ELSE '2' END "
        "ELSE '0' END AS \"" + col_name + "\"";
}

util::Result<void> ensure_acl_table(SqlDdlDialect dialect, SqlExecFn exec_sql) {
    return exec_sql(acl_table_ddl(dialect));
}

std::string grant_upsert_sql(SqlDdlDialect dialect,
                             const std::string& obj_type,
                             const std::string& obj_name,
                             const std::string& parent,
                             const std::string& grantee,
                             bool is_group,
                             uint32_t add_bits) {
    const std::string qtab = quote_ident(dialect, kAclTable);
    const std::string ig   = is_group ? "1" : "0";
    const std::string bits = std::to_string(add_bits);
    switch (dialect) {
        case SqlDdlDialect::Sqlite:
            return "INSERT INTO " + qtab +
                   " (obj_type, obj_name, parent, grantee, is_group, bitmask) "
                   "VALUES (" + sql_escape(obj_type) + ", " +
                   sql_escape(obj_name) + ", " + sql_escape(parent) + ", " +
                   sql_escape(grantee) + ", " + ig + ", " + bits + ") "
                   "ON CONFLICT(obj_type, obj_name, grantee) DO UPDATE SET "
                   "bitmask = " + qtab + ".bitmask | " + bits + ", "
                   "parent = excluded.parent, is_group = excluded.is_group";
        case SqlDdlDialect::Postgres:
            return "INSERT INTO " + qtab +
                   " (obj_type, obj_name, parent, grantee, is_group, bitmask) "
                   "VALUES (" + sql_escape(obj_type) + ", " +
                   sql_escape(obj_name) + ", " + sql_escape(parent) + ", " +
                   sql_escape(grantee) + ", " + ig + ", " + bits + ") "
                   "ON CONFLICT (obj_type, obj_name, grantee) DO UPDATE SET "
                   "bitmask = " + qtab + ".bitmask | EXCLUDED.bitmask, "
                   "parent = EXCLUDED.parent, is_group = EXCLUDED.is_group";
        case SqlDdlDialect::Maria:
            return "INSERT INTO " + qtab +
                   " (obj_type, obj_name, parent, grantee, is_group, bitmask) "
                   "VALUES (" + sql_escape(obj_type) + ", " +
                   sql_escape(obj_name) + ", " + sql_escape(parent) + ", " +
                   sql_escape(grantee) + ", " + ig + ", " + bits + ") "
                   "ON DUPLICATE KEY UPDATE "
                   "bitmask = bitmask | " + bits + ", "
                   "parent = VALUES(parent), is_group = VALUES(is_group)";
        case SqlDdlDialect::Mssql:
            return "MERGE " + qtab + " AS t "
                   "USING (SELECT " + sql_escape(obj_type) + " AS obj_type, " +
                   sql_escape(obj_name) + " AS obj_name, " +
                   sql_escape(grantee) + " AS grantee, " + bits +
                   " AS add_bits, " + sql_escape(parent) + " AS parent, " +
                   ig + " AS is_group) AS s "
                   "ON t.obj_type = s.obj_type AND t.obj_name = s.obj_name "
                   "AND t.grantee = s.grantee "
                   "WHEN MATCHED THEN UPDATE SET "
                   "bitmask = t.bitmask | s.add_bits, parent = s.parent, "
                   "is_group = s.is_group "
                   "WHEN NOT MATCHED THEN INSERT "
                   "(obj_type, obj_name, parent, grantee, is_group, bitmask) "
                   "VALUES (s.obj_type, s.obj_name, s.parent, s.grantee, "
                   "s.is_group, s.add_bits);";
        case SqlDdlDialect::Firebird:
            return "MERGE INTO " + qtab + " t "
                   "USING (SELECT " + sql_escape(obj_type) +
                   " obj_type, " + sql_escape(obj_name) + " obj_name, " +
                   sql_escape(grantee) + " grantee, " + bits +
                   " add_bits, " + sql_escape(parent) + " parent, " + ig +
                   " is_group FROM RDB$DATABASE) s "
                   "ON t.obj_type = s.obj_type AND t.obj_name = s.obj_name "
                   "AND t.grantee = s.grantee "
                   "WHEN MATCHED THEN UPDATE SET "
                   "bitmask = BIN_OR(t.bitmask, s.add_bits), "
                   "parent = s.parent, is_group = s.is_group "
                   "WHEN NOT MATCHED THEN INSERT "
                   "(obj_type, obj_name, parent, grantee, is_group, bitmask) "
                   "VALUES (s.obj_type, s.obj_name, s.parent, s.grantee, "
                   "s.is_group, s.add_bits)";
    }
    return {};
}

std::string revoke_sql(SqlDdlDialect dialect,
                       const std::string& obj_type,
                       const std::string& obj_name,
                       const std::string& grantee,
                       uint32_t revoke_bits) {
    const std::string qtab = quote_ident(dialect, kAclTable);
    const std::string mask = std::to_string(revoke_bits);
    const std::string where =
        " WHERE obj_type = " + sql_escape(obj_type) +
        " AND obj_name = " + sql_escape(obj_name) +
        " AND grantee = " + sql_escape(grantee);
    switch (dialect) {
        case SqlDdlDialect::Firebird:
            return "UPDATE " + qtab + " SET bitmask = BIN_AND(bitmask, -1 - " +
                   mask + ")" + where;
        default:
            return "UPDATE " + qtab + " SET bitmask = bitmask & ~" + mask +
                   where;
    }
}

}  // namespace

std::string acl_table_ddl(SqlDdlDialect dialect) {
    const std::string qtab = quote_ident(dialect, kAclTable);
    switch (dialect) {
        case SqlDdlDialect::Mssql:
            return "IF NOT EXISTS (SELECT 1 FROM sys.tables WHERE name = '" +
                   std::string(kAclTable) + "') CREATE TABLE " + qtab +
                   " (obj_type NVARCHAR(8) NOT NULL, "
                   "obj_name NVARCHAR(200) NOT NULL, "
                   "parent NVARCHAR(200) NOT NULL DEFAULT '', "
                   "grantee NVARCHAR(200) NOT NULL, "
                   "is_group INT NOT NULL DEFAULT 0, "
                   "bitmask INT NOT NULL DEFAULT 0, "
                   "PRIMARY KEY (obj_type, obj_name, grantee))";
        case SqlDdlDialect::Maria:
            return "CREATE TABLE IF NOT EXISTS " + qtab +
                   " (obj_type VARCHAR(8) NOT NULL, "
                   "obj_name VARCHAR(200) NOT NULL, "
                   "parent VARCHAR(200) NOT NULL DEFAULT '', "
                   "grantee VARCHAR(200) NOT NULL, "
                   "is_group TINYINT NOT NULL DEFAULT 0, "
                   "bitmask INT UNSIGNED NOT NULL DEFAULT 0, "
                   "PRIMARY KEY (obj_type, obj_name, grantee))";
        case SqlDdlDialect::Firebird:
            return "EXECUTE BLOCK AS BEGIN IF (NOT EXISTS(SELECT 1 FROM "
                   "RDB$RELATIONS WHERE TRIM(RDB$RELATION_NAME) = '" +
                   std::string(kAclTable) + "')) THEN EXECUTE STATEMENT '" +
                   "CREATE TABLE " + qtab +
                   " (obj_type VARCHAR(8) NOT NULL, "
                   "obj_name VARCHAR(200) NOT NULL, "
                   "parent VARCHAR(200) DEFAULT '''', "
                   "grantee VARCHAR(200) NOT NULL, "
                   "is_group SMALLINT DEFAULT 0, "
                   "bitmask INTEGER DEFAULT 0, "
                   "PRIMARY KEY (obj_type, obj_name, grantee))'; END";
        case SqlDdlDialect::Postgres:
        case SqlDdlDialect::Sqlite:
        default:
            return "CREATE TABLE IF NOT EXISTS " + qtab +
                   " (obj_type TEXT NOT NULL, "
                   "obj_name TEXT NOT NULL, "
                   "parent TEXT NOT NULL DEFAULT '', "
                   "grantee TEXT NOT NULL, "
                   "is_group INTEGER NOT NULL DEFAULT 0, "
                   "bitmask INTEGER NOT NULL DEFAULT 0, "
                   "PRIMARY KEY (obj_type, obj_name, grantee))";
    }
}

std::string acl_permissions_select_sql(SqlDdlDialect dialect) {
    const std::string qtab = quote_ident(dialect, kAclTable);
    return std::string(
        "SELECT obj_name AS \"OBJ_NAME\", obj_type AS \"OBJ_TYPE\", "
        "parent AS \"PARENT\", grantee AS \"GRANTEE\", ") +
           perm_col_sql("SELECT", DD::DD_PERM_SELECT) + ", " +
           perm_col_sql("UPDATE", DD::DD_PERM_UPDATE) + ", " +
           perm_col_sql("INSERT", DD::DD_PERM_INSERT) + ", " +
           perm_col_sql("DELETE", DD::DD_PERM_DELETE) + ", " +
           perm_col_sql("EXECUTE", DD::DD_PERM_EXECUTE) + ", " +
           "'2' AS \"ACCESS\", "
           "CASE WHEN is_group != 0 THEN '' ELSE "
           "CASE WHEN (bitmask & " +
           std::to_string(DD::DD_PERM_GRANT) + ") != 0 THEN '2' ELSE '0' END "
           "END AS \"INHERIT\", "
           "CASE WHEN (bitmask & 128) != 0 THEN "
           "CASE WHEN is_group != 0 THEN '1' ELSE '2' END ELSE '0' END "
           "AS \"CREATE\", "
           "CASE WHEN (bitmask & 256) != 0 THEN "
           "CASE WHEN is_group != 0 THEN '1' ELSE '2' END ELSE '0' END "
           "AS \"ALTER\", "
           "CASE WHEN (bitmask & 512) != 0 THEN "
           "CASE WHEN is_group != 0 THEN '1' ELSE '2' END ELSE '0' END "
           "AS \"DROP\" "
           "FROM " + qtab;
}

std::optional<util::Result<void>> try_sql_acl_statement(
    const std::string& sql,
    SqlDdlDialect dialect,
    SqlExecFn exec_sql) {
    const bool is_grant = openads::sql::sql_is_grant(sql);
    const bool is_revoke = openads::sql::sql_is_revoke(sql);
    if (!is_grant && !is_revoke) return std::nullopt;

    openads::sql::GrantStmt g;
    if (is_grant) {
        auto gs = openads::sql::parse_grant(sql);
        if (!gs) return gs.error();
        g = gs.value();
    } else {
        auto gs = openads::sql::parse_revoke(sql);
        if (!gs) return gs.error();
        g = gs.value();
    }

    if (auto r = ensure_acl_table(dialect, exec_sql); !r) return r.error();

    const std::string obj_type = g.column.empty() ? "1" : "4";
    const std::string obj_name = g.column.empty() ? g.object : g.column;
    const std::string parent   = g.column.empty() ? "" : g.object;
    const std::string grantee  = g.principal;
    const bool is_grp          = grantee_is_group(grantee);

    if (is_grant) {
        const uint32_t add_bits = bits_from_right(g.right);
        if (auto r = exec_sql(grant_upsert_sql(
                dialect, obj_type, obj_name, parent, grantee, is_grp, add_bits));
            !r) {
            return r.error();
        }
        return util::Result<void>{};
    }

    const uint32_t revoke_bits = revoke_mask_from_right(g.right);
    if (auto r = exec_sql(revoke_sql(dialect, obj_type, obj_name, grantee,
                                     revoke_bits));
        !r) {
        return r.error();
    }
    return util::Result<void>{};
}

}  // namespace openads::sql_backend