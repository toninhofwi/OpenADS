#include "doctest.h"
#include "engine/tx_log.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using openads::engine::TxLog;
using openads::engine::TxRecordType;

TEST_CASE("TxLog: append BEGIN / UPDATE / COMMIT and read them back") {
    auto p = fs::temp_directory_path() / "openads_m5x_txlog.bin";
    fs::remove(p);
    {
        TxLog log;
        REQUIRE(log.open(p.string()).has_value());

        REQUIRE(log.append_begin(7).has_value());
        std::vector<std::uint8_t> before{1, 2, 3};
        std::vector<std::uint8_t> after {9, 8, 7};
        REQUIRE(log.append_update(7, 42, 5, before, after).has_value());
        REQUIRE(log.append_commit(7).has_value());
    }
    {
        TxLog log;
        REQUIRE(log.open(p.string()).has_value());
        auto recs = log.read_all();
        REQUIRE(recs.has_value());
        REQUIRE(recs.value().size() == 3);
        CHECK(recs.value()[0].type  == TxRecordType::Begin);
        CHECK(recs.value()[0].tx_id == 7);
        CHECK(recs.value()[1].type  == TxRecordType::Update);
        CHECK(recs.value()[1].update.table_id == 42);
        CHECK(recs.value()[1].update.recno    == 5);
        CHECK(recs.value()[1].update.before == std::vector<std::uint8_t>{1,2,3});
        CHECK(recs.value()[1].update.after  == std::vector<std::uint8_t>{9,8,7});
        CHECK(recs.value()[2].type  == TxRecordType::Commit);
        CHECK(recs.value()[2].tx_id == 7);
    }
    fs::remove(p);
}

TEST_CASE("TxLog: corrupt CRC truncates the read") {
    auto p = fs::temp_directory_path() / "openads_m5x_txlog_crc.bin";
    fs::remove(p);
    {
        TxLog log;
        REQUIRE(log.open(p.string()).has_value());
        REQUIRE(log.append_begin(1).has_value());
        REQUIRE(log.append_commit(1).has_value());
    }
    // Flip the last byte of the file to break the trailing CRC.
    auto sz = fs::file_size(p);
    std::vector<std::uint8_t> bytes(sz);
    {
        std::ifstream in(p, std::ios::binary);
        in.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    bytes.back() ^= 0xFFu;
    {
        std::ofstream out(p, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    {
        TxLog log;
        REQUIRE(log.open(p.string()).has_value());
        auto recs = log.read_all();
        REQUIRE(recs.has_value());
        // Only the BEGIN survives; the COMMIT is dropped at the CRC boundary.
        CHECK(recs.value().size() == 1);
        CHECK(recs.value()[0].type == TxRecordType::Begin);
    }
    fs::remove(p);
}

TEST_CASE("TxLog: truncate clears the file") {
    auto p = fs::temp_directory_path() / "openads_m5x_txlog_trunc.bin";
    fs::remove(p);
    {
        TxLog log;
        REQUIRE(log.open(p.string()).has_value());
        REQUIRE(log.append_begin(2).has_value());
        REQUIRE(log.append_commit(2).has_value());
        REQUIRE(log.truncate().has_value());
        REQUIRE(log.append_begin(3).has_value());
        REQUIRE(log.append_commit(3).has_value());

        auto recs = log.read_all();
        REQUIRE(recs.has_value());
        REQUIRE(recs.value().size() == 2);
        CHECK(recs.value()[0].tx_id == 3);
        CHECK(recs.value()[1].tx_id == 3);
    }
    fs::remove(p);
}
