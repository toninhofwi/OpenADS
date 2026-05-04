#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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
    auto rec = [&](std::uint8_t del, const char* s) {
        file.push_back(del);
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(s)
                           ? static_cast<std::uint8_t>(s[i]) : ' ');
    };
    rec(' ',  "AAAA");
    rec('*',  "BBBB");   // deleted
    rec(' ',  "CCCC");
    rec('*',  "DDDD");   // deleted
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("M9.8 AdsZapTable empties the DBF and indexes") {
    auto dir = fs::temp_directory_path() / "openads_m98_zap";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 name[16] = "data";
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &cnt) == 0);
    CHECK(cnt == 4u);

    REQUIRE(AdsZapTable(hTable) == 0);

    REQUIRE(AdsGetRecordCount(hTable, 0, &cnt) == 0);
    CHECK(cnt == 0u);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.8 AdsPackTable removes deleted rows and renumbers recnos") {
    auto dir = fs::temp_directory_path() / "openads_m98_pack";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 name[16] = "data";
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &cnt) == 0);
    CHECK(cnt == 4u);     // 4 rows total, 2 deleted

    REQUIRE(AdsPackTable(hTable) == 0);
    REQUIRE(AdsGetRecordCount(hTable, 0, &cnt) == 0);
    CHECK(cnt == 2u);     // only the live rows survive

    // Surviving rows: AAAA at recno 1, CCCC at recno 2.
    REQUIRE(AdsGotoRecord(hTable, 1) == 0);
    UNSIGNED8 fld[16] = "TAG";
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "AAAA");

    REQUIRE(AdsGotoRecord(hTable, 2) == 0);
    cap = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "CCCC");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
