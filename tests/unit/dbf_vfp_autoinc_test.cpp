#include "doctest.h"
#include "openads/ace.h"
#include "drivers/cdx/cdx_driver.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Stage a VFP-flagged DBF (header byte 0x32) with a 4-byte autoinc
// field whose initial counter is `start` and step is 1.
fs::path stage_autoinc_dbf(const fs::path& dir, std::uint32_t start) {
    fs::create_directories(dir);
    auto p = dir / "data.dbf";
    fs::remove(p);
    std::vector<std::uint8_t> file;

    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x32;                // VFP with autoinc
    hdr[4]  = 0;
    hdr[8]  = 32 + 32 + 1;
    hdr[10] = 1 + 4;
    file.insert(file.end(), hdr.begin(), hdr.end());

    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "ID", 11);
    fd[11] = 'I';                  // VFP integer
    fd[16] = 4;                    // length 4
    fd[17] = 0;                    // decimals 0
    fd[18] = 0x0Cu;                // autoinc flag bits
    fd[19] = static_cast<std::uint8_t>( start        & 0xFFu);
    fd[20] = static_cast<std::uint8_t>((start >>  8) & 0xFFu);
    fd[21] = static_cast<std::uint8_t>((start >> 16) & 0xFFu);
    fd[22] = static_cast<std::uint8_t>((start >> 24) & 0xFFu);
    fd[23] = 1;                    // step
    file.insert(file.end(), fd.begin(), fd.end());

    file.push_back(0x0D);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

}  // namespace

TEST_CASE("M10.11 VFP autoinc fills appended record + persists counter") {
    const auto dir = fs::temp_directory_path() / "openads_m10_11_autoinc";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_autoinc_dbf(dir, /*start=*/100);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);

    REQUIRE(AdsAppendRecord(hTable) == 0);
    REQUIRE(AdsAppendRecord(hTable) == 0);
    REQUIRE(AdsAppendRecord(hTable) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    // Reopen + verify the three records picked up 100, 101, 102 and
    // that the on-disk counter is now 103.
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);

    UNSIGNED32 expected[] = {100, 101, 102};
    for (UNSIGNED32 i = 1; i <= 3; ++i) {
        REQUIRE(AdsGotoRecord(hTable, i) == 0);
        // The field is a 4-byte little-endian int; AdsGetField on a
        // VFP I column returns the formatted decimal representation
        // (M10.2). Verify the readback string matches.
        UNSIGNED8 fld[16] = "ID";
        UNSIGNED8 buf[16] = {0};
        UNSIGNED32 cap = sizeof(buf);
        REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
        auto s = std::string(reinterpret_cast<const char*>(buf), cap);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        CHECK(s == std::to_string(expected[i - 1]));
    }

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    // Read raw bytes 19-22 of the field descriptor to confirm the
    // counter advanced past 102 (i.e. 103 = next-to-issue).
    std::ifstream raw((dir / "data.dbf"), std::ios::binary);
    REQUIRE(raw.good());
    raw.seekg(32 + 19);
    std::uint8_t bytes[4] = {0};
    raw.read(reinterpret_cast<char*>(bytes), 4);
    std::uint32_t counter =
         static_cast<std::uint32_t>(bytes[0])        |
        (static_cast<std::uint32_t>(bytes[1]) <<  8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) |
        (static_cast<std::uint32_t>(bytes[3]) << 24);
    CHECK(counter == 103);

    fs::remove_all(dir, ec);
}
