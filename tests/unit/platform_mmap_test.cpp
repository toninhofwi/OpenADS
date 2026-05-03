#include "doctest.h"
#include "platform/file.h"
#include "platform/mmap.h"

#include <array>
#include <cstdint>
#include <filesystem>

namespace fs = std::filesystem;
using openads::platform::File;
using openads::platform::FileMap;
using openads::platform::OpenMode;

TEST_CASE("FileMap exposes a read-only view of the file") {
    const auto p = fs::temp_directory_path() / "openads_test_mmap";
    fs::remove(p);
    {
        auto fres = File::open(p.string(), OpenMode::CreateRW);
        REQUIRE(fres.has_value());
        File f = std::move(fres).value();
        std::array<std::uint8_t, 8> payload{0xDE, 0xAD, 0xBE, 0xEF,
                                            0x01, 0x02, 0x03, 0x04};
        REQUIRE(f.write_at(0, payload.data(), payload.size()).has_value());
    }

    {
        auto fres = File::open(p.string(), OpenMode::ReadOnly);
        REQUIRE(fres.has_value());
        File f = std::move(fres).value();

        auto m = FileMap::map_readonly(f, 0, 8);
        REQUIRE(m.has_value());
        auto bytes = std::move(m).value().bytes();
        CHECK(bytes.size() == 8);
        CHECK(bytes[0] == 0xDE);
        CHECK(bytes[3] == 0xEF);
    }

    fs::remove(p);
}
