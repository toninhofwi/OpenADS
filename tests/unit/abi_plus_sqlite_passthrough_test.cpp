#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(OPENADS_WITH_SQLITE)

namespace fs = std::filesystem;

namespace {

// Run one statement through AdsExecuteSQLDirect; returns the cursor handle
// (0 for DDL/DML, non-zero for a result-producing statement).
ADSHANDLE exec_direct(ADSHANDLE hConn, const char* sql) {
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    std::vector<UNSIGNED8> buf(std::strlen(sql) + 1);
    std::memcpy(buf.data(), sql, std::strlen(sql) + 1);
    ADSHANDLE hCursor = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, buf.data(), &hCursor) == 0);
    AdsCloseSQLStatement(hStmt);
    return hCursor;
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

TEST_CASE("ABI Plus: sqlite AdsExecuteSQLDirect passthrough (DDL + DML + SELECT)") {
    const auto dir = fs::temp_directory_path() / "openads_plus_sqlpass";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto db_path = dir / "pass.db";

    const std::string uri = "sqlite://" + db_path.string();
    std::vector<UNSIGNED8> srv(uri.size() + 1);
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    // DDL + DML straight through the ACE ABI — no cursor returned.
    CHECK(exec_direct(hConn, "CREATE TABLE t(a INTEGER, b TEXT)") == 0);
    CHECK(exec_direct(hConn,
        "INSERT INTO t(a,b) VALUES (1,'x'),(2,'y'),(3,'z')") == 0);

    // SELECT through the ABI returns a navigable, materialized result cursor.
    ADSHANDLE hCur = exec_direct(hConn,
        "SELECT a, b FROM t WHERE a >= 2 ORDER BY a");
    REQUIRE(hCur != 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 2);

    REQUIRE(AdsGotoTop(hCur) == 0);
    CHECK(field_str(hCur, "a").substr(0, 1) == "2");
    CHECK(field_str(hCur, "b").substr(0, 1) == "y");

    REQUIRE(AdsSkip(hCur, 1) == 0);
    CHECK(field_str(hCur, "a").substr(0, 1) == "3");
    CHECK(field_str(hCur, "b").substr(0, 1) == "z");

    REQUIRE(AdsSkip(hCur, 1) == 0);
    UNSIGNED16 eof = 0;
    REQUIRE(AdsAtEOF(hCur, &eof) == 0);
    CHECK(eof == 1);

    REQUIRE(AdsCloseTable(hCur) == 0);

    // The CREATE/INSERT really persisted: reopen the base table and count.
    UNSIGNED8 tn[32] = "t";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, tn, tn, ADS_DEFAULT, 0, 0, 0,
                         ADS_READONLY, &hT) == 0);
    UNSIGNED32 total = 0;
    REQUIRE(AdsGetRecordCount(hT, 0, &total) == 0);
    CHECK(total == 3);
    REQUIRE(AdsCloseTable(hT) == 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

#endif // OPENADS_WITH_SQLITE
