// CDX index direction decode: AdsCreateIndex61 must build an ASCENDING tag
// for a plain `INDEX ON` regardless of which RDD client issued it. The two
// clients we interop with put the compound/descending flags on SWAPPED bits
// of the ulOptions word (measured by instrumenting AdsCreateIndex61):
//
//   client          ascending tag   descending tag
//   X#  ADSRDD       0x02            0x0A   (compound 0x02 | descending 0x08)
//   Harbour rddads   0x08            0x0A   (compound 0x08 | descending 0x02)
//
// Reading a lone 0x08 as "descending" (the SDK bit) built every Harbour
// ascending order reversed: AdsGotoTop landed on the last key and SKIP walked
// backward, so a FiveWin/Harbour browse over an OpenADS table showed its rows
// upside-down. For a compound (.cdx) tag, descending is the one case where
// BOTH 0x02 and 0x08 are set; an ascending tag carries exactly one of them.
//
// This builds the index through the ACE ABI for each client's option word and
// asserts the resulting tag's stored direction + ordered walk.

#include "doctest.h"
#include "openads/ace.h"
#include "drivers/cdx/cdx_index.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openads::drivers::IndexOpenMode;
using openads::drivers::cdx::CdxIndex;

namespace {

constexpr std::uint16_t kNameLen = 10;

std::string name_key(const std::string& s) {
    std::string k = s;
    k.resize(kNameLen, ' ');
    return k;
}

// DBF with one char field NAME C(10), records out of order so ascending and
// descending GoTop differ: "BBB" / "AAA" / "CCC".
fs::path stage_dbf(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "dir.dbf";
    std::error_code ec;
    fs::remove(p, ec);

    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };

    const std::uint16_t rec_len = 1 + kNameLen;
    const std::uint16_t hdr_len = 32 + 32 + 1;

    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 3;
    hdr[8]  = hdr_len & 0xFF; hdr[9]  = (hdr_len >> 8) & 0xFF;
    hdr[10] = rec_len & 0xFF; hdr[11] = (rec_len >> 8) & 0xFF;
    push(hdr.data(), hdr.size());

    std::array<std::uint8_t, 32> fld{};
    std::strncpy(reinterpret_cast<char*>(fld.data()), "NAME", 11);
    fld[11] = 'C';
    fld[16] = static_cast<std::uint8_t>(kNameLen);
    fld[17] = 0;
    push(fld.data(), fld.size());

    file.push_back(0x0D);

    auto rec = [&](const std::string& v) {
        file.push_back(' ');
        std::string k = name_key(v);
        push(k.data(), k.size());
    };
    rec("BBB");
    rec("AAA");
    rec("CCC");
    file.push_back(0x1A);

    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

// Create one tag through the ABI with the given option word, reopen the bag,
// and return (descending_flag, ordered_walk_of_keys).
struct TagResult {
    bool                     descending = false;
    std::vector<std::string> walk;       // seek_first .. next
};

TagResult build_and_read(const fs::path& dir, const std::string& bag,
                         const std::string& tag, UNSIGNED32 ulOptions) {
    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 name[16] = "dir";
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 idxfile[32]; std::memcpy(idxfile, bag.c_str(), bag.size() + 1);
    UNSIGNED8 idxname[32]; std::memcpy(idxname, tag.c_str(), tag.size() + 1);
    UNSIGNED8 expr[16] = "NAME";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, idxname, expr,
                             nullptr, nullptr, ulOptions, 512, &hIdx) == 0);
    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    auto bagpath = (dir / bag).string();
    REQUIRE(fs::exists(bagpath));

    CdxIndex ix;
    REQUIRE(ix.open_named(bagpath, IndexOpenMode::Shared, tag).has_value());

    TagResult r;
    r.descending = ix.descending();
    auto s = ix.seek_first();
    REQUIRE(s.has_value());
    while (s.value().positioned) {
        std::string k = ix.current_key();
        while (!k.empty() && k.back() == ' ') k.pop_back();
        r.walk.push_back(k);
        s = ix.next();
        REQUIRE(s.has_value());
    }
    return r;
}

} // namespace

TEST_CASE("CDX index direction: client compound/descending bits decode correctly") {
    auto dir = fs::temp_directory_path() / "openads_cdx_index_dir";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    // Keys are always STORED ascending in the leaf; "descending" is a stored
    // header flag the engine consults at navigation time (Table::goto_top ->
    // seek_last, SKIP -> prev). So the raw seek_first/next walk is ascending
    // for every case — the bug was the header DIRECTION FLAG, which this test
    // pins. End-to-end GoTop/SKIP direction is covered by the FiveWin
    // integration test (examples/fivewin/tdata_index_test.prg).
    const std::vector<std::string> asc{"AAA", "BBB", "CCC"};

    SUBCASE("X# ascending (0x02) builds ascending") {
        auto r = build_and_read(dir, "xs_asc.cdx", "T", ADS_COMPOUND);  // 0x02
        CHECK_FALSE(r.descending);
        CHECK(r.walk == asc);
    }
    SUBCASE("Harbour ascending (0x08) builds ascending") {
        auto r = build_and_read(dir, "hb_asc.cdx", "T", ADS_DESCENDING); // 0x08
        CHECK_FALSE(r.descending);
        CHECK(r.walk == asc);
    }
    SUBCASE("descending (0x0A = compound|descending) sets the descending flag") {
        auto r = build_and_read(dir, "desc.cdx", "T",
                                ADS_COMPOUND | ADS_DESCENDING);          // 0x0A
        CHECK(r.descending);
        CHECK(r.walk == asc);   // leaf still stored ascending
    }

    fs::remove_all(dir, ec);
}
