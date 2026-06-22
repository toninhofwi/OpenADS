#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

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
    exec("CREATE TABLE clientes ("
         "id INTEGER PRIMARY KEY, nome TEXT, saldo REAL)");
    exec("INSERT INTO clientes (id, nome, saldo) VALUES "
         "(1, 'Ana', 10.5), (2, 'Bob', NULL), (3, 'Cid', 0.0)");
    sqlite3_close(db);
}

std::string field_str(ADSHANDLE hTable, const char* name) {
    UNSIGNED8 fld[32];
    std::memcpy(fld, name, std::strlen(name) + 1);
    UNSIGNED8 buf[128] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    return std::string(reinterpret_cast<const char*>(buf), cap);
}

} // namespace

TEST_CASE("ABI Plus: sqlite AdsSeek on column index") {
    const auto dir = fs::temp_directory_path() / "openads_plus_sqlite_seek";
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
                         ADS_DEFAULT, 0, 0, 0, ADS_READONLY,
                         &hTable) == 0);

    UNSIGNED8 idx_tag[16] = "id";
    ADSHANDLE hIndex = 0;
    UNSIGNED16 nidx = 1;
    REQUIRE(AdsOpenIndex(hTable, idx_tag, &hIndex, &nidx) == 0);
    CHECK(nidx == 1);

    const char key[] = "2";
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hIndex,
                    reinterpret_cast<UNSIGNED8*>(const_cast<char*>(key)),
                    static_cast<UNSIGNED16>(std::strlen(key)),
                    ADS_STRINGKEY, 0, &found) == 0);
    CHECK(found == 1);

    UNSIGNED16 is_found = 0;
    REQUIRE(AdsIsFound(hTable, &is_found) == 0);
    CHECK(is_found == 1);

    const std::string nome = field_str(hTable, "nome");
    CHECK(nome.find("Bob") != std::string::npos);

    const char miss[] = "9";
    REQUIRE(AdsSeek(hIndex,
                    reinterpret_cast<UNSIGNED8*>(const_cast<char*>(miss)),
                    static_cast<UNSIGNED16>(std::strlen(miss)),
                    ADS_STRINGKEY, 0, &found) == 0);
    CHECK(found == 0);

    REQUIRE(AdsCloseIndex(hIndex) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    fs::remove_all(dir, ec);
}

#endif