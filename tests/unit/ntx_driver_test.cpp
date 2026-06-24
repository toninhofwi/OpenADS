#include "doctest.h"
#include "drivers/ntx/ntx_driver.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using openads::drivers::DriverOpenMode;
using openads::drivers::ntx::NtxDriver;

namespace {

fs::path make_tiny_dbf(const char* tag) {
    auto p = fs::temp_directory_path() /
             (std::string("openads_ntx_drv_") + tag + ".dbf");
    std::error_code ec;
    fs::remove(p, ec);
    // Minimal DBF: 1 field (ID C 4), 2 records.
    std::vector<std::uint8_t> f;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        f.insert(f.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[4] = 2; hdr[5] = 0; hdr[6] = 0; hdr[7] = 0;
    std::uint16_t hl = 64; std::uint16_t rl = 5;
    hdr[8]  = hl & 0xFF; hdr[9] = (hl >> 8) & 0xFF;
    hdr[10] = rl & 0xFF; hdr[11] = (rl >> 8) & 0xFF;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "ID", 11);
    fd[11] = 'C'; fd[16] = 4;
    push(fd.data(), fd.size());
    f.push_back(0x0D);
    // Record 1
    f.push_back(' ');
    f.insert(f.end(), {'A', 'B', 'C', 'D'});
    // Record 2
    f.push_back(' ');
    f.insert(f.end(), {'E', 'F', 'G', 'H'});
    f.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(f.data()),
        static_cast<std::streamsize>(f.size()));
    return p;
}

void safe_remove(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec);
}

} // namespace

TEST_CASE("NtxDriver: open read-only and read metadata") {
    auto dbf = make_tiny_dbf("meta");
    {
        NtxDriver drv;
        REQUIRE(drv.open(dbf.string(), DriverOpenMode::ReadOnly));
        CHECK(drv.record_count() == 2);
        CHECK(drv.record_length() == 5);
        CHECK(drv.header_length() == 64);
        CHECK(drv.fields().size() == 1);
        CHECK(drv.fields()[0].name == "ID");
    }
    safe_remove(dbf);
}

TEST_CASE("NtxDriver: read_record_raw returns data of correct size") {
    auto dbf = make_tiny_dbf("read");
    {
        NtxDriver drv;
        REQUIRE(drv.open(dbf.string(), DriverOpenMode::ReadOnly));
        auto rec = drv.read_record_raw(1);
        REQUIRE(rec);
        CHECK(rec.value().size() == 5);
    }
    safe_remove(dbf);
}

TEST_CASE("NtxDriver: read_record_raw out-of-range returns error") {
    auto dbf = make_tiny_dbf("oor");
    {
        NtxDriver drv;
        REQUIRE(drv.open(dbf.string(), DriverOpenMode::ReadOnly));
        auto rec = drv.read_record_raw(99);
        CHECK_FALSE(rec);
    }
    safe_remove(dbf);
}

TEST_CASE("NtxDriver: append_record_raw grows record count") {
    auto dbf = make_tiny_dbf("app");
    {
        NtxDriver drv;
        REQUIRE(drv.open(dbf.string(), DriverOpenMode::Exclusive));
        CHECK(drv.record_count() == 2);
        std::array<std::uint8_t, 5> newrec = {' ', 'X', 'Y', 'Z', 'W'};
        auto res = drv.append_record_raw(newrec.data(), newrec.size());
        REQUIRE(res);
        CHECK(res.value() == 3);
        CHECK(drv.record_count() == 3);
        auto readback = drv.read_record_raw(3);
        REQUIRE(readback);
        CHECK(readback.value().size() == 5);
    }
    safe_remove(dbf);
}

TEST_CASE("NtxDriver: write_record_raw does not fail") {
    auto dbf = make_tiny_dbf("wrt");
    {
        NtxDriver drv;
        REQUIRE(drv.open(dbf.string(), DriverOpenMode::Exclusive));
        std::array<std::uint8_t, 5> modified = {' ', 'Z', 'Z', 'Z', 'Z'};
        REQUIRE(drv.write_record_raw(1, modified.data(), modified.size()));
    }
    safe_remove(dbf);
}

TEST_CASE("NtxDriver: zap clears all records") {
    auto dbf = make_tiny_dbf("zap");
    {
        NtxDriver drv;
        REQUIRE(drv.open(dbf.string(), DriverOpenMode::Exclusive));
        CHECK(drv.record_count() == 2);
        REQUIRE(drv.zap());
        CHECK(drv.record_count() == 0);
    }
    safe_remove(dbf);
}

TEST_CASE("NtxDriver: flush does not fail") {
    auto dbf = make_tiny_dbf("fls");
    {
        NtxDriver drv;
        REQUIRE(drv.open(dbf.string(), DriverOpenMode::Exclusive));
        REQUIRE(drv.flush());
    }
    safe_remove(dbf);
}

TEST_CASE("NtxDriver: file() returns open handle") {
    auto dbf = make_tiny_dbf("fil");
    {
        NtxDriver drv;
        REQUIRE(drv.open(dbf.string(), DriverOpenMode::ReadOnly));
        CHECK(drv.file().is_open());
    }
    safe_remove(dbf);
}

TEST_CASE("NtxDriver: open non-existent file returns error") {
    NtxDriver drv;
    CHECK_FALSE(drv.open("/nonexistent/path/dbf.ntx", DriverOpenMode::ReadOnly));
}
