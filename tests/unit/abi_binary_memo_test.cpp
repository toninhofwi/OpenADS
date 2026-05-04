#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "drivers/fpt/fpt_memo.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path make_dbf_with_memo(const fs::path& dir, const char* leaf,
                            std::uint8_t version) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    fs::remove(p);
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = version;
    hdr[4]  = 0;
    hdr[8]  = 32 + 64 + 1; hdr[9]  = 0;
    hdr[10] = 1 + 5 + 10;  hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> name_fd{};
    std::strncpy(reinterpret_cast<char*>(name_fd.data()), "NAME", 11);
    name_fd[11] = 'C'; name_fd[16] = 5;
    file.insert(file.end(), name_fd.begin(), name_fd.end());
    std::array<std::uint8_t, 32> blob_fd{};
    std::strncpy(reinterpret_cast<char*>(blob_fd.data()), "BLOB", 11);
    blob_fd[11] = 'M'; blob_fd[16] = 10;
    file.insert(file.end(), blob_fd.begin(), blob_fd.end());
    file.push_back(0x0D);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

}  // namespace

TEST_CASE("AdsSetBinary + AdsGetBinaryLength + AdsGetBinary round-trip") {
    const auto dir = fs::temp_directory_path() / "openads_m9_13_bin";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf_with_memo(dir, "data.dbf", 0x83);
    REQUIRE(openads::drivers::fpt::FptMemo::create((dir / "data.fpt").string(),
                                                   64).has_value());

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 leaf[64] = "data.dbf";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    REQUIRE(AdsAppendRecord(hTable) == 0);

    UNSIGNED8 fld[16] = "BLOB";
    // 200-byte payload with embedded NULs and high-bit bytes — a
    // realistic binary blob, not a clean ASCII string.
    std::vector<UNSIGNED8> payload(200);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<UNSIGNED8>(i & 0xFF);
    }
    REQUIRE(AdsSetBinary(hTable, fld, ADS_BINARY,
                         static_cast<UNSIGNED32>(payload.size()),
                         /*ulOffset=*/0,
                         payload.data(),
                         static_cast<UNSIGNED32>(payload.size())) == 0);

    UNSIGNED32 len = 0;
    REQUIRE(AdsGetBinaryLength(hTable, fld, &len) == 0);
    CHECK(len == 200);

    std::vector<UNSIGNED8> recv(payload.size(), 0);
    UNSIGNED32 cap = static_cast<UNSIGNED32>(recv.size());
    REQUIRE(AdsGetBinary(hTable, fld, /*ulOffset=*/0,
                         recv.data(), &cap) == 0);
    CHECK(cap == payload.size());
    CHECK(std::memcmp(recv.data(), payload.data(), payload.size()) == 0);

    UNSIGNED16 mtype = 0;
    REQUIRE(AdsGetMemoDataType(hTable, fld, &mtype) == 0);
    CHECK(mtype == ADS_BINARY);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsGetBinary respects ulOffset for chunked reads") {
    const auto dir = fs::temp_directory_path() / "openads_m9_13_bin_off";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf_with_memo(dir, "data.dbf", 0x83);
    REQUIRE(openads::drivers::fpt::FptMemo::create((dir / "data.fpt").string(),
                                                   64).has_value());

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[64] = "data.dbf";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);
    REQUIRE(AdsAppendRecord(hTable) == 0);

    UNSIGNED8 fld[16] = "BLOB";
    std::vector<UNSIGNED8> payload(64);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<UNSIGNED8>('A' + (i % 26));
    }
    REQUIRE(AdsSetBinary(hTable, fld, ADS_IMAGE,
                         static_cast<UNSIGNED32>(payload.size()),
                         0, payload.data(),
                         static_cast<UNSIGNED32>(payload.size())) == 0);

    UNSIGNED8 win[16] = {0};
    UNSIGNED32 cap = sizeof(win);
    REQUIRE(AdsGetBinary(hTable, fld, /*ulOffset=*/40, win, &cap) == 0);
    CHECK(cap == 16);
    for (std::size_t i = 0; i < 16; ++i) {
        CHECK(win[i] == payload[40 + i]);
    }

    cap = sizeof(win);
    std::memset(win, 0, sizeof(win));
    REQUIRE(AdsGetBinary(hTable, fld, /*ulOffset=*/100, win, &cap) == 0);
    CHECK(cap == 0);

    UNSIGNED16 mtype = 0;
    REQUIRE(AdsGetMemoDataType(hTable, fld, &mtype) == 0);
    CHECK(mtype == ADS_IMAGE);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsSetBinary chunked write returns AE_FUNCTION_NOT_AVAILABLE") {
    const auto dir = fs::temp_directory_path() / "openads_m9_13_bin_chunk";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf_with_memo(dir, "data.dbf", 0x83);
    REQUIRE(openads::drivers::fpt::FptMemo::create((dir / "data.fpt").string(),
                                                   64).has_value());

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[64] = "data.dbf";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);
    REQUIRE(AdsAppendRecord(hTable) == 0);

    UNSIGNED8 fld[16] = "BLOB";
    UNSIGNED8 buf[8]  = {0,1,2,3,4,5,6,7};
    UNSIGNED32 rc = AdsSetBinary(hTable, fld, ADS_BINARY,
                                 /*total=*/16, /*offset=*/8,
                                 buf, /*bytes=*/8);
    CHECK(rc == openads::AE_FUNCTION_NOT_AVAILABLE);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsGetMemoDataType reports Text for default text writes") {
    const auto dir = fs::temp_directory_path() / "openads_m9_13_text";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf_with_memo(dir, "data.dbf", 0x83);
    REQUIRE(openads::drivers::fpt::FptMemo::create((dir / "data.fpt").string(),
                                                   64).has_value());

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[64] = "data.dbf";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);
    REQUIRE(AdsAppendRecord(hTable) == 0);

    UNSIGNED8 fld[16] = "BLOB";
    UNSIGNED8 txt[16] = "hello memo";
    REQUIRE(AdsSetString(hTable, fld, txt, 10) == 0);

    UNSIGNED16 mtype = 99;
    REQUIRE(AdsGetMemoDataType(hTable, fld, &mtype) == 0);
    CHECK(mtype == ADS_STRING);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
