#include "doctest.h"
#include "engine/data_dict.h"

#include <cstring>
#include <filesystem>
#include <fstream>
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

// -------------------------------------------------------------------------
// Binary .add round-trip tests
// -------------------------------------------------------------------------

// Locate the pmsys.add fixture relative to this source file.
static fs::path pmsys_fixture() {
    return fs::path(__FILE__).parent_path().parent_path()
           / "fixtures" / "adi" / "pmsys.add";
}

// Copy pmsys.add to a writable temp path so mutations don't touch the fixture.
static fs::path stage_pmsys(const fs::path& dest_dir) {
    fs::create_directories(dest_dir);
    auto src = pmsys_fixture();
    auto dst = dest_dir / "pmsys.add";
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
    return dst;
}

TEST_CASE("DataDict binary .add — add_table round-trips through file") {
    auto fixture = pmsys_fixture();
    if (!fs::exists(fixture)) {
        WARN("pmsys.add fixture not found, skipping binary write test");
        return;
    }
    const auto dir = fs::temp_directory_path() / "openads_dd_bin_add";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto add_path = stage_pmsys(dir);

    // Add a new alias to the binary DD.
    {
        auto opened = DataDict::open(add_path.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        REQUIRE(dd.add_table("newtable", ".\\newtable.adt").has_value());
    }

    // File must still start with the binary signature.
    {
        std::ifstream f(add_path, std::ios::binary);
        char sig[20] = {};
        f.read(sig, 20);
        CHECK(std::memcmp(sig, "ADS Data Dictionary", 19) == 0);
        CHECK(static_cast<unsigned char>(sig[19]) == 0x00);
    }

    // Reopen: new alias present, original aliases still intact.
    {
        auto opened = DataDict::open(add_path.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK(dd.has_alias("newtable"));
        CHECK(dd.resolve("newtable") == ".\\newtable.adt");
        CHECK(dd.has_alias("landlords"));
        CHECK(dd.has_alias("leases"));
        CHECK(dd.has_alias("properties"));
    }

    fs::remove_all(dir, ec);
}

TEST_CASE("DataDict binary .add — remove_table round-trips through file") {
    auto fixture = pmsys_fixture();
    if (!fs::exists(fixture)) {
        WARN("pmsys.add fixture not found, skipping binary write test");
        return;
    }
    const auto dir = fs::temp_directory_path() / "openads_dd_bin_rm";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto add_path = stage_pmsys(dir);

    // Remove an existing alias.
    {
        auto opened = DataDict::open(add_path.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        REQUIRE(dd.has_alias("landlords"));
        REQUIRE(dd.remove_table("landlords").has_value());
    }

    // File must still be binary.
    {
        std::ifstream f(add_path, std::ios::binary);
        char sig[20] = {};
        f.read(sig, 20);
        CHECK(std::memcmp(sig, "ADS Data Dictionary", 19) == 0);
    }

    // Reopen: removed alias gone, others intact.
    {
        auto opened = DataDict::open(add_path.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK_FALSE(dd.has_alias("landlords"));
        CHECK(dd.has_alias("leases"));
        CHECK(dd.has_alias("properties"));
    }

    fs::remove_all(dir, ec);
}

TEST_CASE("DataDict binary .add — all original records preserved after mutation") {
    auto fixture = pmsys_fixture();
    if (!fs::exists(fixture)) {
        WARN("pmsys.add fixture not found, skipping binary write test");
        return;
    }
    const auto dir = fs::temp_directory_path() / "openads_dd_bin_preserve";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto add_path = stage_pmsys(dir);

    auto orig_size = fs::file_size(add_path);

    // Add a table — file must grow by exactly one record (524 bytes).
    {
        auto opened = DataDict::open(add_path.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        REQUIRE(dd.add_table("extra", ".\\extra.adt").has_value());
    }
    CHECK(fs::file_size(add_path) == orig_size + 524u);

    // Remove the added table — size stays the same (slot marked deleted, not reclaimed).
    {
        auto opened = DataDict::open(add_path.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        REQUIRE(dd.remove_table("extra").has_value());
    }
    CHECK(fs::file_size(add_path) == orig_size + 524u);

    // The original 32 Table aliases must all survive.
    {
        auto opened = DataDict::open(add_path.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        for (auto* name : {"landlords", "leases", "properties", "managers",
                           "sys_registry"}) {
            CHECK(dd.has_alias(name));
        }
        CHECK_FALSE(dd.has_alias("extra"));
    }

    fs::remove_all(dir, ec);
}

TEST_CASE("DataDict binary .add — create_user / delete_user round-trip") {
    auto fixture = pmsys_fixture();
    if (!fs::exists(fixture)) {
        WARN("pmsys.add fixture not found, skipping binary write test");
        return;
    }
    const auto dir = fs::temp_directory_path() / "openads_dd_bin_user";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto add_path = stage_pmsys(dir);

    {
        auto opened = DataDict::open(add_path.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        REQUIRE(dd.create_user("testuser").has_value());
    }
    {
        auto opened = DataDict::open(add_path.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK(dd.has_user("testuser"));
        REQUIRE(dd.delete_user("testuser").has_value());
    }
    {
        auto opened = DataDict::open(add_path.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK_FALSE(dd.has_user("testuser"));
        // File is still binary.
        std::ifstream f(add_path, std::ios::binary);
        char sig[20] = {};
        f.read(sig, 20);
        CHECK(std::memcmp(sig, "ADS Data Dictionary", 19) == 0);
    }

    fs::remove_all(dir, ec);
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
