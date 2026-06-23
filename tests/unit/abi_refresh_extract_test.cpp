#include "doctest.h"
#include "openads/ace.h"

#include "drivers/cdx/cdx_index.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using openads::drivers::cdx::CdxIndex;

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
    hdr[4]  = 3;                       // 3 records
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
    rec("AAAA"); rec("BBBB"); rec("CCCC");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

fs::path stage_cdx(const fs::path& dir) {
    auto p = dir / "data.cdx";
    fs::remove(p);
    auto created = CdxIndex::create(p.string(), "TAG", "TAG", 4, false, false);
    REQUIRE(created.has_value());
    CdxIndex ix = std::move(created).value();
    REQUIRE(ix.insert(1, "AAAA").has_value());
    REQUIRE(ix.insert(2, "BBBB").has_value());
    REQUIRE(ix.insert(3, "CCCC").has_value());
    REQUIRE(ix.flush().has_value());
    return p;
}

} // namespace

TEST_CASE("M9.6 AdsRefreshRecord re-reads from disk after external edit") {
    auto dir = fs::temp_directory_path() / "openads_m96_refresh";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto dbf = stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 name[16] = "data";
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTable) == 0);

    REQUIRE(AdsGotoRecord(hTable, 2) == 0);
    UNSIGNED8 fld[16] = "TAG";
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "BBBB");

    // External edit: rewrite recno 2's TAG bytes directly via fstream.
    {
        std::fstream f(dbf, std::ios::in | std::ios::out | std::ios::binary);
        // Header_len=65, record_len=5; rec 2 starts at 65 + (2-1)*5 = 70.
        // Skip the 1-byte delete flag → 71.
        f.seekp(71);
        f.write("ZZZZ", 4);
    }

    REQUIRE(AdsRefreshRecord(hTable) == 0);
    cap = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "ZZZZ");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.6 AdsExtractKey returns the active index key for the current rec") {
    auto dir = fs::temp_directory_path() / "openads_m96_extract";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);
    stage_cdx(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 name[16] = "data";
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 idxname[16] = "data";
    ADSHANDLE hIdx = 0;
    UNSIGNED16 cnt = 1;
    REQUIRE(AdsOpenIndex(hTable, idxname, &hIdx, &cnt) == 0);

    REQUIRE(AdsGotoRecord(hTable, 2) == 0);

    UNSIGNED8 keybuf[32] = {0};
    UNSIGNED16 keylen = sizeof(keybuf);
    REQUIRE(AdsExtractKey(hIdx, keybuf, &keylen) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(keybuf), keylen) == "BBBB");

    REQUIRE(AdsGotoRecord(hTable, 3) == 0);
    keylen = sizeof(keybuf); std::memset(keybuf, 0, sizeof(keybuf));
    REQUIRE(AdsExtractKey(hIdx, keybuf, &keylen) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(keybuf), keylen) == "CCCC");

    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
