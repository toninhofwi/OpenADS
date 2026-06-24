#include "doctest.h"
#include "engine/codepage.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using openads::engine::cp437_to_utf8;
using openads::engine::utf8_to_cp437;

TEST_CASE("codepage: ASCII passthrough in cp437_to_utf8") {
    std::uint8_t in[] = {'H', 'e', 'l', 'l', 'o'};
    auto out = cp437_to_utf8(in, sizeof(in));
    CHECK(out == "Hello");
}

TEST_CASE("codepage: empty input returns empty string") {
    auto out = cp437_to_utf8(nullptr, 0);
    CHECK(out.empty());
}

TEST_CASE("codepage: CP437 0x82 (e-acute) round-trip") {
    std::uint8_t in[] = {0x82};
    auto utf8 = cp437_to_utf8(in, 1);
    // U+00E9 = 0xC3 0xA9
    REQUIRE(utf8.size() == 2);
    CHECK(static_cast<std::uint8_t>(utf8[0]) == 0xC3);
    CHECK(static_cast<std::uint8_t>(utf8[1]) == 0xA9);
    auto back = utf8_to_cp437(utf8.data(), utf8.size());
    REQUIRE(back.size() == 1);
    CHECK(static_cast<std::uint8_t>(back[0]) == 0x82);
}

TEST_CASE("codepage: CP437 0xA4 (n-tilde) round-trip") {
    std::uint8_t in[] = {0xA4};
    auto utf8 = cp437_to_utf8(in, 1);
    // 0xA4 -> U+00F1 = 0xC3 0xB1
    REQUIRE(utf8.size() == 2);
    CHECK(static_cast<std::uint8_t>(utf8[0]) == 0xC3);
    CHECK(static_cast<std::uint8_t>(utf8[1]) == 0xB1);
    auto back = utf8_to_cp437(utf8.data(), utf8.size());
    REQUIRE(back.size() == 1);
    CHECK(static_cast<std::uint8_t>(back[0]) == 0xA4);
}

TEST_CASE("codepage: CP437 box-drawing characters (0xB0..0xDF)") {
    for (std::uint8_t b = 0xB0; b <= 0xDF; ++b) {
        auto utf8 = cp437_to_utf8(&b, 1);
        CHECK(!utf8.empty());
        auto back = utf8_to_cp437(utf8.data(), utf8.size());
        REQUIRE(back.size() == 1);
        CHECK(static_cast<std::uint8_t>(back[0]) == b);
    }
}

TEST_CASE("codepage: CP437 Greek letters (0xE0..0xE9)") {
    std::uint8_t in[] = {0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9};
    auto utf8 = cp437_to_utf8(in, sizeof(in));
    auto back = utf8_to_cp437(utf8.data(), utf8.size());
    REQUIRE(back.size() == sizeof(in));
    for (std::size_t i = 0; i < sizeof(in); ++i) {
        CHECK(static_cast<std::uint8_t>(back[i]) == in[i]);
    }
}

TEST_CASE("codepage: full high-byte range round-trip") {
    std::vector<std::uint8_t> in(128);
    for (std::size_t i = 0; i < 128; ++i) {
        in[i] = static_cast<std::uint8_t>(0x80 + i);
    }
    auto utf8 = cp437_to_utf8(in.data(), in.size());
    auto back = utf8_to_cp437(utf8.data(), utf8.size());
    REQUIRE(back.size() == in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        CHECK(static_cast<std::uint8_t>(back[i]) == in[i]);
    }
}

TEST_CASE("codepage: utf8_to_cp437 replaces unmapped codepoints with '?'") {
    // U+2603 (SNOWMAN) has no CP437 mapping
    const char snowman[] = "\xe2\x98\x83";
    auto out = utf8_to_cp437(snowman, sizeof(snowman) - 1);
    REQUIRE(out.size() == 1);
    CHECK(out[0] == '?');
}

TEST_CASE("codepage: utf8_to_cp437 handles incomplete trailing bytes") {
    // 2-byte sequence missing second byte
    const char incomplete[] = "\xC3";
    auto out = utf8_to_cp437(incomplete, 1);
    REQUIRE(out.size() == 1);
    CHECK(out[0] == '?');
}

