#include "doctest.h"
#include "drivers/ntx/ntx_index.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using openads::drivers::IndexOpenMode;
using openads::drivers::SeekHit;
using openads::drivers::ntx::NtxIndex;

TEST_CASE("M9.10 NTX multi-level split survives many sequential inserts") {
    auto p = fs::temp_directory_path() / "openads_m910_ntx_multilevel.ntx";
    fs::remove(p);

    {
        auto created = NtxIndex::create(p.string(), "T1", "TAG", 4,
                                         /*unique*/ false, /*descend*/ false);
        REQUIRE(created.has_value());
        NtxIndex ix = std::move(created).value();

        // 1024-byte page with 4-byte keys can hold ~84 entries per
        // leaf, so 200 inserts force a multi-level split (one branch
        // root above two leaves can't hold all 200, so the next
        // insert must split a level deeper).
        constexpr int N = 200;
        for (int i = 1; i <= N; ++i) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04d", i);
            auto e = ix.insert(static_cast<std::uint32_t>(i),
                               std::string(buf, 4));
            INFO("insert i=" << i);
            REQUIRE(e.has_value());
        }
        REQUIRE(ix.flush().has_value());
    }

    // Reopen and verify every key is reachable.
    {
        NtxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        for (int i = 1; i <= 200; ++i) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04d", i);
            auto seek = ix.seek_key(std::string(buf, 4), false);
            INFO("seek i=" << i);
            REQUIRE(seek.has_value());
            CHECK(seek.value().hit == SeekHit::Exact);
            CHECK(seek.value().recno == static_cast<std::uint32_t>(i));
        }
    }

    // Walk in order: must visit every recno 1..200 in sorted-key order.
    {
        NtxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        auto seek = ix.seek_first();
        REQUIRE(seek.has_value());
        int seen = 0;
        std::string prev_key;
        while (seek.value().positioned) {
            ++seen;
            std::string cur = ix.current_key();
            if (!prev_key.empty()) {
                CHECK(cur >= prev_key);
            }
            prev_key = cur;
            seek = ix.next();
            REQUIRE(seek.has_value());
        }
        CHECK(seen == 200);
    }
    fs::remove(p);
}
