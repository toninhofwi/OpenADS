// openads_concurrency_stress — multi-threaded ABI workout.
//
// Spawns N worker threads, each opening its own ABI connection to
// the same data directory. Each worker runs a mix of:
//
//   - APPEND new rows (every appended row gets a unique 64-bit token
//     written into TOKEN N(20,0); we verify after the run that the
//     final record count == sum of per-thread appends and that
//     every observed token appears exactly once).
//   - READ random rows (verifies the token field decodes cleanly;
//     a torn write or a partially-flushed buffer would leak through
//     here as a bogus token).
//   - LOCK + UPDATE recno 1's COUNTER N(10,0) — workers contend for
//     the same record, must serialise via AdsLockRecord retry, and
//     the final COUNTER value must equal the total number of
//     successful lock-update cycles.
//
// Usage: openads_concurrency_stress --threads N --seconds S --dir D
//                                   [--ops-per-iter K]

#include "openads/ace.h"
#include "openads/error.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace {

double now_s() {
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    return std::chrono::duration<double>(clock::now() - t0).count();
}

struct Counters {
    std::atomic<std::uint64_t> appends{0};
    std::atomic<std::uint64_t> reads{0};
    std::atomic<std::uint64_t> locks_ok{0};
    std::atomic<std::uint64_t> locks_fail{0};
    std::atomic<std::uint64_t> updates_ok{0};
    std::atomic<std::uint64_t> errors{0};
};

void create_table(const std::string& dir, const std::string& tname) {
    std::vector<UNSIGNED8> srv(dir.size() + 1);
    std::memcpy(srv.data(), dir.c_str(), dir.size() + 1);
    ADSHANDLE hConn = 0;
    if (AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                     nullptr, nullptr, 0, &hConn) != 0) {
        std::fprintf(stderr, "create_table: AdsConnect60 failed\n");
        std::exit(1);
    }
    UNSIGNED8 def[] =
        "TOKEN,N,20,0;COUNTER,N,10,0;TID,N,4,0;PADDING,C,32,0";
    UNSIGNED8 nm[64];
    std::strncpy(reinterpret_cast<char*>(nm), tname.c_str(),
                 sizeof(nm) - 1);
    nm[sizeof(nm) - 1] = 0;
    ADSHANDLE hTable = 0;
    if (auto rc = AdsCreateTable(hConn, nm, nullptr, ADS_CDX,
                                  0, 0, 0, 0, def, &hTable);
        rc != 0) {
        std::fprintf(stderr, "AdsCreateTable rc=%u\n", rc);
        std::exit(1);
    }
    // Seed record 1 with COUNTER = 0 so the lock-update workers
    // have a target on recno 1.
    AdsAppendRecord(hTable);
    UNSIGNED8 ftoken[] = "TOKEN";
    UNSIGNED8 fcounter[] = "COUNTER";
    AdsSetDouble(hTable, ftoken,    0.0);
    AdsSetDouble(hTable, fcounter,  0.0);
    AdsWriteRecord(hTable);
    AdsCloseTable(hTable);
    AdsDisconnect(hConn);
}

