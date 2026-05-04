#include "doctest.h"
#include "openads/ace.h"
#include "sql/parser.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path stage_dbf(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "data.dbf";
    fs::remove(p);
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 3;
    hdr[8]  = 32 + 32 + 32 + 1;
    hdr[10] = 1 + 6 + 4;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> name_fd{};
    std::strncpy(reinterpret_cast<char*>(name_fd.data()), "NAME", 11);
    name_fd[11] = 'C'; name_fd[16] = 6;
    push(name_fd.data(), name_fd.size());
    std::array<std::uint8_t, 32> age_fd{};
    std::strncpy(reinterpret_cast<char*>(age_fd.data()), "AGE", 11);
    age_fd[11] = 'N'; age_fd[16] = 4; age_fd[17] = 0;
    push(age_fd.data(), age_fd.size());
    file.push_back(0x0D);
    auto rec = [&](const char* name, const char* age) {
        file.push_back(' ');
        for (int i = 0; i < 6; ++i)
            file.push_back(i < (int)std::strlen(name)
                           ? static_cast<std::uint8_t>(name[i]) : ' ');
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(age)
                           ? static_cast<std::uint8_t>(age[i]) : ' ');
    };
    rec("Alice", "  30");
    rec("Bob",   "  42");
    rec("Carol", "  18");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

}  // namespace

TEST_CASE("M10.7 SQL UPDATE rewrites matching rows") {
    auto dir = fs::temp_directory_path() / "openads_m10_7_upd";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[200] =
        "UPDATE data.dbf SET AGE = 99 WHERE NAME = 'Bob'";
    ADSHANDLE hCur = 0xDEADBEEF;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    CHECK(hCur == 0);

    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    REQUIRE(AdsGotoRecord(hTable, 2) == 0);
    UNSIGNED8 fld[16] = "AGE";
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    auto val = std::string(reinterpret_cast<const char*>(buf), cap);
    while (!val.empty() && val.front() == ' ') val.erase(val.begin());
    while (!val.empty() && val.back()  == ' ') val.pop_back();
    CHECK(val == "99");

    // Other rows untouched.
    REQUIRE(AdsGotoRecord(hTable, 1) == 0);
    cap = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    auto v1 = std::string(reinterpret_cast<const char*>(buf), cap);
    while (!v1.empty() && v1.front() == ' ') v1.erase(v1.begin());
    while (!v1.empty() && v1.back()  == ' ') v1.pop_back();
    CHECK(v1 == "30");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.7 SQL UPDATE without WHERE rewrites every row") {
    auto dir = fs::temp_directory_path() / "openads_m10_7_upd_all";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[160] = "UPDATE data.dbf SET AGE = 0";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    UNSIGNED8 fld[16] = "AGE";
    for (UNSIGNED32 r = 1; r <= 3; ++r) {
        REQUIRE(AdsGotoRecord(hTable, r) == 0);
        UNSIGNED8 buf[16] = {0};
        UNSIGNED32 cap = sizeof(buf);
        REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
        auto v = std::string(reinterpret_cast<const char*>(buf), cap);
        while (!v.empty() && v.front() == ' ') v.erase(v.begin());
        while (!v.empty() && v.back()  == ' ') v.pop_back();
        CHECK(v == "0");
    }

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.7 SQL DELETE marks matching rows deleted") {
    auto dir = fs::temp_directory_path() / "openads_m10_7_del";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[200] = "DELETE FROM data.dbf WHERE NAME = 'Carol'";
    ADSHANDLE hCur = 0xDEADBEEF;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    CHECK(hCur == 0);

    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    REQUIRE(AdsGotoRecord(hTable, 3) == 0);
    UNSIGNED16 deleted = 0;
    REQUIRE(AdsIsRecordDeleted(hTable, &deleted) == 0);
    CHECK(deleted != 0);

    REQUIRE(AdsGotoRecord(hTable, 1) == 0);
    REQUIRE(AdsIsRecordDeleted(hTable, &deleted) == 0);
    CHECK(deleted == 0);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.7 parsers recognise their leading keyword") {
    CHECK(openads::sql::sql_is_update("UPDATE x SET ..."));
    CHECK(openads::sql::sql_is_delete("DELETE FROM x"));
    CHECK_FALSE(openads::sql::sql_is_update("SELECT * FROM x"));
    CHECK_FALSE(openads::sql::sql_is_delete("INSERT INTO x ..."));
}
