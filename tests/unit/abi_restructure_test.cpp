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
    hdr[4]  = 2;
    hdr[8]  = 32 + 32 + 1;
    hdr[10] = 1 + 5;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 5;
    push(fd.data(), fd.size());
    file.push_back(0x0D);
    auto rec = [&](const char* s) {
        file.push_back(' ');
        for (int i = 0; i < 5; ++i)
            file.push_back(i < (int)std::strlen(s)
                           ? static_cast<std::uint8_t>(s[i]) : ' ');
    };
    rec("ALPHA");
    rec("BETA ");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

}  // namespace

TEST_CASE("M9.26 AdsRestructureTable adds a new field, preserves old data") {
    auto dir = fs::temp_directory_path() / "openads_m9_26_add";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 leaf[16] = "data";
    UNSIGNED8 add[64]  = "AGE,Numeric,3,0;CITY,Character,8";
    UNSIGNED8 empty[1] = {0};

    REQUIRE(AdsRestructureTable(hConn, leaf, nullptr,
                                ADS_CDX, 0, 0, 0,
                                add, empty, empty) == 0);

    // Reopen and verify schema + data.
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);

    UNSIGNED16 nf = 0;
    REQUIRE(AdsGetNumFields(hTable, &nf) == 0);
    CHECK(nf == 3);

    UNSIGNED8 fname[16] = {0};
    UNSIGNED16 fnlen    = sizeof(fname);
    REQUIRE(AdsGetFieldName(hTable, 1, fname, &fnlen) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(fname), fnlen) == "TAG");
    fnlen = sizeof(fname); std::memset(fname, 0, sizeof(fname));
    REQUIRE(AdsGetFieldName(hTable, 2, fname, &fnlen) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(fname), fnlen) == "AGE");
    fnlen = sizeof(fname); std::memset(fname, 0, sizeof(fname));
    REQUIRE(AdsGetFieldName(hTable, 3, fname, &fnlen) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(fname), fnlen) == "CITY");

    // Walk records — TAG values should match original.
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &cnt) == 0);
    CHECK(cnt == 2);

    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED8 fld[16] = "TAG";
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "ALPHA");

    REQUIRE(AdsSkip(hTable, 1) == 0);
    cap = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    // Field content is "BETA" with trailing space-pad up to width 5.
    auto val = std::string(reinterpret_cast<const char*>(buf), cap);
    while (!val.empty() && val.back() == ' ') val.pop_back();
    CHECK(val == "BETA");

    // The added CITY field reads as blanks.
    UNSIGNED8 city[16] = "CITY";
    cap = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGotoTop(hTable) == 0);
    REQUIRE(AdsGetField(hTable, city, buf, &cap, 0) == 0);
    for (UNSIGNED32 i = 0; i < cap; ++i) CHECK(buf[i] == ' ');

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.26 AdsRestructureTable rejects DELETE / CHANGE field lists") {
    auto dir = fs::temp_directory_path() / "openads_m9_26_reject";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 leaf[16]  = "data";
    UNSIGNED8 empty[1]  = {0};
    UNSIGNED8 del[16]   = "TAG";
    UNSIGNED8 chg[32]   = "TAG,Character,10";

    UNSIGNED32 rc1 = AdsRestructureTable(hConn, leaf, nullptr,
                                         ADS_CDX, 0, 0, 0,
                                         empty, del, empty);
    CHECK(rc1 == openads::AE_FUNCTION_NOT_AVAILABLE);

    UNSIGNED32 rc2 = AdsRestructureTable(hConn, leaf, nullptr,
                                         ADS_CDX, 0, 0, 0,
                                         empty, empty, chg);
    CHECK(rc2 == openads::AE_FUNCTION_NOT_AVAILABLE);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.26 AdsRestructureTable with empty add list is a no-op") {
    auto dir = fs::temp_directory_path() / "openads_m9_26_noop";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 leaf[16] = "data";
    UNSIGNED8 empty[1] = {0};

    REQUIRE(AdsRestructureTable(hConn, leaf, nullptr,
                                ADS_CDX, 0, 0, 0,
                                empty, empty, empty) == 0);

    // Schema must remain 1 field.
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    UNSIGNED16 nf = 0;
    REQUIRE(AdsGetNumFields(hTable, &nf) == 0);
    CHECK(nf == 1);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