TEST_CASE("codepage: mixed ASCII and high-byte in cp437_to_utf8") {
    // 0x82 = é (U+00E9 -> 2 UTF-8 bytes), 0x9C = £ (U+00A3 -> 2 UTF-8 bytes)
    std::uint8_t in[] = {'A', 0x82, 'B', 0x9C, 'C'};
    auto utf8 = cp437_to_utf8(in, sizeof(in));
    // 'A'(1) + é(2) + 'B'(1) + £(2) + 'C'(1) = 7
    CHECK(utf8.size() == 7);
    CHECK(utf8[0] == 'A');
    // 0x82 -> U+00E9 -> 0xC3 0xA9
    CHECK(static_cast<std::uint8_t>(utf8[1]) == 0xC3);
    CHECK(static_cast<std::uint8_t>(utf8[2]) == 0xA9);
    CHECK(utf8[3] == 'B');
    // 0x9C -> U+00A3 -> 0xC2 0xA3
    CHECK(static_cast<std::uint8_t>(utf8[4]) == 0xC2);
    CHECK(static_cast<std::uint8_t>(utf8[5]) == 0xA3);
    CHECK(utf8[6] == 'C');
    auto back = utf8_to_cp437(utf8.data(), utf8.size());
    REQUIRE(back.size() == sizeof(in));
    for (std::size_t i = 0; i < sizeof(in); ++i) {
        CHECK(static_cast<std::uint8_t>(back[i]) == in[i]);
    }
}

TEST_CASE("codepage: utf8_to_cp437 with empty input") {
    auto out = utf8_to_cp437("", 0);
    CHECK(out.empty());
}

TEST_CASE("codepage: CP437 0x01 (smiley) round-trip") {
    std::uint8_t in[] = {0x01};
    auto utf8 = cp437_to_utf8(in, 1);
    CHECK(!utf8.empty());
    auto back = utf8_to_cp437(utf8.data(), utf8.size());
    REQUIRE(back.size() == 1);
    CHECK(static_cast<std::uint8_t>(back[0]) == 0x01);
}

TEST_CASE("codepage: CP437 0xFF (nbsp) round-trip") {
    std::uint8_t in[] = {0xFF};
    auto utf8 = cp437_to_utf8(in, 1);
    // U+00A0 = 0xC2 0xA0
    REQUIRE(utf8.size() == 2);
    CHECK(static_cast<std::uint8_t>(utf8[0]) == 0xC2);
    CHECK(static_cast<std::uint8_t>(utf8[1]) == 0xA0);
    auto back = utf8_to_cp437(utf8.data(), utf8.size());
    REQUIRE(back.size() == 1);
    CHECK(static_cast<std::uint8_t>(back[0]) == 0xFF);
}

TEST_CASE("codepage: CP437 0xF6 (division sign) round-trip") {
    std::uint8_t in[] = {0xF6};
    auto utf8 = cp437_to_utf8(in, 1);
    // U+00F7 = 0xC3 0xB7
    REQUIRE(utf8.size() == 2);
    CHECK(static_cast<std::uint8_t>(utf8[0]) == 0xC3);
    CHECK(static_cast<std::uint8_t>(utf8[1]) == 0xB7);
    auto back = utf8_to_cp437(utf8.data(), utf8.size());
    REQUIRE(back.size() == 1);
    CHECK(static_cast<std::uint8_t>(back[0]) == 0xF6);
}

TEST_CASE("codepage: CP437 0x03 (heart) round-trip") {
    std::uint8_t in[] = {0x03};
    auto utf8 = cp437_to_utf8(in, 1);
    CHECK(!utf8.empty());
    auto back = utf8_to_cp437(utf8.data(), utf8.size());
    REQUIRE(back.size() == 1);
    CHECK(static_cast<std::uint8_t>(back[0]) == 0x03);
}

TEST_CASE("codepage: CP437 0x0F (note) round-trip") {
    std::uint8_t in[] = {0x0F};
    auto utf8 = cp437_to_utf8(in, 1);
    CHECK(!utf8.empty());
    auto back = utf8_to_cp437(utf8.data(), utf8.size());
    REQUIRE(back.size() == 1);
    CHECK(static_cast<std::uint8_t>(back[0]) == 0x0F);
}

TEST_CASE("codepage: multi-byte UTF-8 to CP437 4-byte sequence") {
    // U+1F600 (grinning face) -> 4-byte UTF-8 -> unmapped -> '?'
    const char emoji[] = "\xF0\x9F\x98\x80";
    auto out = utf8_to_cp437(emoji, sizeof(emoji) - 1);
    REQUIRE(out.size() == 1);
    CHECK(out[0] == '?');
}
