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

void write_dbf(const fs::path& path,
               const std::vector<std::pair<std::string, std::uint8_t>>& cols,
               const std::vector<std::vector<std::string>>& rows) {
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[4] = static_cast<std::uint8_t>(rows.size());
    std::uint16_t hl = static_cast<std::uint16_t>(32 + 32 * cols.size() + 1);
    std::uint16_t rl = 1;
    for (auto& c : cols) rl += c.second;
    hdr[8]  = static_cast<std::uint8_t>( hl       & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((hl >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>( rl       & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rl >> 8) & 0xFFu);
    push(hdr.data(), hdr.size());
    for (auto& c : cols) {
        std::array<std::uint8_t, 32> fd{};
        std::strncpy(reinterpret_cast<char*>(fd.data()),
                     c.first.c_str(), 11);
        fd[11] = 'C'; fd[16] = c.second;
        push(fd.data(), fd.size());
    }
    file.push_back(0x0D);
    for (auto& row : rows) {
        file.push_back(' ');
        for (std::size_t i = 0; i < cols.size(); ++i) {
            const auto& v = row[i];
            std::uint8_t L = cols[i].second;
            for (std::uint8_t k = 0; k < L; ++k) {
                file.push_back(k < v.size()
                    ? static_cast<std::uint8_t>(v[k]) : ' ');
            }
        }
    }
    file.push_back(0x1A);
    std::ofstream(path, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
}

}  // namespace

TEST_CASE("M10.41 INSERT INTO t (col) SELECT col FROM src") {
    auto dir = fs::temp_directory_path() / "openads_m10_41";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf(dir / "src.dbf", {{"TAG", 4}}, {{"AAAA"}, {"BBBB"}, {"CCCC"}});
    write_dbf(dir / "dst.dbf", {{"TAG", 4}}, {});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[300] =
        "INSERT INTO dst.dbf (TAG) SELECT TAG FROM src.dbf";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    CHECK(hCur == 0);

    UNSIGNED8 sel[200] = "SELECT * FROM dst.dbf";
    REQUIRE(AdsExecuteSQLDirect(hStmt, sel, &hCur) == 0);
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 3);

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.42 CREATE TABLE t AS SELECT — copy schema + rows") {
    auto dir = fs::temp_directory_path() / "openads_m10_42";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf(dir / "src.dbf",
        {{"ID", 4}, {"NAME", 8}},
        {{"R001", "Alice"},
         {"R002", "Bob"},
         {"R003", "Carol"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[300] =
        "CREATE TABLE copy.dbf AS SELECT * FROM src.dbf";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    CHECK(hCur == 0);

    UNSIGNED8 sel[200] = "SELECT * FROM copy.dbf";
    REQUIRE(AdsExecuteSQLDirect(hStmt, sel, &hCur) == 0);
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 3);
    UNSIGNED16 nf = 0;
    REQUIRE(AdsGetNumFields(hCur, &nf) == 0);
    CHECK(nf == 2);

    REQUIRE(AdsGotoTop(hCur) == 0);
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hCur, (UNSIGNED8*)"NAME", buf, &cap, 0) == 0);
    std::string s((char*)buf, cap);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    CHECK(s == "Alice");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
