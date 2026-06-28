#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

ADSHANDLE open_conn(const fs::path& dir) {
    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);
    return hConn;
}

std::uint8_t read_header_byte(const fs::path& dbf, std::size_t off) {
    std::ifstream f(dbf, std::ios::binary);
    f.seekg(static_cast<std::streamoff>(off));
    char b = 0;
    f.read(&b, 1);
    return static_cast<std::uint8_t>(b);
}

}  // namespace

TEST_CASE("AdsCreateTable ADS_VFP writes 0x30 header and opens for read") {
    const auto dir = fs::temp_directory_path() / "openads_vfp_create_30";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    ADSHANDLE hConn = open_conn(dir);

    UNSIGNED8 def[]   = "ID,Integer,4,0;TAG,Character,5";
    UNSIGNED8 tname[] = "vfp30";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_VFP, 0, 0, 0, 0, def, &hT)
            == 0);

    CHECK(read_header_byte(dir / "vfp30.dbf", 0) == 0x30);

    UNSIGNED8 fTAG[8] = "TAG";
    REQUIRE(AdsAppendRecord(hT) == 0);
    REQUIRE(AdsSetString(hT, fTAG, (UNSIGNED8*)"HELLO", 5) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);

    REQUIRE(AdsGotoTop(hT) == 0);
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hT, fTAG, buf, &cap, 0) == 0);
    std::string s(reinterpret_cast<const char*>(buf), cap);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    CHECK(s == "HELLO");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsCreateTable ADS_VFP 0x32 with autoinc and nullable") {
    const auto dir = fs::temp_directory_path() / "openads_vfp_create_32";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    ADSHANDLE hConn = open_conn(dir);

    UNSIGNED8 def[] =
        "ID,AutoInc,4,0;NAME,Character,8,0,NULL";
    UNSIGNED8 tname[] = "vfp32";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_VFP, 0, 0, 0, 0, def, &hT)
            == 0);

    CHECK(read_header_byte(dir / "vfp32.dbf", 0) == 0x32);

    REQUIRE(AdsAppendRecord(hT) == 0);
    UNSIGNED8 fNAME[8] = "NAME";
    REQUIRE(AdsSetString(hT, fNAME, (UNSIGNED8*)"WORLD", 5) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);

    UNSIGNED8 fID[8] = "ID";
    REQUIRE(AdsGotoTop(hT) == 0);
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hT, fID, buf, &cap, 0) == 0);
    std::string id_s(reinterpret_cast<const char*>(buf), cap);
    while (!id_s.empty() && id_s.back() == ' ') id_s.pop_back();
    CHECK(id_s == "1");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}