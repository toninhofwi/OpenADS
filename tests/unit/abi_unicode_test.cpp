#include "doctest.h"
#include "openads/ace.h"
#include "abi/charset.h"

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
    hdr[4]  = 0;
    hdr[8]  = 32 + 32 + 1;
    hdr[10] = 1 + 32;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TXT", 11);
    fd[11] = 'C'; fd[16] = 32;
    push(fd.data(), fd.size());
    file.push_back(0x0D);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

std::vector<UNSIGNED16> u16(const char32_t* cps, std::size_t n) {
    std::vector<UNSIGNED16> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        std::uint32_t cp = static_cast<std::uint32_t>(cps[i]);
        if (cp < 0x10000) {
            out.push_back(static_cast<UNSIGNED16>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<UNSIGNED16>(0xD800u | (cp >> 10)));
            out.push_back(static_cast<UNSIGNED16>(0xDC00u | (cp & 0x3FFu)));
        }
    }
    out.push_back(0);
    return out;
}

}  // namespace

TEST_CASE("M9.17 utf16le_to_utf8 + utf8_to_utf16le round-trip") {
    // 'H' 'e' 'l' 'l' 'o' ' ' U+00F1 ' ' U+6F22 ' ' U+1F600
    char32_t cps[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20,
                      0x00F1, 0x20, 0x6F22, 0x20, 0x1F600, 0};
    std::size_t n = 0;
    while (cps[n] != 0) ++n;
    auto in = u16(cps, n);
    in.pop_back();
    std::string utf8 = openads::abi::utf16le_to_utf8(in.data(), in.size());
    auto round = openads::abi::utf8_to_utf16le(utf8);
    REQUIRE(round.size() == in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        CHECK(round[i] == in[i]);
    }
}

TEST_CASE("M9.17 AdsSetStringW + AdsGetStringW round-trip non-ASCII") {
    const auto dir = fs::temp_directory_path() / "openads_m9_17_w";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hTable = 0;
    UNSIGNED8 leaf[16] = "data";
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    REQUIRE(AdsAppendRecord(hTable) == 0);

    char32_t cps[] = {0x48, 0x6F, 0x6C, 0x61, 0x20,
                      0x4D, 0x75, 0x6E, 0x64, 0x6F, 0x20,
                      0x1F600, 0};
    std::size_t n = 0;
    while (cps[n] != 0) ++n;
    auto value_w = u16(cps, n);
    value_w.pop_back();

    // Field name is ASCII even on the W variants (SAP convention).
    UNSIGNED8 fld[] = "TXT";
    REQUIRE(AdsSetStringW(hTable, fld,
                          value_w.data(),
                          static_cast<UNSIGNED32>(value_w.size())) == 0);

    UNSIGNED16 buf_w[64] = {0};
    UNSIGNED32 cap = 64;
    REQUIRE(AdsGetStringW(hTable, fld, buf_w, &cap, 0) == 0);

    std::string read_utf8 = openads::abi::utf16le_to_utf8(buf_w, cap);
    std::string in_utf8   = openads::abi::utf16le_to_utf8(value_w.data(),
                                                          value_w.size());
    // Field is 32 bytes, so the trailing space-pad lives in `read_utf8`.
    // Trim trailing spaces before compare.
    while (!read_utf8.empty() && read_utf8.back() == ' ') read_utf8.pop_back();
    CHECK(read_utf8 == in_utf8);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.17 AdsGetFieldW = AdsGetStringW alias") {
    const auto dir = fs::temp_directory_path() / "openads_m9_17_w_alias";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hTable = 0;
    UNSIGNED8 leaf[16] = "data";
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    REQUIRE(AdsAppendRecord(hTable) == 0);

    UNSIGNED8  fld[]  = "TXT";
    UNSIGNED16 val_w[] = {'A', 'B', 'C', 0};
    REQUIRE(AdsSetStringW(hTable, fld, val_w, 3) == 0);

    UNSIGNED16 buf_w[64] = {0};
    UNSIGNED32 cap = 64;
    REQUIRE(AdsGetFieldW(hTable, fld, buf_w, &cap, 0) == 0);
    CHECK(buf_w[0] == 'A');
    CHECK(buf_w[1] == 'B');
    CHECK(buf_w[2] == 'C');

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsSetFieldW stores UTF-16 data") {
    const auto dir = fs::temp_directory_path() / "openads_setfieldw";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hTable = 0;
    UNSIGNED8 leaf[16] = "data";
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    REQUIRE(AdsAppendRecord(hTable) == 0);

    UNSIGNED8 fld[] = "TXT";
    UNSIGNED16 val_w[] = {'W', 'i', 'd', 'e', 0};
    REQUIRE(AdsSetFieldW(hTable, fld, val_w, 4) == 0);

    UNSIGNED16 buf_w[16] = {0};
    UNSIGNED32 cap = 16;
    REQUIRE(AdsGetFieldW(hTable, fld, buf_w, &cap, 0) == 0);
    CHECK(buf_w[0] == 'W');
    CHECK(buf_w[1] == 'i');
    CHECK(buf_w[2] == 'd');
    CHECK(buf_w[3] == 'e');

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.17 W variants honour ADSFIELD-style numeric field index") {
    const auto dir = fs::temp_directory_path() / "openads_m9_17_w_numeric";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hTable = 0;
    UNSIGNED8 leaf[16] = "data";
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    REQUIRE(AdsAppendRecord(hTable) == 0);

    // Field 1 = "TXT". Pass numeric 1 cast to UNSIGNED8* — same
    // ADSFIELD(n) convention as the ANSI variants.
    UNSIGNED8* fld_numeric =
        reinterpret_cast<UNSIGNED8*>(static_cast<std::uintptr_t>(1));
    UNSIGNED16 val_w[] = {'X', 0};
    REQUIRE(AdsSetStringW(hTable, fld_numeric, val_w, 1) == 0);

    UNSIGNED16 buf_w[8] = {0};
    UNSIGNED32 cap = 8;
    REQUIRE(AdsGetStringW(hTable, fld_numeric, buf_w, &cap, 0) == 0);
    CHECK(buf_w[0] == 'X');

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
