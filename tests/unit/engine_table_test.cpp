#include "doctest.h"
#include "engine/table.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using openads::engine::Table;
using openads::engine::TableType;

namespace {

// Build a tiny DBF on disk:
//   - version 0x03
//   - 3 records, single 5-char field "NAME"
fs::path make_fixture(const char* tag) {
    auto p = fs::temp_directory_path() / (std::string("openads_m1_") + tag);
    fs::remove(p);

    std::vector<std::uint8_t> file;

    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[1]  = 124; hdr[2] = 1; hdr[3] = 31;
    hdr[4]  = 3; hdr[5] = 0; hdr[6] = 0; hdr[7] = 0;
    hdr[8]  = 32 + 32 + 1; hdr[9] = 0;
    hdr[10] = 1 + 5; hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());

    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "NAME", 11);
    fd[11] = 'C';
    fd[16] = 5;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);

    auto push_rec = [&](char d, const char* name) {
        file.push_back(static_cast<std::uint8_t>(d));
        for (int i = 0; i < 5; ++i) {
            file.push_back(static_cast<std::uint8_t>(
                i < static_cast<int>(std::strlen(name)) ? name[i] : ' '));
        }
    };
    push_rec(' ', "AAA");
    push_rec(' ', "BBBB");
    push_rec(' ', "CCCCC");

    file.push_back(0x1A);

    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("Table opens a CDX-typed DBF and reports counts") {
    auto p = make_fixture("table_counts");
    {
        auto opened = Table::open(p.string(), TableType::Cdx);
        REQUIRE(opened.has_value());
        Table t = std::move(opened).value();

        CHECK(t.field_count()  == 1);
        CHECK(t.record_count() == 3);
        CHECK(t.field_descriptor(0).name == "NAME");
    }
    fs::remove(p);
}

TEST_CASE("Table navigates top / skip / bottom and tracks BOF/EOF") {
    auto p = make_fixture("table_nav");
    {
        auto opened = Table::open(p.string(), TableType::Cdx);
        REQUIRE(opened.has_value());
        Table t = std::move(opened).value();

        REQUIRE(t.goto_top().has_value());
        CHECK(t.recno() == 1);
        CHECK_FALSE(t.eof());
        CHECK_FALSE(t.bof());

        REQUIRE(t.skip(1).has_value());
        CHECK(t.recno() == 2);

        REQUIRE(t.skip(5).has_value());
        CHECK(t.eof());

        REQUIRE(t.goto_bottom().has_value());
        CHECK(t.recno() == 3);

        REQUIRE(t.skip(-10).has_value());
        CHECK(t.bof());
    }
    fs::remove(p);
}

TEST_CASE("Table reads field values by index") {
    auto p = make_fixture("table_field");
    {
        auto opened = Table::open(p.string(), TableType::Cdx);
        REQUIRE(opened.has_value());
        Table t = std::move(opened).value();

        REQUIRE(t.goto_top().has_value());
        auto v0 = t.read_field(0);
        REQUIRE(v0.has_value());
        CHECK(v0.value().as_string == "AAA");

        REQUIRE(t.skip(1).has_value());
        auto v1 = t.read_field(0);
        REQUIRE(v1.has_value());
        CHECK(v1.value().as_string == "BBBB");

        REQUIRE(t.skip(1).has_value());
        auto v2 = t.read_field(0);
        REQUIRE(v2.has_value());
        CHECK(v2.value().as_string == "CCCCC");
    }
    fs::remove(p);
}
