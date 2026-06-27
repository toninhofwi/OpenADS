#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(OPENADS_WITH_SQLITE)
#include <sqlite3.h>
#endif

namespace fs = std::filesystem;

#if defined(OPENADS_WITH_SQLITE)

namespace {

void seed_db(const fs::path& db_path) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(db_path.string().c_str(), &db) == SQLITE_OK);
    auto exec = [&](const char* sql) {
        char* err = nullptr;
        const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            const std::string msg = err ? err : "sqlite exec failed";
            if (err) sqlite3_free(err);
            FAIL(msg);
        }
    };
    exec("DROP TABLE IF EXISTS clientes");
    exec("CREATE TABLE clientes ("
         "id INTEGER PRIMARY KEY, nome TEXT, saldo REAL)");
    exec("INSERT INTO clientes (id, nome, saldo) VALUES "
         "(1, 'Ana', 10.5), (2, 'Bob', NULL), (3, 'Cid', 0.0)");
    sqlite3_close(db);
}

std::string rtrim(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::string field_str(ADSHANDLE hTable, const char* name) {
    UNSIGNED8 fld[32];
    std::memcpy(fld, name, std::strlen(name) + 1);
    UNSIGNED8 buf[256] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    return std::string(reinterpret_cast<const char*>(buf), cap);
}

void set_str(ADSHANDLE hTable, const char* field, const char* value) {
    UNSIGNED8 f[64];
    std::memcpy(f, field, std::strlen(field) + 1);
    UNSIGNED8 v[256];
    std::memcpy(v, value, std::strlen(value) + 1);
    REQUIRE(AdsSetString(hTable, f, v,
                         static_cast<UNSIGNED32>(std::strlen(value))) == 0);
}

UNSIGNED32 row_count(ADSHANDLE hTable) {
    UNSIGNED32 count = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &count) == 0);
    return count;
}

} // namespace

TEST_CASE("ABI: sqlite AdsAppendRecord + AdsSetString + AdsWriteRecord + AdsDeleteRecord") {
    const auto dir = fs::temp_directory_path() / "openads_plus_sqlite_write";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto db_path = dir / "app.db";
    seed_db(db_path);

    const std::string uri = "sqlite://" + db_path.string();
    std::vector<UNSIGNED8> srv(uri.size() + 1);
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 tbl_name[32] = "clientes";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tbl_name, tbl_name,
                         ADS_DEFAULT, 0, 0, 0, ADS_DEFAULT, &hTable) == 0);

    CHECK(row_count(hTable) == 3);

    REQUIRE(AdsAppendRecord(hTable) == 0);
    set_str(hTable, "id", "99");
    set_str(hTable, "nome", "Dan");
    set_str(hTable, "saldo", "42.5");
    REQUIRE(AdsWriteRecord(hTable) == 0);
    CHECK(row_count(hTable) == 4);

    REQUIRE(AdsGotoBottom(hTable) == 0);
    CHECK(rtrim(field_str(hTable, "nome")) == "Dan");
    CHECK(rtrim(field_str(hTable, "id")) == "99");
    CHECK(rtrim(field_str(hTable, "saldo")) == "42.5");

    set_str(hTable, "nome", "DanX");
    REQUIRE(AdsWriteRecord(hTable) == 0);
    REQUIRE(AdsGotoBottom(hTable) == 0);
    CHECK(rtrim(field_str(hTable, "nome")) == "DanX");

    REQUIRE(AdsDeleteRecord(hTable) == 0);
    CHECK(row_count(hTable) == 3);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

#else

TEST_CASE("ABI: sqlite write test disabled at compile time") {
    UNSIGNED8 uri[] = "sqlite:///tmp/none.db";
    ADSHANDLE hConn = 0;
    const UNSIGNED32 rc = AdsConnect60(uri, ADS_LOCAL_SERVER,
                                       nullptr, nullptr, 0, &hConn);
    CHECK(rc == openads::AE_FUNCTION_NOT_AVAILABLE);
}

#endif