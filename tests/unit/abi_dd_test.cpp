#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path make_dbf(const fs::path& dir, const char* leaf) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    fs::remove(p);
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 1;
    hdr[8]  = 32 + 32 + 1; hdr[9]  = 0;
    hdr[10] = 1 + 5;       hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 5;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    file.push_back(' ');
    file.push_back('H'); file.push_back('E'); file.push_back('L');
    file.push_back('L'); file.push_back('O');
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("AdsDDCreate + AdsDDAddTable + open table by alias") {
    const auto dir = fs::temp_directory_path() / "openads_m6_dd_abi";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    auto add_path = (dir / "openads.add").string();
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.c_str(), add_path.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsDDCreate(add_buf, 0, nullptr, &hConn) == 0);

    UNSIGNED8 alias[64] = "clientes";
    UNSIGNED8 path [64] = "data.dbf";
    REQUIRE(AdsDDAddTable(hConn, alias, path, nullptr, 0,
                          nullptr, nullptr, nullptr) == 0);

    // Open the table by alias — DD resolves "clientes" -> "data.dbf".
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, alias, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED8 fld[16] = "TAG";
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "HELLO");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    // Reopen via .add path: DD persists the alias; AdsOpenTable still
    // resolves "clientes" -> "data.dbf".
    REQUIRE(AdsConnect60(add_buf, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    REQUIRE(AdsOpenTable(hConn, alias, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);
    cap = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "HELLO");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsDDRemoveTable drops alias; reopen by alias fails") {
    const auto dir = fs::temp_directory_path() / "openads_m6_dd_remove";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    auto add_path = (dir / "openads.add").string();
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.c_str(), add_path.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsDDCreate(add_buf, 0, nullptr, &hConn) == 0);
    UNSIGNED8 alias[64] = "x";
    UNSIGNED8 path [64] = "data.dbf";
    REQUIRE(AdsDDAddTable(hConn, alias, path, nullptr, 0,
                          nullptr, nullptr, nullptr) == 0);
    REQUIRE(AdsDDRemoveTable(hConn, alias, 0) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED32 r = AdsOpenTable(hConn, alias, nullptr, ADS_CDX,
                                0, 0, 0, 0, &hTable);
    CHECK(r != 0);  // alias no longer resolves; literal "x" is not a file

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
