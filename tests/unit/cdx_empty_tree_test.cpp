// cdx_empty_tree_test.cpp — coverage for CDX index operations on an empty
// or all-erased tree. Verifies that seek_first/seek_last/seek_key return
// AfterEnd on an empty tree, and that prefix seek + descending index work
// correctly after bulk erase leaves all leaves empty.

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
std::string key_n(int n, int width = 20) {
    char num[16];
    std::snprintf(num, sizeof(num), "%08d", n);
    std::string k = std::string("KEY-") + num;
    k.resize(static_cast<std::size_t>(width), ' ');
    return k;
}
} // namespace

TEST_CASE("CDX empty tree: seek_first / seek_last / seek_key all return AfterEnd") {
    auto p = fs::temp_directory_path() / "openads_cdx_empty.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    {
        auto c = CdxIndex::create(p.string(), "EMPTY", "FIELD", 20,
                                  false, false);
        REQUIRE(c.has_value());
        // Don't insert anything — the tree is empty.
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());

        auto sf = ix.seek_first();
        REQUIRE(sf.has_value());
        CHECK_FALSE(sf.value().positioned);  // AfterEnd

        auto sl = ix.seek_last();
        REQUIRE(sl.has_value());
        CHECK_FALSE(sl.value().positioned);  // AfterEnd

        auto sk = ix.seek_key(std::string("KEY-00000001"), false);
        REQUIRE(sk.has_value());
        CHECK_FALSE(sk.value().positioned);  // AfterEnd
    }
    fs::remove(p, ec);
}

TEST_CASE("CDX all-erased tree: forward walk crosses empty leaves") {
    auto p = fs::temp_directory_path() / "openads_cdx_all_empty.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    constexpr int N = 2000;
    {
        auto c = CdxIndex::create(p.string(), "ALLDEL", "FIELD", 20,
                                  false, false);
        REQUIRE(c.has_value());
        CdxIndex ix = std::move(c).value();
        for (int i = 1; i <= N; ++i)
            REQUIRE(ix.insert(static_cast<std::uint32_t>(i),
                              key_n(i)).has_value());
        REQUIRE(ix.flush().has_value());
        // Erase every key.
        for (int i = 1; i <= N; ++i)
            REQUIRE(ix.erase(static_cast<std::uint32_t>(i),
                             key_n(i)).has_value());
        REQUIRE(ix.flush().has_value());
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());

        auto sf = ix.seek_first();
        REQUIRE(sf.has_value());
        CHECK_FALSE(sf.value().positioned);  // all empty → AfterEnd

        auto sl = ix.seek_last();
        REQUIRE(sl.has_value());
        CHECK_FALSE(sl.value().positioned);

        auto sk = ix.seek_key(key_n(500), false);
        REQUIRE(sk.has_value());
        CHECK_FALSE(sk.value().positioned);
    }
    fs::remove(p, ec);
}

TEST_CASE("CDX prefix seek: exact-length key matches fully") {
    auto p = fs::temp_directory_path() / "openads_cdx_prefix_exact.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    constexpr int N = 500;
    {
        auto c = CdxIndex::create(p.string(), "PEXACT", "FIELD", 20,
                                  false, false);
        REQUIRE(c.has_value());
        CdxIndex ix = std::move(c).value();
        for (int i = 1; i <= N; ++i)
            REQUIRE(ix.insert(static_cast<std::uint32_t>(i),
                              key_n(i)).has_value());
        REQUIRE(ix.flush().has_value());
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        // Full 20-char key must match exactly.
        for (int i = 1; i <= N; i += 50) {
            auto sk = ix.seek_key(key_n(i), false);
            INFO("exact seek i=" << i);
            REQUIRE(sk.has_value());
            CHECK(sk.value().positioned);
            CHECK(sk.value().hit == SeekHit::Exact);
            CHECK(sk.value().recno == static_cast<std::uint32_t>(i));
        }
    }
    fs::remove(p, ec);
}

TEST_CASE("CDX descending prefix seek") {
    auto p = fs::temp_directory_path() / "openads_cdx_prefix_desc.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    constexpr int N = 1000;
    {
        auto c = CdxIndex::create(p.string(), "PDESC", "FIELD", 20,
                                  true, false);  // descending
        REQUIRE(c.has_value());
        CdxIndex ix = std::move(c).value();
        for (int i = 1; i <= N; ++i)
            REQUIRE(ix.insert(static_cast<std::uint32_t>(i),
                              key_n(i)).has_value());
        REQUIRE(ix.flush().has_value());
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        // Prefix seek on descending: first matching key in descending order.
        std::string prefix = "KEY-000005";  // 12-char prefix
        auto sk = ix.seek_key(prefix, false);
        INFO("desc prefix seek '" << prefix << "'");
        REQUIRE(sk.has_value());
        CHECK(sk.value().positioned);
        // Must land on a key starting with the prefix.
        std::string cur = ix.current_key();
        CHECK(cur.substr(0, prefix.size()) == prefix);
    }
    fs::remove(p, ec);
}
