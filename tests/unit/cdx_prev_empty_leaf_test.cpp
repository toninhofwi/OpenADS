// cdx_prev_empty_leaf_test.cpp — backward-walk counterpart to the
// skip-empty-leaves fix. erase() empties a leaf but does not merge or
// unlink it, so the leaf sibling chain can hold holes. The forward walks
// (seek_first/seek_key/next/seek_last) skip them, but prev() followed the
// LEFT sibling pointer one hop and stopped if it landed on an empty leaf —
// reporting begin-of-index while live keys remained further left.
//
// Reproduce a guaranteed MIDDLE hole: insert a multi-level tag, then erase
// every key except a small cluster at each end. The middle leaves become
// fully empty; a backward walk from seek_last() must skip them and still
// reach the leftmost live cluster.

#include "doctest.h"
#include "drivers/cdx/cdx_index.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openads::drivers::IndexOpenMode;
using openads::drivers::SeekHit;
using openads::drivers::cdx::CdxIndex;

namespace {
std::string nom_key(int n) {            // mirror NOMBRE: 30-char field
    char num[16];
    std::snprintf(num, sizeof(num), "%08d", n);
    std::string k = std::string("Cliente ") + num;
    k.resize(30, ' ');
    return k;
}
} // namespace

TEST_CASE("CDX prev() skips empty middle leaves on a backward walk") {
    auto p = fs::temp_directory_path() / "openads_cdx_prev_hole.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    constexpr int N = 8000;
    // Keep a cluster at each end; erase the entire middle so the leaves
    // between the two clusters are emptied (holes in the sibling chain).
    const std::set<int> keep = {1, 2, 3, 4, N - 3, N - 2, N - 1, N};

    {
        auto c = CdxIndex::create(p.string(), "ORD_NOM", "NOMBRE", 30,
                                  false, false);
        REQUIRE(c.has_value());
        CdxIndex ix = std::move(c).value();
        for (int i = 1; i <= N; ++i)
            REQUIRE(ix.insert(static_cast<std::uint32_t>(i),
                              nom_key(i)).has_value());
        REQUIRE(ix.flush().has_value());

        for (int i = 1; i <= N; ++i)
            if (!keep.count(i))
                REQUIRE(ix.erase(static_cast<std::uint32_t>(i),
                                 nom_key(i)).has_value());
        REQUIRE(ix.flush().has_value());
    }

    CdxIndex ix;
    REQUIRE(ix.open_named(p.string(), IndexOpenMode::Shared, "ORD_NOM")
                .has_value());

    // Backward walk: seek_last() then prev() to before-begin. Must visit
    // every kept key in descending order, crossing the empty middle.
    auto s = ix.seek_last();
    REQUIRE(s.has_value());
    int seen = 0;
    std::string prevkey;
    while (s.value().positioned) {
        ++seen;
        std::uint32_t r = s.value().recno;
        INFO("backward walk seen=" << seen << " recno=" << r);
        CHECK(r >= 1u);
        CHECK(r <= static_cast<std::uint32_t>(N));
        std::string cur = ix.current_key();
        if (!prevkey.empty()) CHECK(cur <= prevkey);   // strictly descending
        prevkey = cur;
        s = ix.prev();
        REQUIRE(s.has_value());
    }
    CHECK(seen == static_cast<int>(keep.size()));

    fs::remove(p, ec);
}
