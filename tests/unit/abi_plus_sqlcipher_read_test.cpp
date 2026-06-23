#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// Only meaningful when the SQL backend is built with SQLCipher-compatible
// encryption (SQLite3 Multiple Ciphers). With plain SQLite the cipher PRAGMAs
// are unavailable, so this case compiles out entirely — no phantom green.
#if defined(OPENADS_WITH_SQLCIPHER)

#include <sqlite3.h>

namespace fs = std::filesystem;

namespace {

void exec_or_fail(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        const std::string msg = err ? err : "sqlite exec failed";
        if (err) sqlite3_free(err);
        FAIL(msg);
    }
}

// Build an encrypted database in SQLCipher v4 format and seed two rows.
void seed_encrypted_db(const fs::path& db_path, const char* key) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(db_path.string().c_str(), &db) == SQLITE_OK);
    exec_or_fail(db, "PRAGMA cipher='sqlcipher'");
    exec_or_fail(db, "PRAGMA legacy=4");
    exec_or_fail(db, std::string("PRAGMA key='") + key + "'");
    exec_or_fail(db,
        "CREATE TABLE secret(id INTEGER PRIMARY KEY, nome TEXT, valor REAL)");
    exec_or_fail(db,
        "INSERT INTO secret VALUES (1,'Alpha',9.5),(2,'Beta',NULL)");
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

TEST_CASE("ABI Plus: sqlcipher encrypted round-trip through the ACE ABI") {
    const auto dir = fs::temp_directory_path() / "openads_plus_sqlcipher";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto db_path = dir / "enc.db";
    const char* key = "s3cr3t-pass";

    seed_encrypted_db(db_path, key);

    // The file really is encrypted: a plain open without the key cannot read it.
    {
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(db_path.string().c_str(), &raw) == SQLITE_OK);
        const int rc =
            sqlite3_exec(raw, "SELECT count(*) FROM secret", nullptr, nullptr, nullptr);
        CHECK(rc != SQLITE_OK);
        sqlite3_close(raw);
    }

    // Open it through the public ACE ABI with the key on the sqlite:// URI.
    const std::string uri = "sqlite://" + db_path.string() + "?key=" + key;
    std::vector<UNSIGNED8> srv(uri.size() + 1);
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 tbl_name[32] = "secret";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tbl_name, tbl_name,
                         ADS_DEFAULT, 0, 0, 0, ADS_READONLY,
                         &hTable) == 0);

    UNSIGNED32 count = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &count) == 0);
    CHECK(count == 2);

    REQUIRE(AdsGotoTop(hTable) == 0);
    CHECK(field_str(hTable, "nome").substr(0, 5) == "Alpha");

    REQUIRE(AdsSkip(hTable, 1) == 0);
    CHECK(field_str(hTable, "nome").substr(0, 4) == "Beta");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    fs::remove_all(dir, ec);
}

#endif // OPENADS_WITH_SQLCIPHER
