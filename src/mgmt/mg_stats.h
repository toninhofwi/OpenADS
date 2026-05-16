#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace openads::mgmt {

// Process-global telemetry counters that a point-in-time session
// snapshot cannot derive (uptime origin, cumulative comm totals,
// high-water marks). One instance per process; the server daemon and
// an in-process (local-mode) DLL each own their own.
struct MgStats {
    std::chrono::system_clock::time_point start_time{
        std::chrono::system_clock::now()};

    std::atomic<std::uint64_t> packets_in{0};
    std::atomic<std::uint64_t> packets_out{0};
    std::atomic<std::uint64_t> bytes_in{0};
    std::atomic<std::uint64_t> bytes_out{0};
    std::atomic<std::uint64_t> disconnects{0};
    std::atomic<std::uint64_t> partial_connects{0};
    std::atomic<std::uint64_t> operations{0};
    std::atomic<std::uint64_t> logged_errors{0};

    std::atomic<std::uint32_t> max_users{0};
    std::atomic<std::uint32_t> max_connections{0};
    std::atomic<std::uint32_t> max_workareas{0};
    std::atomic<std::uint32_t> max_tables{0};
    std::atomic<std::uint32_t> max_indexes{0};
    std::atomic<std::uint32_t> max_locks{0};

    // Raise `hwm` to `cur` if `cur` is larger. Lock-free.
    static void bump_max(std::atomic<std::uint32_t>& hwm,
                         std::uint32_t cur) {
        std::uint32_t prev = hwm.load(std::memory_order_relaxed);
        while (cur > prev &&
               !hwm.compare_exchange_weak(prev, cur,
                                          std::memory_order_relaxed)) {
        }
    }

    void reset_comm() {
        packets_in.store(0);
        packets_out.store(0);
        bytes_in.store(0);
        bytes_out.store(0);
        disconnects.store(0);
        partial_connects.store(0);
    }
};

// Process-global singleton. The server daemon and a local-mode DLL
// each get the one instance for their process.
MgStats& process_mg_stats();

}  // namespace openads::mgmt
