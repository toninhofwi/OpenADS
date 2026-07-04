// abi_no_current_record_test.cpp
//
// Verifies that AdsGetField at BOF/EOF returns AE_SUCCESS with a blank
// field template (not AE_INTERNAL_ERROR / 5000).
//
// Background: FWH TDataBase:td_blankrow does DBGOBOTTOM+DBSKIP then FieldGet
// on the append-row position. rddads treats any non-success other than a
// handled no-current-record as a hard error (ADSCDX/5000). OpenADS serves
// type-default blanks with AE_SUCCESS so Harbour xBase callers keep working.

#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Build a minimal DBF on disk: 3 records, one C(5) field "NAME".
fs::path make_dbf_fixture(const char* tag) {
    auto p = fs::temp_directory_path() /
             (std::string("openads_nocurrec_") + tag + ".dbf");
    fs::remove(p);

    std::vector<std::uint8_t> file;

    // DBF header (32 bytes)
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 3; hdr[5] = 0; hdr[6] = 0; hdr[7] = 0;  // 3 records
    hdr[8]  = 32 + 32 + 1; hdr[9] = 0;                 // header size
    hdr[10] = 1 + 5; hdr[11] = 0;                      // record length (delete-flag + C(5))
    file.insert(file.end(), hdr.begin(), hdr.end());

    // Field descriptor (32 bytes): "NAME" C(5)
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "NAME", 11);
    fd[11] = 'C';
    fd[16] = 5;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);  // header terminator

    auto push_rec = [&](const char* name) {
        file.push_back(' ');  // delete flag
        for (int i = 0; i < 5; ++i)
            file.push_back(static_cast<std::uint8_t>(
                i < static_cast<int>(std::strlen(name)) ? name[i] : ' '));
    };
    push_rec("AAA");
    push_rec("BBBB");
    push_rec("CCCCC");
    file.push_back(0x1A);  // EOF marker

    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("AdsGetField at EOF returns blank field (AE_SUCCESS)") {
    const auto dir = fs::temp_directory_path() / "openads_nocurrec_eof";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    make_dbf_fixture("data");
    // Copy the fixture into the working dir that AdsConnect60 will use.
    auto src = fs::temp_directory_path() / "openads_nocurrec_data.dbf";
    fs::copy_file(src, dir / "data.dbf",
                  fs::copy_options::overwrite_existing);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == AE_SUCCESS);

    UNSIGNED8 tname[] = "data.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hT) == AE_SUCCESS);

    // Navigate to EOF: skip well past the last record.
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    REQUIRE(AdsSkip(hT, 100) == AE_SUCCESS);

    UNSIGNED16 at_eof = 0;
    REQUIRE(AdsAtEOF(hT, &at_eof) == AE_SUCCESS);
    REQUIRE(at_eof == 1);

    UNSIGNED8 fname[] = "NAME";
    UNSIGNED8 buf[64] = {0};
    UNSIGNED32 cap = sizeof(buf);
    UNSIGNED32 rc = AdsGetField(hT, fname, buf, &cap, 0);
    CHECK(rc == AE_SUCCESS);
    CHECK(cap == 5);
    CHECK(std::string(reinterpret_cast<char*>(buf), cap) == "     ");

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
    fs::remove(src, ec);
}

TEST_CASE("AdsGetField at BOF returns blank field (AE_SUCCESS)") {
    const auto dir = fs::temp_directory_path() / "openads_nocurrec_bof";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    make_dbf_fixture("data2");
    auto src = fs::temp_directory_path() / "openads_nocurrec_data2.dbf";
    fs::copy_file(src, dir / "data.dbf",
                  fs::copy_options::overwrite_existing);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == AE_SUCCESS);

    UNSIGNED8 tname[] = "data.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hT) == AE_SUCCESS);

    // Navigate to BOF: GOTOP then skip backward past the first record.
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    REQUIRE(AdsSkip(hT, -100) == AE_SUCCESS);

    UNSIGNED16 at_bof = 0;
    REQUIRE(AdsAtBOF(hT, &at_bof) == AE_SUCCESS);
    REQUIRE(at_bof == 1);

    UNSIGNED8 fname[] = "NAME";
    UNSIGNED8 buf[64] = {0};
    UNSIGNED32 cap = sizeof(buf);
    UNSIGNED32 rc = AdsGetField(hT, fname, buf, &cap, 0);
    CHECK(rc == AE_SUCCESS);
    CHECK(cap == 5);
    CHECK(std::string(reinterpret_cast<char*>(buf), cap) == "     ");

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
    fs::remove(src, ec);
}

TEST_CASE("AdsGetField on a positioned record succeeds") {
    // Sanity check: normal positioned reads must not be affected by the fix.
    const auto dir = fs::temp_directory_path() / "openads_nocurrec_positioned";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    make_dbf_fixture("data3");
    auto src = fs::temp_directory_path() / "openads_nocurrec_data3.dbf";
    fs::copy_file(src, dir / "data.dbf",
                  fs::copy_options::overwrite_existing);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == AE_SUCCESS);

    UNSIGNED8 tname[] = "data.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hT) == AE_SUCCESS);

    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);

    UNSIGNED16 at_eof = 0, at_bof = 0;
    REQUIRE(AdsAtEOF(hT, &at_eof) == AE_SUCCESS);
    REQUIRE(AdsAtBOF(hT, &at_bof) == AE_SUCCESS);
    CHECK(at_eof == 0);
    CHECK(at_bof == 0);

    UNSIGNED8 fname[] = "NAME";
    UNSIGNED8 buf[64] = {0};
    UNSIGNED32 cap = sizeof(buf);
    UNSIGNED32 rc = AdsGetField(hT, fname, buf, &cap, 0);
    CHECK(rc == AE_SUCCESS);  // 0
    // First record is "AAA" space-padded to 5 chars.
    CHECK(cap == 5);

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
    fs::remove(src, ec);
}
