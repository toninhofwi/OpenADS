// VFP header 0x32: autoinc + nullable columns in one table.
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

void write_u32_le(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>( v        & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >>  8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

fs::path stage_combined_dbf(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "data.dbf";
    fs::remove(p);

    // rec_len = 1 (del) + 4 (null bitmap) + 4 (ID) + 10 (NAME) = 19
    constexpr std::uint16_t REC_LEN = 19;

    std::vector<std::uint8_t> file;

    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x32;
    hdr[4]  = 1;                   // one record
    hdr[8]  = 32 + 64 + 1;       // header + two field descriptors + 0x0D
    hdr[9]  = 0;
    hdr[10] = REC_LEN & 0xFF;
    hdr[11] = (REC_LEN >> 8) & 0xFF;
    file.insert(file.end(), hdr.begin(), hdr.end());

    std::array<std::uint8_t, 32> id_fd{};
    std::strncpy(reinterpret_cast<char*>(id_fd.data()), "ID", 11);
    id_fd[11] = 'I';
    id_fd[16] = 4;
    id_fd[18] = 0x0Cu;             // autoinc
    write_u32_le(id_fd.data() + 19, 11);  // next autoinc value to issue
    id_fd[23] = 1;
    file.insert(file.end(), id_fd.begin(), id_fd.end());

    std::array<std::uint8_t, 32> name_fd{};
    std::strncpy(reinterpret_cast<char*>(name_fd.data()), "NAME", 11);
    name_fd[11] = 'C';
    name_fd[16] = 10;
    name_fd[18] = 0x02u;           // nullable
    file.insert(file.end(), name_fd.begin(), name_fd.end());

    file.push_back(0x0D);

    // Record 1: active, NAME null (bit 0), ID=10, NAME ignored
    file.push_back(' ');
    file.push_back(0x01);          // null bitmap: bit 0 set
    file.push_back(0);
    file.push_back(0);
    file.push_back(0);
    file.push_back(10);
    file.push_back(0);
    file.push_back(0);
    file.push_back(0);
    file.insert(file.end(), 10, ' ');

    file.push_back(0x1A);

    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("VFP 0x32: autoinc + nullable coexist — offsets and AdsIsNull") {
    const auto dir = fs::temp_directory_path() / "openads_vfp_0x32_combined";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_combined_dbf(dir);

    UNSIGNED8 srv[256]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == AE_SUCCESS);

    UNSIGNED8 leaf[] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         ADS_ANSI, ADS_READONLY,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT,
                         &hTable) == AE_SUCCESS);

    UNSIGNED16 nflds = 0;
    REQUIRE(AdsGetNumFields(hTable, &nflds) == AE_SUCCESS);
    CHECK(nflds == 3);  // ID, NAME, synthetic _NullFlags

    REQUIRE(AdsGotoRecord(hTable, 1) == AE_SUCCESS);

    UNSIGNED8 id_f[] = "ID";
    SIGNED32 id_val = 0;
    REQUIRE(AdsGetLong(hTable, id_f, &id_val) == AE_SUCCESS);
    CHECK(id_val == 10);

    UNSIGNED8 name_f[] = "NAME";
    UNSIGNED16 name_null = 0;
    REQUIRE(AdsIsNull(hTable, name_f, &name_null) == AE_SUCCESS);
    CHECK(name_null != 0);

    UNSIGNED16 id_null = 0;
    REQUIRE(AdsIsNull(hTable, id_f, &id_null) == AE_SUCCESS);
    CHECK(id_null == 0);

    // Append picks up autoinc counter (11) without disturbing null layout.
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);

    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == AE_SUCCESS);
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT,
                         &hTable) == AE_SUCCESS);
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    REQUIRE(AdsGotoRecord(hTable, 2) == AE_SUCCESS);
    REQUIRE(AdsGetLong(hTable, id_f, &id_val) == AE_SUCCESS);
    CHECK(id_val == 11);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    fs::remove_all(dir, ec);
}