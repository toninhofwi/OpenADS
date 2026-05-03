#include "doctest.h"
#include "engine/cursor.h"
#include "engine/table.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openads::engine::Cursor;
using openads::engine::Table;
using openads::engine::TableType;

namespace {

fs::path make_fixture() {
    auto p = fs::temp_directory_path() / "openads_m1_cursor.dbf";
    fs::remove(p);
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[4] = 2;
    hdr[8] = 32 + 32 + 1; hdr[9] = 0;
    hdr[10] = 1 + 4; hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C';
    fd[16] = 4;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    auto push_rec = [&](const char* name) {
        file.push_back(' ');
        for (int i = 0; i < 4; ++i) {
            file.push_back(static_cast<std::uint8_t>(
                i < static_cast<int>(std::strlen(name)) ? name[i] : ' '));
        }
    };
    push_rec("X");
    push_rec("YZ");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("Cursor walks all live records once") {
    auto p = make_fixture();
    {
        auto t = Table::open(p.string(), TableType::Cdx);
        REQUIRE(t.has_value());
        Table table = std::move(t).value();
        Cursor c(table);

        std::vector<std::string> seen;
        while (auto row = c.next()) {
            auto v = (*row)->read_field(0);
            REQUIRE(v.has_value());
            seen.push_back(v.value().as_string);
        }
        CHECK(seen == std::vector<std::string>{"X", "YZ"});
    }
    fs::remove(p);
}
