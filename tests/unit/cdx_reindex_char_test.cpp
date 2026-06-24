// cdx_reindex_char_test.cpp — characterize the post-REINDEX ADSCDX/5000
// the daily chaos harness hits on the middle tag. Table::reindex() empties
// each index by erasing every (recno,key) one at a time (NOT clear_data,
// so the multi-level page structure stays), then re-inserts every live
// record. Reproduce that erase-all-then-reinsert cycle on a multi-level
// char tag and check the tree is intact afterwards.

#include "doctest.h"
#include "drivers/cdx/cdx_index.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
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

TEST_CASE("CDX reindex-style erase-all + reinsert keeps a multi-level tag") {
    auto p = fs::temp_directory_path() / "openads_cdx_reindex.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    constexpr int N = 8000;
    {
        auto c = CdxIndex::create(p.string(), "ORD_NOM", "NOMBRE", 30,
                                  false, false);
        REQUIRE(c.has_value());
        CdxIndex ix = std::move(c).value();
        for (int i = 1; i <= N; ++i)
            REQUIRE(ix.insert(static_cast<std::uint32_t>(i),
                              nom_key(i)).has_value());
        REQUIRE(ix.flush().has_value());

        // --- reindex(): collect every entry via the cursor, erase each,
        // then re-insert every record (same as Table::reindex step 1+2). ---
        std::vector<std::pair<std::uint32_t, std::string>> entries;
        auto s = ix.seek_first();
        while (s && s.value().positioned) {
            entries.emplace_back(s.value().recno, ix.current_key());
            s = ix.next();
        }
        REQUIRE(entries.size() == static_cast<std::size_t>(N));
        for (auto& [rec, key] : entries)
            REQUIRE(ix.erase(rec, key).has_value());
        for (int i = 1; i <= N; ++i)
            REQUIRE(ix.insert(static_cast<std::uint32_t>(i),
                              nom_key(i)).has_value());
        REQUIRE(ix.flush().has_value());
    }
    // After the reindex cycle every key must seek to its recno and an
    // ordered walk must visit all N with in-range recnos.
    {
        CdxIndex ix;
        REQUIRE(ix.open_named(p.string(), IndexOpenMode::Shared, "ORD_NOM")
                    .has_value());
        for (int i = 1; i <= N; ++i) {
            auto seek = ix.seek_key(nom_key(i), false);
            INFO("reindex seek i=" << i);
            REQUIRE(seek.has_value());
            CHECK(seek.value().hit == SeekHit::Exact);
            CHECK(seek.value().recno == static_cast<std::uint32_t>(i));
        }
        auto s = ix.seek_first();
        REQUIRE(s.has_value());
        int seen = 0;
        std::string prev;
        while (s.value().positioned) {
            ++seen;
            std::uint32_t r = s.value().recno;
            INFO("reindex walk seen=" << seen << " recno=" << r);
            CHECK(r >= 1u);
            CHECK(r <= static_cast<std::uint32_t>(N));
            std::string cur = ix.current_key();
            if (!prev.empty()) CHECK(cur >= prev);
            prev = cur;
            s = ix.next();
            REQUIRE(s.has_value());
        }
        CHECK(seen == N);
    }
    fs::remove(p, ec);
}
