#include "doctest.h"
#include "engine/data_dict.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using openads::engine::DataDict;

TEST_CASE("DataDict create + add_table + reopen + resolve") {
    auto p = fs::temp_directory_path() / "openads_m6_dd_basic.add";
    fs::remove(p);
    {
        auto created = DataDict::create(p.string());
        REQUIRE(created.has_value());
        DataDict dd = std::move(created).value();
        REQUIRE(dd.add_table("clientes", "clientes.dbf").has_value());
        REQUIRE(dd.add_table("ventas",   "v\\ventas.dbf").has_value());
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK(dd.has_alias("clientes"));
        CHECK(dd.resolve("clientes")     == "clientes.dbf");
        CHECK(dd.resolve("ventas")       == "v\\ventas.dbf");
        CHECK(dd.resolve("not_an_alias") == "not_an_alias");
    }
    fs::remove(p);
}

TEST_CASE("DataDict open — ADS binary .add — reads Table entries") {
    // pmsys.add is the real proprietary ADS data dictionary from the PMSys
    // fixture set.  We only need to verify that the binary parser loads the
    // Table registry correctly; we do not mutate or re-save the file.
    auto fixture = fs::path(__FILE__).parent_path().parent_path()
                   / "fixtures" / "adi" / "pmsys.add";
    if (!fs::exists(fixture)) {
        WARN("pmsys.add fixture not found, skipping binary DD test");
        return;
    }
    auto opened = DataDict::open(fixture.string());
    REQUIRE(opened.has_value());
    DataDict dd = std::move(opened).value();

    CHECK(dd.has_alias("landlords"));
    CHECK(dd.resolve("landlords")   == ".\\landlords.adt");
    CHECK(dd.has_alias("leases"));
    CHECK(dd.resolve("leases")      == ".\\leases.adt");
    CHECK(dd.has_alias("properties"));
    CHECK(dd.has_alias("managers"));
    // Aliases that should NOT appear
    CHECK_FALSE(dd.has_alias("Database"));
    CHECK_FALSE(dd.has_alias("AutoTasks"));
}

TEST_CASE("DataDict remove_table + reopen no longer has the alias") {
    auto p = fs::temp_directory_path() / "openads_m6_dd_remove.add";
    fs::remove(p);
    {
        auto created = DataDict::create(p.string());
        REQUIRE(created.has_value());
        DataDict dd = std::move(created).value();
        REQUIRE(dd.add_table("a", "a.dbf").has_value());
        REQUIRE(dd.add_table("b", "b.dbf").has_value());
        REQUIRE(dd.remove_table("a").has_value());
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK_FALSE(dd.has_alias("a"));
        CHECK(dd.has_alias("b"));
    }
    fs::remove(p);
}
