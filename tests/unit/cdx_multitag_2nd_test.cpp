// cdx_multitag_2nd_test.cpp — isolate the real-world ADSCDX/5000 report:
//   create TAG ORD1 (8-byte key, ascending-with-recno), then a SECOND
//   tag ORD2 (40-byte key, inserted in DESCENDING key order) on the same
//   compound CDX. In the field the 2nd tag is born corrupt: unseekable
//   at small N, ADSCDX/5000 on an ordered walk once it goes multi-level.
//
// These three cases pin down WHICH layer breaks, so we don't guess:
//   A) single tag, ORD2-style keys (40B, descending insert) — tests the
//      multi-level split path in isolation (no add_tag, no sibling rebuild).
//   B) ORD1 + add_tag(ORD2) + per-record insert, NO sibling rebuild —
//      tests the compound add_tag build path.
//   C) B, then the ABI "sibling refresh" (open ORD1, clear_data, rebuild,
//      flush) — tests whether rebuilding a sibling corrupts ORD2.
//
// Whichever case first goes RED is the root-cause layer.

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

// ORD2 key for record i (1-based) given N records: descending in i.
//   "ART-" + zero-padded (N - i), 8 digits + " desc", right-padded to 40.
std::string ord2_key(int i, int N) {
    char num[16];
    std::snprintf(num, sizeof(num), "%08d", N - i);
    std::string k = std::string("ART-") + num + " desc";
    k.resize(40, ' ');
    return k;
}

// ORD1 key for record i: ascending with recno, 8 digits.
std::string ord1_key(int i) {
    char num[16];
    std::snprintf(num, sizeof(num), "%08d", i);
    return std::string(num, 8);
}

} // namespace

TEST_CASE("CDX 2nd-tag(A): single tag, 40-byte descending insert, multi-level") {
    auto p = fs::temp_directory_path() / "openads_cdx_2nd_A.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    constexpr int N = 6000;
    {
        auto created = CdxIndex::create(p.string(), "ORD2", "CNOMBREART", 40,
                                         false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        for (int i = 1; i <= N; ++i) {
            REQUIRE(ix.insert(static_cast<std::uint32_t>(i),
                              ord2_key(i, N)).has_value());
        }
        REQUIRE(ix.flush().has_value());
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        for (int i = 1; i <= N; ++i) {
            auto seek = ix.seek_key(ord2_key(i, N), false);
            INFO("A seek i=" << i);
            REQUIRE(seek.has_value());
            CHECK(seek.value().hit == SeekHit::Exact);
            CHECK(seek.value().recno == static_cast<std::uint32_t>(i));
        }
    }
    fs::remove(p, ec);
}

TEST_CASE("CDX 2nd-tag(B): ORD1 + add_tag(ORD2), no sibling rebuild") {
    auto p = fs::temp_directory_path() / "openads_cdx_2nd_B.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    constexpr int N = 6000;

    // Build ORD1 (ascending 8-byte).
    {
        auto c1 = CdxIndex::create(p.string(), "ORD1", "CCODIGOART", 8,
                                   false, false);
        REQUIRE(c1.has_value());
        CdxIndex ix1 = std::move(c1).value();
        for (int i = 1; i <= N; ++i)
            REQUIRE(ix1.insert(static_cast<std::uint32_t>(i),
                               ord1_key(i)).has_value());
        REQUIRE(ix1.flush().has_value());
    }
    // add_tag ORD2 (40-byte, descending insert).
    {
        auto c2 = CdxIndex::add_tag(p.string(), "ORD2", "CNOMBREART", 40,
                                    false, false);
        REQUIRE(c2.has_value());
        CdxIndex ix2 = std::move(c2).value();
        for (int i = 1; i <= N; ++i)
            REQUIRE(ix2.insert(static_cast<std::uint32_t>(i),
                               ord2_key(i, N)).has_value());
        REQUIRE(ix2.flush().has_value());
    }
    // Reopen ORD2 and verify every key seeks to its recno + ordered walk.
    {
        CdxIndex ix;
        REQUIRE(ix.open_named(p.string(), IndexOpenMode::Shared, "ORD2")
                    .has_value());
        for (int i = 1; i <= N; ++i) {
            auto seek = ix.seek_key(ord2_key(i, N), false);
            INFO("B seek i=" << i);
            REQUIRE(seek.has_value());
            CHECK(seek.value().hit == SeekHit::Exact);
            CHECK(seek.value().recno == static_cast<std::uint32_t>(i));
        }
        // ordered walk: every recno in [1,N] visited, keys non-decreasing.
        auto s = ix.seek_first();
        REQUIRE(s.has_value());
        int seen = 0;
        std::string prev;
        while (s.value().positioned) {
            ++seen;
            std::uint32_t r = s.value().recno;
            INFO("B walk seen=" << seen << " recno=" << r);
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

TEST_CASE("CDX 2nd-tag(C): B + ABI sibling refresh rebuilds ORD1") {
    auto p = fs::temp_directory_path() / "openads_cdx_2nd_C.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    constexpr int N = 6000;

    {
        auto c1 = CdxIndex::create(p.string(), "ORD1", "CCODIGOART", 8,
                                   false, false);
        REQUIRE(c1.has_value());
        CdxIndex ix1 = std::move(c1).value();
        for (int i = 1; i <= N; ++i)
            REQUIRE(ix1.insert(static_cast<std::uint32_t>(i),
                               ord1_key(i)).has_value());
        REQUIRE(ix1.flush().has_value());
    }
    {
        auto c2 = CdxIndex::add_tag(p.string(), "ORD2", "CNOMBREART", 40,
                                    false, false);
        REQUIRE(c2.has_value());
        CdxIndex ix2 = std::move(c2).value();
        for (int i = 1; i <= N; ++i)
            REQUIRE(ix2.insert(static_cast<std::uint32_t>(i),
                               ord2_key(i, N)).has_value());
        REQUIRE(ix2.flush().has_value());
    }
    // ABI sibling refresh: rebuild ORD1 (clear_data + per-record insert).
    {
        CdxIndex sib;
        REQUIRE(sib.open_named(p.string(), IndexOpenMode::Shared, "ORD1")
                    .has_value());
        REQUIRE(sib.clear_data().has_value());
        for (int i = 1; i <= N; ++i)
            REQUIRE(sib.insert(static_cast<std::uint32_t>(i),
                               ord1_key(i)).has_value());
        REQUIRE(sib.flush().has_value());
    }
    // ORD2 must still be intact.
    {
        CdxIndex ix;
        REQUIRE(ix.open_named(p.string(), IndexOpenMode::Shared, "ORD2")
                    .has_value());
        for (int i = 1; i <= N; ++i) {
            auto seek = ix.seek_key(ord2_key(i, N), false);
            INFO("C seek i=" << i);
            REQUIRE(seek.has_value());
            CHECK(seek.value().hit == SeekHit::Exact);
            CHECK(seek.value().recno == static_cast<std::uint32_t>(i));
        }
    }
    fs::remove(p, ec);
}
