#include "doctest.h"
#include "drivers/dbf_common.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

using openads::drivers::DbfField;
using openads::drivers::DbfFieldType;
using openads::drivers::parse_dbf_fields;

namespace {

// Build an N-field descriptor block of 32 bytes per field plus the
// trailing 0x0D terminator. Each descriptor:
//   bytes 0-10 : ASCII name padded with NUL
//   byte 11    : ASCII type letter
//   bytes 12-15: field data offset (uint32 LE) — VFP only, ignored
//   byte 16    : field length
//   byte 17    : decimal places
//   bytes 18-31: reserved
std::vector<std::uint8_t> build_descriptors(
        const std::vector<std::tuple<const char*, char, std::uint8_t,
                                     std::uint8_t>>& fields) {
    std::vector<std::uint8_t> out(fields.size() * 32 + 1, 0);
    for (std::size_t i = 0; i < fields.size(); ++i) {
        std::uint8_t* slot = out.data() + i * 32;
        const auto& [name, type, len, dec] = fields[i];
        std::strncpy(reinterpret_cast<char*>(slot), name, 11);
        slot[11] = static_cast<std::uint8_t>(type);
        slot[16] = len;
        slot[17] = dec;
    }
    out.back() = 0x0D;
    return out;
}

} // namespace

TEST_CASE("Field parser reads name, type, length, decimals") {
    auto buf = build_descriptors({
        {"NAME",      'C', 20, 0},
        {"BALANCE",   'N', 12, 2},
        {"BORN",      'D',  8, 0},
        {"ACTIVE",    'L',  1, 0},
    });
    auto parsed = parse_dbf_fields(buf.data(), buf.size());
    REQUIRE(parsed.has_value());
    auto fields = parsed.value();
    REQUIRE(fields.size() == 4);

    CHECK(fields[0].name == "NAME");
    CHECK(fields[0].type == DbfFieldType::Character);
    CHECK(fields[0].length == 20);
    CHECK(fields[0].decimals == 0);

    CHECK(fields[1].name == "BALANCE");
    CHECK(fields[1].type == DbfFieldType::Numeric);
    CHECK(fields[1].length == 12);
    CHECK(fields[1].decimals == 2);

    CHECK(fields[2].type == DbfFieldType::Date);
    CHECK(fields[2].length == 8);

    CHECK(fields[3].type == DbfFieldType::Logical);
    CHECK(fields[3].length == 1);
}

TEST_CASE("Field parser stops at the 0x0D terminator") {
    auto buf = build_descriptors({
        {"ONLY", 'C', 5, 0},
    });
    auto parsed = parse_dbf_fields(buf.data(), buf.size());
    REQUIRE(parsed.has_value());
    CHECK(parsed.value().size() == 1);
}

TEST_CASE("Field parser flags unknown types as Unknown but still records them") {
    auto buf = build_descriptors({
        {"WEIRD", 'Q', 3, 0},
    });
    auto parsed = parse_dbf_fields(buf.data(), buf.size());
    REQUIRE(parsed.has_value());
    REQUIRE(parsed.value().size() == 1);
    CHECK(parsed.value()[0].type == DbfFieldType::Unknown);
    CHECK(parsed.value()[0].raw_type == 'Q');
}

TEST_CASE("Field parser computes record offset for each field") {
    auto buf = build_descriptors({
        {"A", 'C', 5, 0},
        {"B", 'C', 7, 0},
        {"C", 'N', 3, 0},
    });
    auto parsed = parse_dbf_fields(buf.data(), buf.size());
    REQUIRE(parsed.has_value());
    auto fields = parsed.value();
    CHECK(fields[0].record_offset == 1);          // 1 = past deletion byte
    CHECK(fields[1].record_offset == 1 + 5);
    CHECK(fields[2].record_offset == 1 + 5 + 7);
}
