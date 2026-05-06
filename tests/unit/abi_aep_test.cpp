#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string read_result(ADSHANDLE hCur) {
    REQUIRE(AdsGotoTop(hCur) == 0);
    UNSIGNED8  buf[256] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hCur, (UNSIGNED8*)"RESULT", buf, &cap, 0) == 0);
    std::string s((char*)buf, cap);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

}  // namespace

TEST_CASE("M11.4 CREATE PROCEDURE + EXECUTE PROCEDURE — sum_proc") {
    auto dir = fs::temp_directory_path() / "openads_m11_4_sum";
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

    std::string create_sql =
        std::string("CREATE PROCEDURE my_sum AS '") +
        OPENADS_TEST_AEP_DLL + "::sum_proc'";
    // Backslashes in the path inside the SQL string literal would be
    // double-escaped by the compiler already; pass through as-is.
    std::vector<UNSIGNED8> sqlbuf(create_sql.size() + 1);
    std::memcpy(sqlbuf.data(), create_sql.c_str(), create_sql.size() + 1);
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sqlbuf.data(), &hCur) == 0);
    CHECK(hCur == 0);

    UNSIGNED8 exec_sql[200] =
        "EXECUTE PROCEDURE my_sum(5, 7)";
    REQUIRE(AdsExecuteSQLDirect(hStmt, exec_sql, &hCur) == 0);
    CHECK(read_result(hCur) == "12");

    UNSIGNED8 exec_sql2[200] =
        "EXECUTE PROCEDURE my_sum(40, 2)";
    ADSHANDLE hCur2 = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, exec_sql2, &hCur2) == 0);
    CHECK(read_result(hCur2) == "42");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M11.4 EXECUTE PROCEDURE — echo_proc with string args") {
    auto dir = fs::temp_directory_path() / "openads_m11_4_echo";
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

    std::string create_sql =
        std::string("CREATE PROCEDURE echo AS '") +
        OPENADS_TEST_AEP_DLL + "::echo_proc'";
    std::vector<UNSIGNED8> sqlbuf(create_sql.size() + 1);
    std::memcpy(sqlbuf.data(), create_sql.c_str(), create_sql.size() + 1);
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sqlbuf.data(), &hCur) == 0);

    UNSIGNED8 exec_sql[200] =
        "EXECUTE PROCEDURE echo('hello', 'world')";
    REQUIRE(AdsExecuteSQLDirect(hStmt, exec_sql, &hCur) == 0);
    auto got = read_result(hCur);
    CHECK(got == std::string("hello\x1f""world"));

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M11.4 EXECUTE PROCEDURE — error_proc returns non-zero") {
    auto dir = fs::temp_directory_path() / "openads_m11_4_err";
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

    std::string create_sql =
        std::string("CREATE PROCEDURE bad AS '") +
        OPENADS_TEST_AEP_DLL + "::error_proc'";
    std::vector<UNSIGNED8> sqlbuf(create_sql.size() + 1);
    std::memcpy(sqlbuf.data(), create_sql.c_str(), create_sql.size() + 1);
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sqlbuf.data(), &hCur) == 0);

    UNSIGNED8 exec_sql[200] = "EXECUTE PROCEDURE bad()";
    CHECK(AdsExecuteSQLDirect(hStmt, exec_sql, &hCur) != 0);

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
