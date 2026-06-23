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
    hdr[4]  = 3;
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
    rec("BBBB");
    rec("AAAA");
    rec("CCCC");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

}  // namespace

TEST_CASE("M10.4 AdsSetIndexDirection reverses traversal") {
    auto dir = fs::temp_directory_path() / "openads_m10_4_dir";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_NTX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 fn[32]   = "tag.ntx";
    UNSIGNED8 tagn[16] = "T";
    UNSIGNED8 expr[16] = "TAG";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, fn, tagn, expr,
                             nullptr, nullptr, 0, 1024, &hIdx) == 0);

    // Default ascending: GotoTop = AAAA (rec 2).
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED32 r = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 2);

    // Flip direction: GotoTop now = CCCC (rec 3, last in ascending).
    REQUIRE(AdsSetIndexDirection(hIdx, /*usDir=*/1) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 3);

    // Skip(+1) when descending walks prev() — moves to BBBB (rec 1).
    REQUIRE(AdsSkip(hTable, 1) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 1);

    // Skip(+1) again → AAAA (rec 2, smallest).
    REQUIRE(AdsSkip(hTable, 1) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 2);

    // Restore ascending — GotoTop = AAAA again.
    REQUIRE(AdsSetIndexDirection(hIdx, 0) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 2);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsCreateIndex61 ADS_COMPOUND option builds an ascending order") {
    // rddads / X#'s ADSRDD pass ADS_COMPOUND (0x02) for every CDX/NTX
    // tag. ADS_COMPOUND must NOT be decoded as ADS_DESCENDING (0x08) —
    // doing so builds every order descending: GotoTop lands on the
    // last key and SKIP walks backward.
    auto dir = fs::temp_directory_path() / "openads_idx_compound_dir";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_NTX, 1, 1, 0, 1,
                         &hTable) == 0);

    UNSIGNED8 fn[32]   = "tagc.ntx";
    UNSIGNED8 tagn[16] = "T";
    UNSIGNED8 expr[16] = "TAG";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, fn, tagn, expr, nullptr, nullptr,
                             ADS_COMPOUND, 1024, &hIdx) == 0);

    // ADS_COMPOUND alone => ascending: GotoTop = AAAA (rec 2).
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED32 r = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 2);

    // Skip(+1) walks forward (ascending) => BBBB (rec 1).
    REQUIRE(AdsSkip(hTable, 1) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 1);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsCreateIndex61 ADS_DESCENDING option builds a descending order") {
    auto dir = fs::temp_directory_path() / "openads_idx_desc_dir";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_NTX, 1, 1, 0, 1,
                         &hTable) == 0);

    UNSIGNED8 fn[32]   = "tagd.ntx";
    UNSIGNED8 tagn[16] = "T";
    UNSIGNED8 expr[16] = "TAG";
    ADSHANDLE hIdx = 0;
    // ADS_DESCENDING | ADS_COMPOUND — the real descending request.
    REQUIRE(AdsCreateIndex61(hTable, fn, tagn, expr, nullptr, nullptr,
                             ADS_DESCENDING | ADS_COMPOUND, 1024,
                             &hIdx) == 0);

    // Descending: GotoTop = CCCC (rec 3, largest key).
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED32 r = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 3);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
