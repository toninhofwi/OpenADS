#include "doctest.h"

#include "sql_backend/sql_system_catalog.h"

using openads::sql_backend::SqlDdlDialect;
using openads::sql_backend::build_system_catalog_sql;
using openads::sql_backend::rewrite_system_select_sql;

TEST_CASE("sql_system_catalog: views/triggers/procs/functions/links on SQLite") {
    CHECK(build_system_catalog_sql(SqlDdlDialect::Sqlite, "views").has_value());
    CHECK(build_system_catalog_sql(SqlDdlDialect::Sqlite, "triggers").has_value());
    CHECK(build_system_catalog_sql(SqlDdlDialect::Sqlite, "storedprocedures")
              .has_value());
    CHECK(build_system_catalog_sql(SqlDdlDialect::Sqlite, "functions").has_value());
    CHECK(build_system_catalog_sql(SqlDdlDialect::Sqlite, "links").has_value());
}

TEST_CASE("sql_system_catalog: rewrite preserves WHERE on system.permissions") {
    const char* sql =
        "SELECT OBJ_NAME FROM system.permissions "
        "WHERE OBJ_NAME = 'item' AND OBJ_TYPE = '1'";
    auto rewritten =
        rewrite_system_select_sql(SqlDdlDialect::Sqlite, sql);
    REQUIRE(rewritten.has_value());
    CHECK(rewritten->find("WHERE OBJ_NAME = 'item'") != std::string::npos);
    CHECK(rewritten->find("_openads_sys") != std::string::npos);
}

TEST_CASE("sql_system_catalog: non-system SELECT is not rewritten") {
    CHECK_FALSE(rewrite_system_select_sql(
        SqlDdlDialect::Sqlite, "SELECT * FROM items").has_value());
}

#if defined(OPENADS_WITH_SQLITE)
#include <sqlite3.h>

TEST_CASE("sql_system_catalog: rewritten permissions WHERE executes on SQLite") {
    const char* sql =
        "SELECT OBJ_NAME FROM system.permissions "
        "WHERE OBJ_NAME = 'item' AND OBJ_TYPE = '1'";
    auto rewritten =
        rewrite_system_select_sql(SqlDdlDialect::Sqlite, sql);
    REQUIRE(rewritten.has_value());

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db,
                         "CREATE TABLE item (GRP TEXT, DATA TEXT)",
                         nullptr, nullptr, nullptr) == SQLITE_OK);

    sqlite3_stmt* stmt = nullptr;
    const int prep = sqlite3_prepare_v2(
        db, rewritten->c_str(),
        static_cast<int>(rewritten->size()), &stmt, nullptr);
    if (prep != SQLITE_OK) {
        MESSAGE("prepare failed: ", sqlite3_errmsg(db));
        MESSAGE("sql: ", *rewritten);
    }
    REQUIRE(prep == SQLITE_OK);
    int rows = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) ++rows;
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    CHECK(rows == 1);
}
#endif