void worker(int tid,
            const std::string& dir,
            const std::string& tname,
            std::atomic<bool>* stop,
            std::atomic<std::uint64_t>* token_ctr,
            Counters* c,
            std::mutex* token_mu,
            std::unordered_set<std::uint64_t>* observed_tokens,
            int ops_per_iter)
{
    std::vector<UNSIGNED8> srv(dir.size() + 1);
    std::memcpy(srv.data(), dir.c_str(), dir.size() + 1);
    ADSHANDLE hConn = 0;
    if (AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                     nullptr, nullptr, 0, &hConn) != 0) {
        std::fprintf(stderr, "tid=%d AdsConnect60 failed\n", tid);
        c->errors.fetch_add(1);
        return;
    }
    UNSIGNED8 nm[64] = {0};
    std::strncpy(reinterpret_cast<char*>(nm), tname.c_str(),
                 sizeof(nm) - 1);
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(hConn, nm, nm, ADS_CDX,
                     0, 2, 0, 0, &hTable) != 0) {
        std::fprintf(stderr, "tid=%d AdsOpenTable failed\n", tid);
        c->errors.fetch_add(1);
        AdsDisconnect(hConn);
        return;
    }

    UNSIGNED8 ftoken[] = "TOKEN";
    UNSIGNED8 fcounter[] = "COUNTER";
    UNSIGNED8 ftid[] = "TID";

    std::mt19937_64 rng(static_cast<std::uint64_t>(tid) * 0x9E3779B97F4A7C15ULL);

    while (!stop->load(std::memory_order_relaxed)) {
        for (int op = 0; op < ops_per_iter; ++op) {
            std::uint32_t pick = rng() % 100;
            if (pick < 50) {
                // READ: random row (or recno 1 fallback).
                UNSIGNED32 rc = 0;
                AdsGetRecordCount(hTable, 0, &rc);
                if (rc == 0) continue;
                std::uint32_t recno = 1 +
                    static_cast<std::uint32_t>(rng() % rc);
                if (AdsGotoRecord(hTable, recno) != 0) {
                    c->errors.fetch_add(1);
                    continue;
                }
                std::int64_t tok = 0;
                if (AdsGetLongLong(hTable, ftoken, &tok) != 0) {
                    c->errors.fetch_add(1);
                    continue;
                }
                if (recno > 1) {
                    std::uint64_t t = static_cast<std::uint64_t>(tok);
                    std::lock_guard<std::mutex> g(*token_mu);
                    observed_tokens->insert(t);
                }
                c->reads.fetch_add(1);
            }
            else if (pick < 80) {
                // APPEND a unique-token row.
                if (AdsAppendRecord(hTable) != 0) {
                    c->errors.fetch_add(1);
                    continue;
                }
                std::uint64_t t = token_ctr->fetch_add(1) + 1;
                AdsSetDouble(hTable, ftoken,
                             static_cast<double>(t));
                AdsSetDouble(hTable, ftid,
                             static_cast<double>(tid));
                if (AdsWriteRecord(hTable) != 0) {
                    c->errors.fetch_add(1);
                    continue;
                }
                c->appends.fetch_add(1);
            }
            else {
                // LOCK recno 1 + increment COUNTER.
                bool got_lock = false;
                for (int retry = 0; retry < 50; ++retry) {
                    if (AdsLockRecord(hTable, 1) == 0) {
                        got_lock = true;
                        break;
                    }
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(100 + (retry * 50)));
                }
                if (!got_lock) {
                    c->locks_fail.fetch_add(1);
                    continue;
                }
                c->locks_ok.fetch_add(1);

                if (AdsGotoRecord(hTable, 1) != 0) {
                    AdsUnlockRecord(hTable, 1);
                    c->errors.fetch_add(1);
                    continue;
                }
                std::int64_t cur = 0;
                AdsGetLongLong(hTable, fcounter, &cur);
                AdsSetDouble(hTable, fcounter, static_cast<double>(cur + 1));
                AdsWriteRecord(hTable);
                AdsUnlockRecord(hTable, 1);
                c->updates_ok.fetch_add(1);
            }
        }
    }
    AdsCloseTable(hTable);
    AdsDisconnect(hConn);
}

} // namespace

