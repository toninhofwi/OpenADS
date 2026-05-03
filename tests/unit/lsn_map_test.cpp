#include "doctest.h"
#include "engine/lsn_map.h"

#include <cstdint>
#include <filesystem>

namespace fs = std::filesystem;
using openads::engine::LsnMap;

TEST_CASE("LsnMap: put / get monotonically advances stored LSN") {
    auto p = fs::temp_directory_path() / "openads_m55_lsnmap_basic.lsnmap";
    fs::remove(p);

    LsnMap m;
    REQUIRE(m.open(p.string()).has_value());
    CHECK(m.size() == 0);
    CHECK(m.get("data.dbf", 1) == 0u);

    m.put("data.dbf", 1, 100);
    m.put("data.dbf", 2, 200);
    CHECK(m.get("data.dbf", 1) == 100u);
    CHECK(m.get("data.dbf", 2) == 200u);

    // put with a smaller LSN must not regress.
    m.put("data.dbf", 1, 50);
    CHECK(m.get("data.dbf", 1) == 100u);
    // put with a larger LSN advances.
    m.put("data.dbf", 1, 150);
    CHECK(m.get("data.dbf", 1) == 150u);

    fs::remove(p);
}

TEST_CASE("LsnMap: flush + reopen round-trips entries") {
    auto p = fs::temp_directory_path() / "openads_m55_lsnmap_persist.lsnmap";
    fs::remove(p);

    {
        LsnMap m;
        REQUIRE(m.open(p.string()).has_value());
        m.put("a/b.dbf", 7, 1234567890ULL);
        m.put("a/b.dbf", 8, 9876543210ULL);
        m.put("c.ntx",   1, 42);
        REQUIRE(m.flush().has_value());
    }
    {
        LsnMap m;
        REQUIRE(m.open(p.string()).has_value());
        CHECK(m.size() == 3);
        CHECK(m.get("a/b.dbf", 7) == 1234567890ULL);
        CHECK(m.get("a/b.dbf", 8) == 9876543210ULL);
        CHECK(m.get("c.ntx",   1) == 42u);
        // Missing entries still report zero.
        CHECK(m.get("c.ntx",   2) == 0u);
    }
    fs::remove(p);
    fs::remove(fs::path(p.string() + ".tmp"));
}

TEST_CASE("LsnMap: clear empties in-memory state") {
    auto p = fs::temp_directory_path() / "openads_m55_lsnmap_clear.lsnmap";
    fs::remove(p);
    LsnMap m;
    REQUIRE(m.open(p.string()).has_value());
    m.put("x", 1, 99);
    CHECK(m.size() == 1);
    m.clear();
    CHECK(m.size() == 0);
    CHECK(m.get("x", 1) == 0u);
    fs::remove(p);
}
