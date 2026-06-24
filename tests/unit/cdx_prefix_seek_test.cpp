// cdx_prefix_seek_test.cpp — partial (prefix) dbSeek on a character tag.
//
// Clipper / DBFCDX semantics: SEEK with a search key SHORTER than the
// index key matches on the prefix (it finds the first stored key that
// begins with the search string). OpenADS used to pad the search key to
// the full index width with spaces and require a full-width match, so a
// partial seek like SEEK "ART-00024800" against a stored "ART-00024800
// desc ..." key missed — exactly the "2nd tag unseekable" symptom from
// the real-world report (where the harness seeks a 12-char prefix of a
// 40-char key).

#include "doctest.h"
#include "drivers/cdx/cdx_index.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using openads::drivers::IndexOpenMode;
using openads::drivers::SeekHit;
using openads::drivers::cdx::CdxIndex;

namespace {
std::string art_key(int n) {            // mirror CNOMBREART: 40-char field
    char num[16];
    std::snprintf(num, sizeof(num), "%08d", n);
    std::string k = std::string("ART-") + num + " desc";
    k.resize(40, ' ');
    return k;
}
} // namespace

TEST_CASE("CDX partial (prefix) seek finds the full key") {
    auto p = fs::temp_directory_path() / "openads_cdx_prefix.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    constexpr int N = 3000;
    {
        auto created = CdxIndex::create(p.string(), "ORD2", "CNOMBREART", 40,
                                         false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        // recno i carries key for (N - i): ascending in the index.
        for (int i = 1; i <= N; ++i)
            REQUIRE(ix.insert(static_cast<std::uint32_t>(i),
                              art_key(N - i)).has_value());
        REQUIRE(ix.flush().has_value());
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        // Seek a 12-char PREFIX of a 40-char stored key (no " desc",
        // no padding) — must land on the matching full key.
        for (int n = 0; n < N; n += 137) {
            char num[16];
            std::snprintf(num, sizeof(num), "%08d", n);
            std::string prefix = std::string("ART-") + num;   // 12 chars
            auto seek = ix.seek_key(prefix, /*soft=*/false);
            INFO("prefix seek n=" << n << " '" << prefix << "'");
            REQUIRE(seek.has_value());
            CHECK(seek.value().positioned);
            CHECK(seek.value().hit == SeekHit::Exact);
            // recno that carried key n is i where N - i == n -> i = N - n.
            CHECK(seek.value().recno == static_cast<std::uint32_t>(N - n));
        }
        // A prefix that matches nothing must still miss cleanly.
        auto miss = ix.seek_key(std::string("ZZZ-"), false);
        REQUIRE(miss.has_value());
        CHECK_FALSE(miss.value().positioned);
    }
    fs::remove(p, ec);
}
