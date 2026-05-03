#include "doctest.h"
#include "drivers/fpt/fpt_memo.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using openads::drivers::MemoOpenMode;
using openads::drivers::fpt::FptMemo;

TEST_CASE("FptMemo round-trips a short memo through create+write+reopen (block=64)") {
    auto p = fs::temp_directory_path() / "openads_m4_fpt_short.fpt";
    fs::remove(p);
    std::uint32_t block = 0;
    {
        auto created = FptMemo::create(p.string(), 64);
        REQUIRE(created.has_value());
        FptMemo m = std::move(created).value();
        auto w = m.write("hello fpt");
        REQUIRE(w.has_value());
        block = w.value();
        REQUIRE(m.flush().has_value());
    }
    {
        FptMemo m;
        REQUIRE(m.open(p.string(), MemoOpenMode::ReadOnly).has_value());
        CHECK(m.block_size() == 64);
        auto r = m.read(block);
        REQUIRE(r.has_value());
        CHECK(r.value() == "hello fpt");
    }
    fs::remove(p);
}

TEST_CASE("FptMemo round-trips a multi-block memo (block=64)") {
    auto p = fs::temp_directory_path() / "openads_m4_fpt_multi.fpt";
    fs::remove(p);
    std::string payload(500, 'y');
    payload.append("END");
    std::uint32_t block = 0;
    {
        auto created = FptMemo::create(p.string(), 64);
        REQUIRE(created.has_value());
        FptMemo m = std::move(created).value();
        auto w = m.write(payload);
        REQUIRE(w.has_value());
        block = w.value();
        REQUIRE(m.flush().has_value());
    }
    {
        FptMemo m;
        REQUIRE(m.open(p.string(), MemoOpenMode::ReadOnly).has_value());
        auto r = m.read(block);
        REQUIRE(r.has_value());
        CHECK(r.value() == payload);
    }
    fs::remove(p);
}

TEST_CASE("FptMemo two writes with block=512 (FoxPro 2.x default)") {
    auto p = fs::temp_directory_path() / "openads_m4_fpt_512.fpt";
    fs::remove(p);
    std::uint32_t b1 = 0, b2 = 0;
    {
        auto created = FptMemo::create(p.string(), 512);
        REQUIRE(created.has_value());
        FptMemo m = std::move(created).value();
        auto w1 = m.write("first");
        REQUIRE(w1.has_value()); b1 = w1.value();
        auto w2 = m.write("second");
        REQUIRE(w2.has_value()); b2 = w2.value();
        REQUIRE(m.flush().has_value());
    }
    CHECK(b1 != b2);
    CHECK(b2 > b1);
    {
        FptMemo m;
        REQUIRE(m.open(p.string(), MemoOpenMode::ReadOnly).has_value());
        CHECK(m.block_size() == 512);
        auto r1 = m.read(b1);
        REQUIRE(r1.has_value());
        CHECK(r1.value() == "first");
        auto r2 = m.read(b2);
        REQUIRE(r2.has_value());
        CHECK(r2.value() == "second");
    }
    fs::remove(p);
}

TEST_CASE("FptMemo read of block 0 returns empty string") {
    auto p = fs::temp_directory_path() / "openads_m4_fpt_zero.fpt";
    fs::remove(p);
    {
        auto created = FptMemo::create(p.string(), 64);
        REQUIRE(created.has_value());
        FptMemo m = std::move(created).value();
        auto r = m.read(0);
        REQUIRE(r.has_value());
        CHECK(r.value().empty());
    }
    fs::remove(p);
}
