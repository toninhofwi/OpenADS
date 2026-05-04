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
    hdr[4]  = 4;
    hdr[8]  = 32 + 32 + 1;
    hdr[10] = 1 + 4;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 4;
    push(fd.data(), fd.size());
    file.push_back(0x0D);
    auto rec = [&](const char* s) {
        file.push_back(' ');
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(s)
                           ? static_cast<std::uint8_t>(s[i]) : ' ');
    };
    rec("CCCC"); rec("AAAA"); rec("DDDD"); rec("BBBB");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

std::vector<UNSIGNED32> walk(ADSHANDLE hCur) {
    std::vector<UNSIGNED32> out;
    if (AdsGotoTop(hCur) != 0) return out;
    while (true) {
        UNSIGNED16 atend = 0;
        if (AdsAtEOF(hCur, &atend) != 0 || atend) break;
        UNSIGNED32 r = 0;
        if (AdsGetRecordNum(hCur, 0, &r) != 0) break;
        out.push_back(r);
        if (AdsSkip(hCur, 1) != 0) break;
    }
    return out;
}

}  // namespace

TEST_CASE("M10.6 SQL ORDER BY ascending walks rows in sorted order") {
    auto dir = fs::temp_directory_path() / "openads_m10_6_asc";
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

    UNSIGNED8 sql[160] = "SELECT * FROM data.dbf ORDER BY TAG";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    auto seq = walk(hCur);
    // AAAA(2), BBBB(4), CCCC(1), DDDD(3).
    REQUIRE(seq.size() == 4);
    CHECK(seq[0] == 2);
    CHECK(seq[1] == 4);
    CHECK(seq[2] == 1);
    CHECK(seq[3] == 3);

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.6 SQL ORDER BY DESC reverses the order") {
    auto dir = fs::temp_directory_path() / "openads_m10_6_desc";
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

    UNSIGNED8 sql[160] = "SELECT * FROM data.dbf ORDER BY TAG DESC";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    auto seq = walk(hCur);
    REQUIRE(seq.size() == 4);
    CHECK(seq[0] == 3);   // DDDD
    CHECK(seq[1] == 1);   // CCCC
    CHECK(seq[2] == 4);   // BBBB
    CHECK(seq[3] == 2);   // AAAA

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.6 SQL ORDER BY combines with WHERE") {
    auto dir = fs::temp_directory_path() / "openads_m10_6_where";
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

    // TAG > 'AAAA' filters {1,3,4}. ORDER BY TAG → BBBB(4), CCCC(1), DDDD(3).
    UNSIGNED8 sql[200] =
        "SELECT * FROM data.dbf WHERE TAG > 'AAAA' ORDER BY TAG";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    auto seq = walk(hCur);
    REQUIRE(seq.size() == 3);
    CHECK(seq[0] == 4);
    CHECK(seq[1] == 1);
    CHECK(seq[2] == 3);

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