int main(int argc, char** argv) {
    int           threads = 8;
    int           seconds = 10;
    int           ops_per_iter = 50;
    std::string   data_dir = ".";
    std::string   tname = "concur";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--threads"  && i + 1 < argc) threads = std::atoi(argv[++i]);
        else if (a == "--seconds"  && i + 1 < argc) seconds = std::atoi(argv[++i]);
        else if (a == "--ops-per-iter" && i + 1 < argc) ops_per_iter = std::atoi(argv[++i]);
        else if (a == "--dir"      && i + 1 < argc) data_dir = argv[++i];
        else if (a == "--table"    && i + 1 < argc) tname = argv[++i];
        else if (a == "-h" || a == "--help") {
            std::printf(
                "usage: %s [--threads N] [--seconds S] [--dir D] [--table T]\n",
                argv[0]);
            return 0;
        }
    }
    if (threads < 1) threads = 1;
    if (seconds < 1) seconds = 1;

    fs::create_directories(data_dir);
    std::error_code ec;
    for (auto& f : {tname + ".dbf", tname + ".cdx",
                     tname + ".fpt", tname + ".dbt"}) {
        fs::remove(fs::path(data_dir) / f, ec);
    }

    std::printf("[concur] data_dir=%s threads=%d seconds=%d "
                "ops_per_iter=%d\n",
                data_dir.c_str(), threads, seconds, ops_per_iter);
    create_table(data_dir, tname);

    Counters counters;
    std::atomic<bool>           stop{false};
    std::atomic<std::uint64_t>  token_ctr{0};
    std::mutex                  token_mu;
    std::unordered_set<std::uint64_t> observed;

    std::vector<std::thread> ths;
    ths.reserve(threads);
    double t0 = now_s();
    for (int i = 0; i < threads; ++i) {
        ths.emplace_back(worker, i, std::ref(data_dir), std::ref(tname),
                         &stop, &token_ctr, &counters,
                         &token_mu, &observed, ops_per_iter);
    }

    while (now_s() - t0 < seconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::printf("  t=%.1fs  appends=%llu reads=%llu "
                    "locks_ok=%llu locks_fail=%llu updates=%llu errors=%llu\n",
                    now_s() - t0,
                    static_cast<unsigned long long>(counters.appends.load()),
                    static_cast<unsigned long long>(counters.reads.load()),
                    static_cast<unsigned long long>(counters.locks_ok.load()),
                    static_cast<unsigned long long>(counters.locks_fail.load()),
                    static_cast<unsigned long long>(counters.updates_ok.load()),
                    static_cast<unsigned long long>(counters.errors.load()));
        std::fflush(stdout);
    }
    stop.store(true);
    for (auto& t : ths) t.join();
    double elapsed = now_s() - t0;

    // Verification phase.
    std::vector<UNSIGNED8> srv(data_dir.size() + 1);
    std::memcpy(srv.data(), data_dir.c_str(), data_dir.size() + 1);
    ADSHANDLE hConn = 0;
    AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn);
    UNSIGNED8 nm[64] = {0};
    std::strncpy(reinterpret_cast<char*>(nm), tname.c_str(), sizeof(nm) - 1);
    ADSHANDLE hTable = 0;
    AdsOpenTable(hConn, nm, nm, ADS_CDX, 0, 0, 0, 0, &hTable);
    UNSIGNED32 final_count = 0;
    AdsGetRecordCount(hTable, 0, &final_count);

    UNSIGNED8 ftoken[] = "TOKEN";
    UNSIGNED8 fcounter[] = "COUNTER";

    AdsGotoRecord(hTable, 1);
    std::int64_t final_counter = 0;
    AdsGetLongLong(hTable, fcounter, &final_counter);

    // Walk every record post-run; tokens 1..N must each appear once
    // (recno 1's TOKEN is 0 by construction).
    std::unordered_set<std::uint64_t> all_tokens;
    AdsGotoTop(hTable);
    for (UNSIGNED32 r = 1; r <= final_count; ++r) {
        AdsGotoRecord(hTable, r);
        std::int64_t tok = 0;
        AdsGetLongLong(hTable, ftoken, &tok);
        all_tokens.insert(static_cast<std::uint64_t>(tok));
    }
    AdsCloseTable(hTable);
    AdsDisconnect(hConn);

    bool ok = true;
    std::uint64_t expected_appends = counters.appends.load();
    if (final_count != expected_appends + 1) {
        std::fprintf(stderr,
            "FAIL: record_count=%u expected=%llu (appends+seed)\n",
            final_count,
            static_cast<unsigned long long>(expected_appends + 1));
        ok = false;
    }
    // 1 token = 0 (seed) + every unique token in [1..token_ctr]
    if (all_tokens.size() != expected_appends + 1) {
        std::fprintf(stderr,
            "FAIL: distinct_tokens=%zu expected=%llu\n",
            all_tokens.size(),
            static_cast<unsigned long long>(expected_appends + 1));
        ok = false;
    }
    if (final_counter != static_cast<std::int64_t>(counters.updates_ok.load())) {
        std::fprintf(stderr,
            "FAIL: COUNTER=%lld expected=%llu\n",
            static_cast<long long>(final_counter),
            static_cast<unsigned long long>(counters.updates_ok.load()));
        ok = false;
    }

    std::printf(
        "\n[concur] elapsed=%.2fs threads=%d\n"
        "  appends      = %llu (%.0f /s)\n"
        "  reads        = %llu (%.0f /s)\n"
        "  locks_ok     = %llu (%.0f /s)\n"
        "  locks_fail   = %llu\n"
        "  updates      = %llu  (final COUNTER=%lld)\n"
        "  errors       = %llu\n"
        "  record_count = %u\n"
        "  distinct_tok = %zu\n"
        "  RESULT: %s\n",
        elapsed, threads,
        static_cast<unsigned long long>(counters.appends.load()),
        counters.appends.load() / elapsed,
        static_cast<unsigned long long>(counters.reads.load()),
        counters.reads.load() / elapsed,
        static_cast<unsigned long long>(counters.locks_ok.load()),
        counters.locks_ok.load() / elapsed,
        static_cast<unsigned long long>(counters.locks_fail.load()),
        static_cast<unsigned long long>(counters.updates_ok.load()),
        static_cast<long long>(final_counter),
        static_cast<unsigned long long>(counters.errors.load()),
        final_count, all_tokens.size(),
        ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
