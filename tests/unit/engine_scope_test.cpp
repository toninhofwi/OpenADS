#include "doctest.h"
#include "drivers/ntx/ntx_index.h"
#include "engine/table.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace fs = std::filesystem;
using openads::drivers::IndexOpenMode;
using openads::drivers::ntx::NtxIndex;
using openads::engine::OpenMode;
using openads::engine::Table;
using openads::engine::TableType;

namespace {

fs::path make_dbf(const char* tag) {
    auto p = fs::temp_directory_path() / (std::string("openads_m3_scope_") + tag + ".dbf");
    fs::remove(p);
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 4;
    hdr[8]  = 32 + 32 + 1; hdr[9] = 0;
    hdr[10] = 1 + 4;       hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 4;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    auto push = [&](const char* k){
        file.push_back(' ');
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(k) ? static_cast<std::uint8_t>(k[i]) : ' ');
    };
    push("AAAA");
    push("BBBB");
    push("CCCC");
    push("DDDD");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

fs::path build_ntx(const fs::path& dbf) {
    auto ntx = dbf; ntx.replace_extension(".ntx");
    fs::remove(ntx);
    auto created = NtxIndex::create(ntx.string(), "T1", "TAG", 4, false, false);
    REQUIRE(created.has_value());
    NtxIndex ix = std::move(created).value();
    REQUIRE(ix.insert(1, "AAAA").has_value());
    REQUIRE(ix.insert(2, "BBBB").has_value());
    REQUIRE(ix.insert(3, "CCCC").has_value());
    REQUIRE(ix.insert(4, "DDDD").has_value());
    REQUIRE(ix.flush().has_value());
    return ntx;
}

} // namespace

TEST_CASE("Table::set_scope clamps the top of the index walk") {
    auto dbf = make_dbf("top");
    auto ntx = build_ntx(dbf);
    {
        auto t = Table::open(dbf.string(), TableType::Cdx, OpenMode::Read);
        REQUIRE(t.has_value());
        Table table = std::move(t).value();
        auto idx = std::make_unique<NtxIndex>();
        REQUIRE(idx->open(ntx.string(), IndexOpenMode::ReadOnly).has_value());
        table.set_order(std::move(idx));

        REQUIRE(table.set_scope(true,  "BBBB").has_value());
        REQUIRE(table.set_scope(false, "CCCC").has_value());

        REQUIRE(table.goto_top().has_value());
        CHECK(table.recno() == 2);   // BBBB
        REQUIRE(table.skip(1).has_value());
        CHECK(table.recno() == 3);   // CCCC
        REQUIRE(table.skip(1).has_value());
        CHECK(table.eof());
    }
    fs::remove(dbf);
    fs::remove(ntx);
}

TEST_CASE("Table::clear_scopes returns to full-table walk") {
    auto dbf = make_dbf("clear");
    auto ntx = build_ntx(dbf);
    {
        auto t = Table::open(dbf.string(), TableType::Cdx, OpenMode::Read);
        REQUIRE(t.has_value());
        Table table = std::move(t).value();
        auto idx = std::make_unique<NtxIndex>();
        REQUIRE(idx->open(ntx.string(), IndexOpenMode::ReadOnly).has_value());
        table.set_order(std::move(idx));

        REQUIRE(table.set_scope(true,  "BBBB").has_value());
        REQUIRE(table.set_scope(false, "CCCC").has_value());
        REQUIRE(table.clear_scopes().has_value());

        REQUIRE(table.goto_top().has_value());
        CHECK(table.recno() == 1);
        REQUIRE(table.goto_bottom().has_value());
        CHECK(table.recno() == 4);
    }
    fs::remove(dbf);
    fs::remove(ntx);
}
