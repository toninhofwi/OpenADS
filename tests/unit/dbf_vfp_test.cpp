#include "doctest.h"
#include "drivers/dbf_common.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

[[maybe_unused]] void write_u16_le(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>( v       & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

void write_i32_le(std::uint8_t* p, std::int32_t v) {
    auto u = static_cast<std::uint32_t>(v);
    p[0] = static_cast<std::uint8_t>( u        & 0xFFu);
    p[1] = static_cast<std::uint8_t>((u >>  8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((u >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((u >> 24) & 0xFFu);
}

void write_i64_le(std::uint8_t* p, std::int64_t v) {
    auto u = static_cast<std::uint64_t>(v);
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<std::uint8_t>((u >> (i * 8)) & 0xFFu);
    }
}

void write_f64_le(std::uint8_t* p, double v) {
    std::uint64_t u;
    std::memcpy(&u, &v, sizeof(u));
    write_i64_le(p, static_cast<std::int64_t>(u));
}

}  // namespace

TEST_CASE("M10.2 VFP Integer (I) field decodes 4-byte little-endian int32") {
    openads::drivers::DbfField f;
    f.name          = "VAL";
    f.raw_type      = 'I';
    f.type          = openads::drivers::DbfFieldType::Integer;
    f.length        = 4;
    f.record_offset = 1;

    std::vector<std::uint8_t> rec(5, ' ');
    write_i32_le(rec.data() + 1, -123456);

    auto v = openads::drivers::decode_field(f, rec.data(), rec.size());
    REQUIRE(v.has_value());
    CHECK(v.value().as_double == doctest::Approx(-123456));
    CHECK(v.value().as_string == "-123456");
}

TEST_CASE("M10.2 VFP Currency (Y) decodes 8-byte int64 / 10000.0") {
    openads::drivers::DbfField f;
    f.name          = "PRICE";
    f.raw_type      = 'Y';
    f.type          = openads::drivers::DbfFieldType::Currency;
    f.length        = 8;
    f.record_offset = 1;

    std::vector<std::uint8_t> rec(9, ' ');
    // $123.45 → 1234500 in money units.
    write_i64_le(rec.data() + 1, 1234500);

    auto v = openads::drivers::decode_field(f, rec.data(), rec.size());
    REQUIRE(v.has_value());
    CHECK(v.value().as_double == doctest::Approx(123.45));
    CHECK(v.value().as_string == "123.4500");
}

TEST_CASE("M10.2 VFP Double (B) decodes 8-byte IEEE 754 little-endian") {
    openads::drivers::DbfField f;
    f.name          = "PI";
    f.raw_type      = 'B';
    f.type          = openads::drivers::DbfFieldType::Double;
    f.length        = 8;
    f.decimals      = 4;
    f.record_offset = 1;

    std::vector<std::uint8_t> rec(9, ' ');
    write_f64_le(rec.data() + 1, 3.14159);

    auto v = openads::drivers::decode_field(f, rec.data(), rec.size());
    REQUIRE(v.has_value());
    CHECK(v.value().as_double == doctest::Approx(3.14159));
    CHECK(v.value().as_string == "3.1416");
}

TEST_CASE("M10.2 VFP Integer / Currency / Double encode round-trips") {
    using openads::drivers::DbfField;
    using openads::drivers::DbfFieldType;
    using openads::drivers::decode_field;
    using openads::drivers::encode_field_double;

    SUBCASE("Integer") {
        DbfField f; f.raw_type = 'I'; f.type = DbfFieldType::Integer;
        f.length = 4; f.record_offset = 1;
        std::vector<std::uint8_t> rec(5, ' ');
        REQUIRE(encode_field_double(f, rec.data(), rec.size(), 4242).has_value());
        auto v = decode_field(f, rec.data(), rec.size());
        REQUIRE(v.has_value());
        CHECK(v.value().as_double == doctest::Approx(4242));
    }
    SUBCASE("Currency") {
        DbfField f; f.raw_type = 'Y'; f.type = DbfFieldType::Currency;
        f.length = 8; f.record_offset = 1;
        std::vector<std::uint8_t> rec(9, ' ');
        REQUIRE(encode_field_double(f, rec.data(), rec.size(), 99.99).has_value());
        auto v = decode_field(f, rec.data(), rec.size());
        REQUIRE(v.has_value());
        CHECK(v.value().as_double == doctest::Approx(99.99));
    }
    SUBCASE("Double") {
        DbfField f; f.raw_type = 'B'; f.type = DbfFieldType::Double;
        f.length = 8; f.decimals = 6; f.record_offset = 1;
        std::vector<std::uint8_t> rec(9, ' ');
        REQUIRE(encode_field_double(f, rec.data(), rec.size(), 2.7182818).has_value());
        auto v = decode_field(f, rec.data(), rec.size());
        REQUIRE(v.has_value());
        CHECK(v.value().as_double == doctest::Approx(2.7182818));
    }
}

TEST_CASE("M11.1 VFP Varchar (V) decodes trimmed of trailing NUL pad") {
    openads::drivers::DbfField f;
    f.raw_type = 'V';
    f.type     = openads::drivers::DbfFieldType::Varchar;
    f.length = 6; f.record_offset = 1;
    std::vector<std::uint8_t> rec(7, 0);
    rec[0] = ' ';                                      // deletion byte
    rec[1] = 'A'; rec[2] = 'B'; rec[3] = 'C';
    auto v = openads::drivers::decode_field(f, rec.data(), rec.size());
    REQUIRE(v.has_value());
    CHECK(v.value().as_string == "ABC");
}

TEST_CASE("M11.1 VFP Varbinary (Q) preserves bytes up to NUL pad") {
    openads::drivers::DbfField f;
    f.raw_type = 'Q';
    f.type     = openads::drivers::DbfFieldType::Varbinary;
    f.length = 4; f.record_offset = 1;
    std::vector<std::uint8_t> rec(5, 0);
    rec[0] = ' ';
    rec[1] = 0xDE; rec[2] = 0xAD;                       // 2 bytes used
    auto v = openads::drivers::decode_field(f, rec.data(), rec.size());
    REQUIRE(v.has_value());
    REQUIRE(v.value().as_string.size() == 2);
    CHECK(static_cast<std::uint8_t>(v.value().as_string[0]) == 0xDE);
    CHECK(static_cast<std::uint8_t>(v.value().as_string[1]) == 0xAD);
}

TEST_CASE("M11.1 VFP V / Q round-trip via encode_field_string") {
    openads::drivers::DbfField f;
    f.raw_type = 'V';
    f.type     = openads::drivers::DbfFieldType::Varchar;
    f.length = 8; f.record_offset = 1;
    std::vector<std::uint8_t> rec(9, ' ');
    REQUIRE(openads::drivers::encode_field_string(
        f, rec.data(), rec.size(), "hi").has_value());
    CHECK(rec[1] == 'h');
    CHECK(rec[2] == 'i');
    CHECK(rec[3] == 0x00);
    CHECK(rec[8] == 0x00);
    auto v = openads::drivers::decode_field(f, rec.data(), rec.size());
    REQUIRE(v.has_value());
    CHECK(v.value().as_string == "hi");
}
