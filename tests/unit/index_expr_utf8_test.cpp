#include "doctest.h"
#include "engine/index_expr.h"
#include "engine/table.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path stage_dbf(const fs::path& dir, const char* leaf,
                   const std::vector<std::string>& records) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    fs::remove(p);
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = static_cast<std::uint8_t>(records.size());
    hdr[8]  = 32 + 32 + 1;
    hdr[10] = 1 + 32;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "NAME", 11);
    fd[11] = 'C'; fd[16] = 32;
    push(fd.data(), fd.size());
    file.push_back(0x0D);
    for (auto& s : records) {
        file.push_back(' ');
        std::size_t k = std::min<std::size_t>(s.size(), 32);
        for (std::size_t i = 0; i < k; ++i) {
            file.push_back(static_cast<std::uint8_t>(s[i]));
        }
        for (std::size_t i = k; i < 32; ++i) file.push_back(' ');
    }
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

}  // namespace

TEST_CASE("M9.22 UPPER over UTF-8 column promotes Latin-1 codepoints") {
    auto dir = fs::temp_directory_path() / "openads_m9_22_upper";
    std::error_code ec;
    fs::remove_all(dir, ec);
    // UTF-8 bytes for "señor" and "café".
    stage_dbf(dir, "data.dbf",
              {"se\xC3\xB1or", "caf\xC3\xA9"});

    auto tres = openads::engine::Table::open(
        (dir / "data.dbf").string(),
        openads::engine::TableType::Cdx,
        openads::engine::OpenMode::Read);
    REQUIRE(tres.has_value());
    auto t = std::move(tres).value();
    REQUIRE(t.goto_top().has_value());

    auto k1 = openads::engine::evaluate_index_expr(t, "UPPER(NAME)", 32);
    REQUIRE(k1.has_value());
    auto v1 = k1.value();
    while (!v1.empty() && v1.back() == ' ') v1.pop_back();
    // "señor" → "SEÑOR" (UTF-8 bytes for SEÑOR).
    CHECK(v1 == "SE\xC3\x91OR");

    REQUIRE(t.skip(1).has_value());
    auto k2 = openads::engine::evaluate_index_expr(t, "UPPER(NAME)", 32);
    REQUIRE(k2.has_value());
    auto v2 = k2.value();
    while (!v2.empty() && v2.back() == ' ') v2.pop_back();
    // "café" → "CAFÉ" (UTF-8 bytes for CAFÉ).
    CHECK(v2 == "CAF\xC3\x89");

    fs::remove_all(dir, ec);
}

TEST_CASE("M9.22 LOWER over UTF-8 column demotes Latin-1 codepoints") {
    auto dir = fs::temp_directory_path() / "openads_m9_22_lower";
    std::error_code ec;
    fs::remove_all(dir, ec);
    // UTF-8 for "ÑOÑO".
    stage_dbf(dir, "data.dbf", {"\xC3\x91O\xC3\x91O"});

    auto tres = openads::engine::Table::open(
        (dir / "data.dbf").string(),
        openads::engine::TableType::Cdx,
        openads::engine::OpenMode::Read);
    REQUIRE(tres.has_value());
    auto t = std::move(tres).value();
    REQUIRE(t.goto_top().has_value());

    auto k = openads::engine::evaluate_index_expr(t, "LOWER(NAME)", 32);
    REQUIRE(k.has_value());
    auto v = k.value();
    while (!v.empty() && v.back() == ' ') v.pop_back();
    // "ÑOÑO" → "ñoño".
    CHECK(v == "\xC3\xB1o\xC3\xB1o");

    fs::remove_all(dir, ec);
}

TEST_CASE("M9.22 SUBSTR walks UTF-8 codepoints, not bytes") {
    auto dir = fs::temp_directory_path() / "openads_m9_22_substr";
    std::error_code ec;
    fs::remove_all(dir, ec);
    // UTF-8 for "héllo" — 5 codepoints, 6 bytes (é is 2 bytes).
    stage_dbf(dir, "data.dbf", {"h\xC3\xA9llo"});

    auto tres = openads::engine::Table::open(
        (dir / "data.dbf").string(),
        openads::engine::TableType::Cdx,
        openads::engine::OpenMode::Read);
    REQUIRE(tres.has_value());
    auto t = std::move(tres).value();
    REQUIRE(t.goto_top().has_value());

    // SUBSTR(NAME, 1, 2) = "hé" (2 codepoints = 3 bytes).
    auto k = openads::engine::evaluate_index_expr(t, "SUBSTR(NAME,1,2)", 8);
    REQUIRE(k.has_value());
    auto v = k.value();
    while (!v.empty() && v.back() == ' ') v.pop_back();
    CHECK(v == "h\xC3\xA9");

    // SUBSTR(NAME, 2, 1) = "é" (the multibyte char by itself).
    auto k2 = openads::engine::evaluate_index_expr(t, "SUBSTR(NAME,2,1)", 8);
    REQUIRE(k2.has_value());
    auto v2 = k2.value();
    while (!v2.empty() && v2.back() == ' ') v2.pop_back();
    CHECK(v2 == "\xC3\xA9");

    fs::remove_all(dir, ec);
}

TEST_CASE("M9.22 ASCII fast path still byte-identical") {
    auto dir = fs::temp_directory_path() / "openads_m9_22_ascii";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "data.dbf", {"hello"});

    auto tres = openads::engine::Table::open(
        (dir / "data.dbf").string(),
        openads::engine::TableType::Cdx,
        openads::engine::OpenMode::Read);
    REQUIRE(tres.has_value());
    auto t = std::move(tres).value();
    REQUIRE(t.goto_top().has_value());

    auto k = openads::engine::evaluate_index_expr(t, "UPPER(NAME)", 32);
    REQUIRE(k.has_value());
    auto v = k.value();
    while (!v.empty() && v.back() == ' ') v.pop_back();
    CHECK(v == "HELLO");

    fs::remove_all(dir, ec);
}
