// Regression: building a character-key ADI index large enough to force a
// multi-level B-tree (the root branch itself splits) must complete and stay
// navigable. Before the fix, the root-branch split reused root_page_ for both
// the new root AND its left child, producing a self-referential child pointer;
// the next insert's descent then looped forever, so the build ran away in time
// and memory. (The 1-byte child-page field also truncated page numbers > 255.)
#include "doctest.h"
#include "drivers/adi/adi_index.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {
std::string trim_sp(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
} // namespace

TEST_CASE("ADI: char-key index build forces multi-level tree and stays navigable") {
    fs::path tmp = fs::temp_directory_path() / "openads_adi_multilevel";
    { std::error_code ec; fs::create_directories(tmp, ec);
      fs::remove(tmp / "big.adt", ec); fs::remove(tmp / "big.adi", ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[]    = "big.adt";
    UNSIGNED8 flddef[] = "Code,Character,8";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    // Enough rows that the root branch (not just a leaf) has to split.
    const int N = 8000;
    for (int i = 1; i <= N; ++i) {
        char code[16];
        std::snprintf(code, sizeof(code), "%08d", i);
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        REQUIRE(AdsSetString(hTable, (UNSIGNED8*)"Code", (UNSIGNED8*)code, 8)
                == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    }

    UNSIGNED8 idxfile[] = "big.adi";
    ADSHANDLE hIdx = 0;
    // Pre-fix this call never returned (infinite descent after the root split).
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"CODE",
                             (UNSIGNED8*)"Code", nullptr, nullptr, 0, 0, &hIdx)
            == AE_SUCCESS);

    // Full ordered walk visits every row exactly once, ascending.
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    int walked = 0;
    std::string prev;
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hTable, &eof) == AE_SUCCESS);
        if (eof) break;
        UNSIGNED8 buf[16]{}; UNSIGNED32 len = sizeof(buf);
        REQUIRE(AdsGetString(hTable, (UNSIGNED8*)"Code", buf, &len, 0)
                == AE_SUCCESS);
        std::string cur = trim_sp(std::string(reinterpret_cast<char*>(buf), len));
        CHECK(cur > prev);            // strictly ascending
        prev = cur;
        ++walked;
        REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    }
    CHECK(walked == N);

    // Seek several keys spread across the tree -> all found.
    for (int key : {1, 1234, 4000, 7999, N}) {
        char code[16];
        std::snprintf(code, sizeof(code), "%08d", key);
        UNSIGNED16 found = 0;
        REQUIRE(AdsSeek(hIdx, (UNSIGNED8*)code, 8, 0, 0, &found) == AE_SUCCESS);
        CHECK(found != 0);
    }

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}
