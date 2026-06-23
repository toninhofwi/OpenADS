#include "doctest.h"
#include "drivers/cdx/cdx_index.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using openads::drivers::IndexOpenMode;
using openads::drivers::SeekHit;
using openads::drivers::cdx::CdxIndex;

TEST_CASE("CdxIndex create + insert + reopen walks keys in compact-leaf order") {
    auto p = fs::temp_directory_path() / "openads_m35_cdx_basic.cdx";
    fs::remove(p);
    {
        auto created = CdxIndex::create(p.string(), "T1", "TAG", 4, false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        REQUIRE(ix.insert(2, "AAAA").has_value());
        REQUIRE(ix.insert(3, "BBBB").has_value());
        REQUIRE(ix.insert(1, "CCCC").has_value());
        REQUIRE(ix.flush().has_value());
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        CHECK(ix.expression() == "TAG");
        CHECK(ix.name()       == "T1");

        auto a = ix.seek_first();
        REQUIRE(a.has_value());
        CHECK(a.value().recno == 2);
        CHECK(ix.current_key() == "AAAA");

        REQUIRE(ix.next().has_value());
        CHECK(ix.current_key() == "BBBB");
        REQUIRE(ix.next().has_value());
        CHECK(ix.current_key() == "CCCC");
        auto end = ix.next();
        REQUIRE(end.has_value());
        CHECK_FALSE(end.value().positioned);
    }
    fs::remove(p);
}

TEST_CASE("CdxIndex seek_key locates an exact match") {
    auto p = fs::temp_directory_path() / "openads_m35_cdx_seek.cdx";
    fs::remove(p);
    {
        auto created = CdxIndex::create(p.string(), "T1", "TAG", 4, false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        REQUIRE(ix.insert(1, "AAAA").has_value());
        REQUIRE(ix.insert(2, "BBBB").has_value());
        REQUIRE(ix.insert(3, "CCCC").has_value());
        REQUIRE(ix.flush().has_value());
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        auto r = ix.seek_key("BBBB", false);
        REQUIRE(r.has_value());
        CHECK(r.value().hit == SeekHit::Exact);
        CHECK(r.value().recno == 2);
        CHECK(ix.current_key() == "BBBB");
    }
    fs::remove(p);
}

TEST_CASE("CdxIndex seek_last + prev walks backward") {
    auto p = fs::temp_directory_path() / "openads_m35_cdx_back.cdx";
    fs::remove(p);
    {
        auto created = CdxIndex::create(p.string(), "T1", "TAG", 4, false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        REQUIRE(ix.insert(1, "AAAA").has_value());
        REQUIRE(ix.insert(2, "BBBB").has_value());
        REQUIRE(ix.insert(3, "CCCC").has_value());
        REQUIRE(ix.flush().has_value());
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        auto r = ix.seek_last();
        REQUIRE(r.has_value());
        CHECK(ix.current_key() == "CCCC");
        REQUIRE(ix.prev().has_value());
        CHECK(ix.current_key() == "BBBB");
        REQUIRE(ix.prev().has_value());
        CHECK(ix.current_key() == "AAAA");
        auto begin = ix.prev();
        REQUIRE(begin.has_value());
        CHECK_FALSE(begin.value().positioned);
    }
    fs::remove(p);
}

TEST_CASE("CdxIndex erase removes a key") {
    auto p = fs::temp_directory_path() / "openads_m35_cdx_erase.cdx";
    fs::remove(p);
    {
        auto created = CdxIndex::create(p.string(), "T1", "TAG", 4, false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        REQUIRE(ix.insert(1, "AAAA").has_value());
        REQUIRE(ix.insert(2, "BBBB").has_value());
        REQUIRE(ix.insert(3, "CCCC").has_value());
        REQUIRE(ix.erase(2, "BBBB").has_value());
        REQUIRE(ix.flush().has_value());
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        REQUIRE(ix.seek_first().has_value());
        CHECK(ix.current_key() == "AAAA");
        REQUIRE(ix.next().has_value());
        CHECK(ix.current_key() == "CCCC");
        auto end = ix.next();
        REQUIRE(end.has_value());
        CHECK_FALSE(end.value().positioned);
    }
    fs::remove(p);
}

TEST_CASE("CdxIndex compound layout: file header + struct-tag leaf + sub-tag header") {
    auto p = fs::temp_directory_path() / "openads_m39_cdx_compound.cdx";
    fs::remove(p);
    {
        auto created = CdxIndex::create(p.string(), "MYTAG", "FIELD", 8, false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        REQUIRE(ix.insert(1, "alpha").has_value());
        REQUIRE(ix.insert(2, "beta").has_value());
        REQUIRE(ix.flush().has_value());
    }

    // Direct on-disk inspection: file header at 0, struct-tag root leaf at
    // offset 1024 mapping the tag name to 1536, sub-tag header at 1536.
    {
        std::ifstream f(p, std::ios::binary);
        REQUIRE(f.is_open());
        std::uint8_t hdr[1024]{};
        f.read(reinterpret_cast<char*>(hdr), 1024);
        auto rd32 = [](const std::uint8_t* x) {
            return  static_cast<std::uint32_t>(x[0])        |
                   (static_cast<std::uint32_t>(x[1]) <<  8) |
                   (static_cast<std::uint32_t>(x[2]) << 16) |
                   (static_cast<std::uint32_t>(x[3]) << 24);
        };
        auto rd16 = [](const std::uint8_t* x) {
            return  static_cast<std::uint16_t>(x[0]) |
                   (static_cast<std::uint16_t>(x[1]) << 8);
        };
        CHECK(rd32(hdr + 0) == 1024u);                 // struct-tag root
        CHECK(rd16(hdr + 12) == 10u);                  // struct-tag key_size = 10
        CHECK(static_cast<int>(hdr[14] & 0x40) != 0);  // CDX_TYPE_COMPOUND bit
        // Native-interop conformance: the structure ("tag of tags") header
        // MUST carry CDX_TYPE_STRUCTURE (0x80); without it a native reader
        // tries to compile the tag-name "key expression" and reports the
        // index corrupt.
        CHECK(static_cast<int>(hdr[14] & 0x80) != 0);  // CDX_TYPE_STRUCTURE

        f.seekg(1024);
        std::uint8_t leaf[512]{};
        f.read(reinterpret_cast<char*>(leaf), 512);
        // Root-that-is-a-leaf must be ROOT|LEAF (0x03); a native FoxPro /
        // Harbour reader rejects a root node missing the ROOT bit.
        CHECK(rd16(leaf + 0) == 3u);                   // CDX_NODE_ROOT | CDX_NODE_LEAF
        CHECK(rd16(leaf + 2) == 1u);                   // one entry

        f.seekg(1536);
        std::uint8_t sub[1024]{};
        f.read(reinterpret_cast<char*>(sub), 1024);
        CHECK(rd16(sub + 12) == 8u);                   // sub-tag key_size
    }

    // Reopen via the public API and confirm the sub-tag walks correctly.
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        CHECK(ix.name() == "MYTAG");
        CHECK(ix.expression() == "FIELD");
        CHECK(ix.key_length() == 8);
        REQUIRE(ix.seek_first().has_value());
        CHECK(ix.current_key() == "alpha   ");     // padded to 8
        REQUIRE(ix.next().has_value());
        CHECK(ix.current_key() == "beta    ");
    }
    fs::remove(p);
}

TEST_CASE("CdxIndex multi-tag: add_tag + open_named round-trip independent sub-trees") {
    auto p = fs::temp_directory_path() / "openads_m310_cdx_multitag.cdx";
    fs::remove(p);

    // 1) Create the CDX with a first tag and insert into it.
    {
        auto created = CdxIndex::create(p.string(), "PRIMARY", "PK", 4, false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        REQUIRE(ix.insert(1, "AAAA").has_value());
        REQUIRE(ix.insert(2, "BBBB").has_value());
        REQUIRE(ix.flush().has_value());
    }

    // 2) Add a second tag with a different key length and key expression,
    //    then insert independent rows into it.
    {
        auto added = CdxIndex::add_tag(p.string(), "SECOND", "EXPR", 6, true, false);
        REQUIRE(added.has_value());
        CdxIndex ix = std::move(added).value();
        REQUIRE(ix.insert(10, "alpha1").has_value());
        REQUIRE(ix.insert(20, "beta22").has_value());
        REQUIRE(ix.flush().has_value());
    }

    // 3) list_tags reports both.
    {
        auto tags = CdxIndex::list_tags(p.string());
        REQUIRE(tags.has_value());
        auto& tv = tags.value();
        REQUIRE(tv.size() == 2);
        // Sorted by tag name.
        CHECK(tv[0] == "PRIMARY");
        CHECK(tv[1] == "SECOND");
    }

    // 4) open_named picks each sub-tag independently and walks its keys.
    {
        CdxIndex ix;
        REQUIRE(ix.open_named(p.string(), IndexOpenMode::Shared, "PRIMARY")
                .has_value());
        CHECK(ix.name() == "PRIMARY");
        CHECK(ix.expression() == "PK");
        CHECK(ix.key_length() == 4);
        REQUIRE(ix.seek_first().has_value());
        CHECK(ix.current_key() == "AAAA");
        REQUIRE(ix.next().has_value());
        CHECK(ix.current_key() == "BBBB");
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open_named(p.string(), IndexOpenMode::Shared, "SECOND")
                .has_value());
        CHECK(ix.name() == "SECOND");
        CHECK(ix.expression() == "EXPR");
        CHECK(ix.key_length() == 6);
        CHECK(ix.unique() == true);
        REQUIRE(ix.seek_first().has_value());
        CHECK(ix.current_key() == "alpha1");
        REQUIRE(ix.next().has_value());
        CHECK(ix.current_key() == "beta22");
    }

    // 5) open_named with a non-existent tag returns 5044.
    {
        CdxIndex ix;
        auto r = ix.open_named(p.string(), IndexOpenMode::Shared, "NOPE");
        CHECK_FALSE(r.has_value());
        CHECK(r.error().code == 5044);
    }

    // 6) add_tag rejects duplicate tag names.
    {
        auto r = CdxIndex::add_tag(p.string(), "PRIMARY", "X", 4, false, false);
        CHECK_FALSE(r.has_value());
        CHECK(r.error().code == 5044);
    }

    fs::remove(p);
}

TEST_CASE("CdxIndex unique tag rejects duplicates") {
    auto p = fs::temp_directory_path() / "openads_m35_cdx_unique.cdx";
    fs::remove(p);
    {
        auto created = CdxIndex::create(p.string(), "T1", "TAG", 4, true, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        REQUIRE(ix.insert(1, "AAAA").has_value());
        auto r = ix.insert(2, "AAAA");
        CHECK_FALSE(r.has_value());
        CHECK(r.error().code == 5044);
    }
    fs::remove(p);
}
