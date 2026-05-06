#include "doctest.h"
#include "engine/tx_log.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <thread>
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
        REQUIRE(log.append_update(7, "data.dbf", 5, before, after).has_value());
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
        CHECK(recs.value()[1].update.table_path == "data.dbf");
        CHECK(recs.value()[1].update.recno      == 5);
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

TEST_CASE("TxLog: assigns monotonically increasing LSNs and sync_to is idempotent") {
    auto p = fs::temp_directory_path() / "openads_m54_txlog_lsn.bin";
    fs::remove(p);
    {
        TxLog log;
        REQUIRE(log.open(p.string()).has_value());

        auto a = log.append_begin_async (1);
        auto b = log.append_commit_async(1);
        auto c = log.append_begin_async (2);
        auto d = log.append_commit_async(2);
        REQUIRE(a.has_value()); REQUIRE(b.has_value());
        REQUIRE(c.has_value()); REQUIRE(d.has_value());
        // Strictly monotonic.
        CHECK(a.value() < b.value());
        CHECK(b.value() < c.value());
        CHECK(c.value() < d.value());

        // Before any sync_to, nothing is durable.
        CHECK(log.last_synced_lsn() == 0);

        // sync_to advances to the high-water mark; second call is a no-op.
        REQUIRE(log.sync_to(d.value()).has_value());
        CHECK(log.last_synced_lsn() >= d.value());
        std::uint64_t hwm = log.last_synced_lsn();
        REQUIRE(log.sync_to(d.value()).has_value());
        CHECK(log.last_synced_lsn() == hwm);
    }
    fs::remove(p);
}

TEST_CASE("TxLog: group commit — many threads append, single fsync covers all") {
    auto p = fs::temp_directory_path() / "openads_m54_txlog_groupcommit.bin";
    fs::remove(p);

    constexpr int N_THREADS  = 4;
    constexpr int N_PER_THR  = 50;

    std::vector<std::uint64_t> commit_lsns(N_THREADS * N_PER_THR);

    {
        TxLog log;
        REQUIRE(log.open(p.string()).has_value());

        std::vector<std::thread> threads;
        threads.reserve(N_THREADS);
        for (int t = 0; t < N_THREADS; ++t) {
            threads.emplace_back([&, t]() {
                for (std::size_t i = 0; i < N_PER_THR; ++i) {
                    std::uint64_t tx_id = static_cast<std::uint64_t>(
                        t * N_PER_THR + i + 1);
                    auto begin  = log.append_begin_async(tx_id);
                    auto commit = log.append_commit_async(tx_id);
                    REQUIRE(begin.has_value());
                    REQUIRE(commit.has_value());
                    commit_lsns[t * N_PER_THR + i] = commit.value();
                }
            });
        }
        for (auto& th : threads) th.join();

        // One fsync flushes every record.
        std::uint64_t max_lsn = 0;
        for (auto x : commit_lsns) if (x > max_lsn) max_lsn = x;
        REQUIRE(log.sync_to(max_lsn).has_value());
        CHECK(log.last_synced_lsn() >= max_lsn);

        // A second sync_to with a smaller target is a no-op.
        REQUIRE(log.sync_to(max_lsn / 2).has_value());
    }

    // Reopen and verify all 200 commits are durable and LSNs are unique.
    {
        TxLog log;
        REQUIRE(log.open(p.string()).has_value());
        auto recs = log.read_all();
        REQUIRE(recs.has_value());
        CHECK(recs.value().size() == N_THREADS * N_PER_THR * 2);

        std::vector<std::uint64_t> seen;
        seen.reserve(recs.value().size());
        for (auto& r : recs.value()) seen.push_back(r.lsn);
        std::sort(seen.begin(), seen.end());
        for (std::size_t i = 1; i < seen.size(); ++i) {
            CHECK(seen[i] > seen[i - 1]);
        }
        // Reopen resumes the LSN counter past the surviving high-water mark.
        CHECK(log.high_water_lsn() > seen.back());
        CHECK(log.last_synced_lsn() == seen.back());
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
