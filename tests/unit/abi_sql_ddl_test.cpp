#include "doctest.h"
#include "openads/ace.h"
#include "sql/parser.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

TEST_CASE("M10.9 parse_create_table") {
    auto r = openads::sql::parse_create_table(
        "CREATE TABLE customers (NAME Character(20), AGE Numeric(3,0))");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "customers");
    REQUIRE(r.value().columns.size() == 2);
    CHECK(r.value().columns[0].name == "NAME");
    CHECK(r.value().columns[0].type == "Character");
    CHECK(r.value().columns[0].length == 20);
    CHECK(r.value().columns[1].name == "AGE");
    CHECK(r.value().columns[1].type == "Numeric");
    CHECK(r.value().columns[1].length == 3);
}

TEST_CASE("M10.9 parse_create_index with DESCENDING + UNIQUE") {
    auto r = openads::sql::parse_create_index(
        "CREATE INDEX TAGORD ON data (UPPER(NAME)) DESC UNIQUE");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "data");
    CHECK(r.value().tag   == "TAGORD");
    CHECK(r.value().expression == "UPPER(NAME)");
    CHECK(r.value().descending);
    CHECK(r.value().unique);
}

TEST_CASE("M10.9 sql_is_create_table / sql_is_create_index dispatch") {
    CHECK(openads::sql::sql_is_create_table("CREATE TABLE x (y N(3))"));
    CHECK(openads::sql::sql_is_create_index("CREATE INDEX t ON x (y)"));
    CHECK_FALSE(openads::sql::sql_is_create_table("CREATE INDEX …"));
    CHECK_FALSE(openads::sql::sql_is_create_index("CREATE TABLE …"));
    CHECK_FALSE(openads::sql::sql_is_create_table("SELECT * FROM x"));
}

TEST_CASE("DDL keyword dispatch distinguishes DROP TABLE vs DROP INDEX") {
    CHECK(openads::sql::sql_is_drop_table("DROP TABLE customers"));
    CHECK(openads::sql::sql_is_drop_index("DROP INDEX tagord ON data"));
    CHECK_FALSE(openads::sql::sql_is_drop_table("DROP INDEX tagord ON data"));
    CHECK_FALSE(openads::sql::sql_is_drop_index("DROP TABLE customers"));
    CHECK(openads::sql::sql_is_alter_table(
        "ALTER TABLE t ADD COLUMN nome Character(20)"));
}

TEST_CASE("M10.9 CREATE TABLE through AdsExecuteSQLDirect produces a usable table") {
    auto dir = fs::temp_directory_path() / "openads_m10_9_ct";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[200] =
        "CREATE TABLE notes (TAG Character(8), AGE Numeric(3))";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    CHECK(hCur == 0);
    CHECK(fs::exists(dir / "notes.dbf"));

    // INSERT a row and read it back.
    UNSIGNED8 sql2[200] =
        "INSERT INTO notes (TAG, AGE) VALUES ('hello', 42)";
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql2, &hCur) == 0);

    UNSIGNED8 leaf[16] = "notes";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &cnt) == 0);
    CHECK(cnt == 1);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.9 CREATE INDEX through AdsExecuteSQLDirect builds a CDX tag") {
    auto dir = fs::temp_directory_path() / "openads_m10_9_ci";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 ct[160] =
        "CREATE TABLE data (TAG Character(4))";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, ct, &hCur) == 0);

    UNSIGNED8 ins1[80] = "INSERT INTO data (TAG) VALUES ('BBBB')";
    UNSIGNED8 ins2[80] = "INSERT INTO data (TAG) VALUES ('AAAA')";
    UNSIGNED8 ins3[80] = "INSERT INTO data (TAG) VALUES ('CCCC')";
    REQUIRE(AdsExecuteSQLDirect(hStmt, ins1, &hCur) == 0);
    REQUIRE(AdsExecuteSQLDirect(hStmt, ins2, &hCur) == 0);
    REQUIRE(AdsExecuteSQLDirect(hStmt, ins3, &hCur) == 0);

    UNSIGNED8 ci[160] = "CREATE INDEX TAGORD ON data (TAG)";
    REQUIRE(AdsExecuteSQLDirect(hStmt, ci, &hCur) == 0);
    // SQL DDL CREATE INDEX writes a structural .cdx bag named
    // after the table stem; subsequent ADS_CDX opens auto-attach.
    CHECK(fs::exists(dir / "data.cdx"));

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("DDL ALTER TABLE ADD COLUMN through AdsExecuteSQLDirect") {
    auto dir = fs::temp_directory_path() / "openads_ddl_alter";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 ct[160] = "CREATE TABLE data (TAG Character(4))";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, ct, &hCur) == 0);

    UNSIGNED8 alter[200] =
        "ALTER TABLE data ADD COLUMN AGE Numeric(3,0)";
    REQUIRE(AdsExecuteSQLDirect(hStmt, alter, &hCur) == 0);
    CHECK(hCur == 0);

    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    UNSIGNED16 nfields = 0;
    REQUIRE(AdsGetNumFields(hTable, &nfields) == 0);
    CHECK(nfields == 2);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("DDL DROP TABLE through AdsExecuteSQLDirect") {
    auto dir = fs::temp_directory_path() / "openads_ddl_drop";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 ct[160] = "CREATE TABLE doomed (ID Numeric(4,0))";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, ct, &hCur) == 0);
    CHECK(fs::exists(dir / "doomed.dbf"));

    UNSIGNED8 drop_sql[64] = "DROP TABLE doomed";
    REQUIRE(AdsExecuteSQLDirect(hStmt, drop_sql, &hCur) == 0);
    CHECK_FALSE(fs::exists(dir / "doomed.dbf"));

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

#if defined(OPENADS_WITH_SQLITE)

TEST_CASE("DDL ALTER/DROP passthrough on sqlite backend") {
    const auto dir = fs::temp_directory_path() / "openads_ddl_sqlite";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto db_path = dir / "ddl.db";

    const std::string uri = "sqlite://" + db_path.string();
    std::vector<UNSIGNED8> srv(uri.size() + 1);
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    auto exec = [&](const char* sql) {
        std::vector<UNSIGNED8> b(std::strlen(sql) + 1);
        std::memcpy(b.data(), sql, std::strlen(sql) + 1);
        ADSHANDLE hCur = 1;
        REQUIRE(AdsExecuteSQLDirect(hStmt, b.data(), &hCur) == 0);
        CHECK(hCur == 0);
    };

    exec("CREATE TABLE t(id INTEGER PRIMARY KEY, nome TEXT)");
    exec("ALTER TABLE t ADD COLUMN saldo REAL");
    exec("DROP TABLE t");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

#endif
