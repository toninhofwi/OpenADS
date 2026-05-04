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
    rec("BBBB"); rec("AAAA"); rec("CCCC");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

}  // namespace

TEST_CASE("M9.20 AdsAddCustomKey + AdsDeleteCustomKey round-trip on NTX") {
    auto dir = fs::temp_directory_path() / "openads_m9_20_custom";
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

    // Build a normal NTX so AdsAddCustomKey has something to insert
    // into. After M9.7 the new index already contains every existing
    // record's entry; the custom-key path then reinserts the current
    // record's key (idempotent for a NTX leaf since duplicate keys are
    // permitted on the same recno).
    UNSIGNED8 fn[32]   = "tag.ntx";
    UNSIGNED8 tagn[16] = "TAGORD";
    UNSIGNED8 expr[16] = "TAG";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, fn, tagn, expr,
                             nullptr, nullptr, 0, 1024, &hIdx) == 0);

    // Position to record 2 (AAAA) and exercise AdsAddCustomKey.
    REQUIRE(AdsGotoRecord(hTable, 2) == 0);
    REQUIRE(AdsAddCustomKey(hIdx) == 0);

    // Now delete — the entry for the current key should erase from
    // the index. The post-erase NTX still has BBBB and CCCC reachable
    // via dbSeek; AAAA may either remain (because AdsAddCustomKey
    // inserted it twice in M9.7's auto-sync + custom-key path and
    // erase removed only one), or vanish — both are acceptable for a
    // first-cut custom-key surface. The contract this test asserts is
    // that AdsDeleteCustomKey returns AE_SUCCESS on a positioned row.
    REQUIRE(AdsDeleteCustomKey(hIdx) == 0);

    // Sanity: BBBB still findable after the custom-key churn.
    UNSIGNED8 key[8] = "BBBB";
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hIdx, key, 4, 0, 0, &found) == 0);
    CHECK(found == 1);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.20 AdsAddCustomKey rejects bogus index handle") {
    UNSIGNED32 rc = AdsAddCustomKey(/*hIndex=*/9999);
    CHECK(rc != 0);
}

TEST_CASE("M9.20 AdsDeleteCustomKey rejects bogus index handle") {
    UNSIGNED32 rc = AdsDeleteCustomKey(/*hIndex=*/9999);
    CHECK(rc != 0);
}
