#include "doctest.h"
#include "session/connection.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using openads::engine::TableType;
using openads::session::Connection;

namespace {

fs::path tmp_dir(const char* tag) {
    auto p = fs::temp_directory_path() / (std::string("openads_m1_conn_") + tag);
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p);
    return p;
}

void write_minimal_dbf(const fs::path& p) {
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[4] = 1;
    hdr[8] = 32 + 32 + 1; hdr[9] = 0;
    hdr[10] = 1 + 3; hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C';
    fd[16] = 3;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    file.push_back(' '); file.push_back('a'); file.push_back('b'); file.push_back('c');
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
}

} // namespace

TEST_CASE("Connection opens against a directory") {
    auto dir = tmp_dir("open");
    {
        auto opened = Connection::open(dir.string());
        REQUIRE(opened.has_value());
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("Connection opens a CDX-typed table by relative name") {
    auto dir = tmp_dir("table");
    write_minimal_dbf(dir / "data.dbf");
    {
        auto opened = Connection::open(dir.string());
        REQUIRE(opened.has_value());
        Connection c = std::move(opened).value();

        auto th = c.open_table("data.dbf", TableType::Cdx);
        REQUIRE(th.has_value());
        auto* table = c.lookup_table(th.value());
        REQUIRE(table != nullptr);
        CHECK(table->field_count() == 1);
        CHECK(table->record_count() == 1);

        c.close_table(th.value());
        CHECK(c.lookup_table(th.value()) == nullptr);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
