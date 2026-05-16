# AdsMg* Server-Telemetry Subsystem Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the ~17 stubbed `AdsMg*` management exports with a real telemetry subsystem so Harbour's `manage.prg` reports live server data instead of zeros.

**Architecture:** A single `MgCollector` turns a raw `MgSnapshot` + process-global `MgStats` into the SAP-canonical `ADS_MGMT_*` structs. Local-mode `AdsMg*` builds the snapshot in-process; remote-mode ships a `MgRequest` wire frame to the server, which builds the snapshot from its session registry and replies. `mg_wire` does explicit little-endian serialization so a 32-bit client and 64-bit server interoperate.

**Tech Stack:** C++17, CMake, doctest unit tests, existing `openads::network` wire protocol, Harbour `rddads` smoke harness.

Spec: `docs/superpowers/specs/2026-05-16-adsmg-telemetry-design.md`

---

## File Structure

New files:
- `src/mgmt/mg_stats.h` — process-global atomic counter block.
- `src/mgmt/mg_snapshot.h` — POD describing raw collected telemetry.
- `src/mgmt/mg_collector.h` / `.cpp` — `MgSnapshot` + `MgStats` → `ADS_MGMT_*`.
- `src/network/mg_wire.h` / `.cpp` — LE serialization of `MgSnapshot` and request kinds.
- `tests/unit/mg_collector_test.cpp` — collector unit tests.
- `tests/unit/mg_wire_test.cpp` — serialization round-trip tests.

Modified files:
- `src/network/wire.h` — new opcodes.
- `src/network/server.h` / `.cpp` — `MgRequest` handler, snapshot builder, `MgStats` wiring, comm-counter bumps.
- `src/abi/ace_exports.cpp` — real `AdsMg*` exports + local/remote dispatch.
- `tests/unit/abi_mgmt_test.cpp` — updated expectations (no longer all-zero).
- `src/CMakeLists.txt` — add `mgmt/mg_collector.cpp`, `network/mg_wire.cpp`.
- `tests/CMakeLists.txt` — add the two new test files.

Naming conventions confirmed against the codebase:
- Namespace `openads::mgmt` for the new subsystem (mirrors `openads::network`).
- Test framework is **doctest** (`#include "doctest.h"`, `TEST_CASE`, `REQUIRE`, `CHECK`).
- Result type is `openads::util::Result<T>` (see `src/util/result.h`).

---

## Phase 1 — MgStats + MgSnapshot + MgCollector (local)

### Task 1: MgStats counter block

**Files:**
- Create: `src/mgmt/mg_stats.h`

- [ ] **Step 1: Write the file**

```cpp
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
```

- [ ] **Step 2: Commit**

```bash
git add src/mgmt/mg_stats.h
git commit -m "feat(mgmt): MgStats process-global telemetry counters"
```

### Task 2: MgSnapshot raw-telemetry POD

**Files:**
- Create: `src/mgmt/mg_snapshot.h`

- [ ] **Step 1: Write the file**

```cpp
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace openads::mgmt {

// One connected user / session.
struct MgUser {
    std::string                           name;
    std::string                           address;     // "ip:port"
    std::string                           os_login;
    std::uint16_t                         conn_no = 0;
    std::chrono::system_clock::time_point  connected_at{};
};

// One open table in a session.
struct MgTable {
    std::string   name;
    std::string   user;
    std::uint16_t conn_no   = 0;
    std::uint16_t open_mode = 0;   // 0 = shared, 1 = exclusive
    std::uint16_t lock_type = 0;   // ADS_MGMT_* lock constant
};

// One open index/order.
struct MgIndex {
    std::string name;
    std::string tag;
    std::string expression;
};

// One held lock.
struct MgLock {
    std::string   user;
    std::uint16_t conn_no = 0;
    std::uint32_t recno   = 0;
};

// One worker thread.
struct MgThread {
    std::uint32_t thread_no = 0;
    std::uint16_t opcode    = 0;
    std::string   user;
    std::uint16_t conn_no   = 0;
    std::string   os_login;
};

// Point-in-time raw telemetry. Built by collect_local_snapshot() in a
// DLL process or collect_server_snapshot(Server&) in the daemon, then
// formatted into ADS_MGMT_* structs by MgCollector.
struct MgSnapshot {
    std::uint32_t users           = 0;
    std::uint32_t connections     = 0;
    std::uint32_t workareas       = 0;
    std::uint32_t tables          = 0;
    std::uint32_t indexes         = 0;
    std::uint32_t locks           = 0;
    std::uint32_t worker_threads  = 0;
    std::uint16_t server_type     = 0;   // 0 = unknown/local

    std::vector<MgUser>   user_list;
    std::vector<MgTable>  table_list;
    std::vector<MgIndex>  index_list;
    std::vector<MgLock>   lock_list;
    std::vector<MgThread> thread_list;
};

}  // namespace openads::mgmt
```

- [ ] **Step 2: Commit**

```bash
git add src/mgmt/mg_snapshot.h
git commit -m "feat(mgmt): MgSnapshot raw-telemetry POD"
```

### Task 3: MgCollector header

**Files:**
- Create: `src/mgmt/mg_collector.h`

- [ ] **Step 1: Write the file**

```cpp
#pragma once

#include "mgmt/mg_snapshot.h"
#include "mgmt/mg_stats.h"
#include "openads/ace.h"

#include <vector>

namespace openads::mgmt {

// Formats a raw MgSnapshot + MgStats into the SAP-canonical
// ADS_MGMT_* structs declared in include/openads/ace.h. Pure: holds
// copies of its inputs and never touches global state, so it is
// trivially unit-testable with a fabricated snapshot.
class MgCollector {
public:
    MgCollector(MgSnapshot snapshot, const MgStats& stats);

    ADS_MGMT_INSTALL_INFO   install_info() const;
    ADS_MGMT_ACTIVITY_INFO  activity_info() const;
    ADS_MGMT_COMM_STATS     comm_stats() const;
    ADS_MGMT_CONFIG_PARAMS  config_params() const;
    ADS_MGMT_CONFIG_MEMORY  config_memory() const;

    std::vector<ADS_MGMT_USER_INFO>       user_names() const;
    std::vector<ADS_MGMT_TABLE_INFO>      open_tables() const;
    std::vector<ADS_MGMT_INDEX_INFO>      open_indexes() const;
    std::vector<ADS_MGMT_LOCK_INFO>       locks() const;
    std::vector<ADS_MGMT_THREAD_ACTIVITY> worker_thread_activity() const;

    // Returns the lock held on (conn-agnostic) `recno`; usConnNumber
    // is 0 and ulRecordNumber is 0 when no such lock exists.
    ADS_MGMT_LOCK_INFO lock_owner(std::uint32_t recno) const;

    std::uint16_t server_type() const { return snapshot_.server_type; }

    const MgSnapshot& snapshot() const { return snapshot_; }

private:
    MgSnapshot    snapshot_;
    // Plain copies of the counters captured at construction time.
    std::uint64_t packets_in_;
    std::uint64_t packets_out_;
    std::uint64_t bytes_in_;
    std::uint64_t bytes_out_;
    std::uint64_t disconnects_;
    std::uint64_t partial_connects_;
    std::uint64_t operations_;
    std::uint64_t logged_errors_;
    std::uint32_t max_users_;
    std::uint32_t max_connections_;
    std::uint32_t max_workareas_;
    std::uint32_t max_tables_;
    std::uint32_t max_indexes_;
    std::uint32_t max_locks_;
    long long     uptime_seconds_;
};

}  // namespace openads::mgmt
```

- [ ] **Step 2: Commit**

```bash
git add src/mgmt/mg_collector.h
git commit -m "feat(mgmt): MgCollector header"
```

### Task 4: MgCollector test — install_info

**Files:**
- Create: `tests/unit/mg_collector_test.cpp`
- Modify: `tests/CMakeLists.txt` (add `unit/mg_collector_test.cpp` after line 75)

- [ ] **Step 1: Add the test file to the build**

In `tests/CMakeLists.txt`, after the line `    unit/abi_mgmt_test.cpp`, add:

```cmake
    unit/mg_collector_test.cpp
    unit/mg_wire_test.cpp
```

(`mg_wire_test.cpp` is created in Task 9; add both now so the build file is touched once.)

- [ ] **Step 2: Write the failing test**

```cpp
#include "doctest.h"

#include "mgmt/mg_collector.h"
#include "mgmt/mg_snapshot.h"
#include "mgmt/mg_stats.h"

#include <cstring>
#include <string>

using openads::mgmt::MgCollector;
using openads::mgmt::MgSnapshot;
using openads::mgmt::MgStats;

namespace {
std::string c_str_of(const UNSIGNED8* p, std::size_t cap) {
    std::size_t n = 0;
    while (n < cap && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}
}  // namespace

TEST_CASE("MgCollector::install_info reports the product string") {
    MgSnapshot snap;
    MgStats    stats;
    MgCollector c(snap, stats);

    ADS_MGMT_INSTALL_INFO info = c.install_info();
    CHECK(c_str_of(info.aucVersionStr, sizeof(info.aucVersionStr))
              .rfind("OpenADS", 0) == 0);
    // OpenADS is not serial-licensed: serial reports empty.
    CHECK(c_str_of(info.aucSerialNumber,
                   sizeof(info.aucSerialNumber)).empty());
}
```

- [ ] **Step 3: Run the test, verify it fails**

Run: `cmake --build build/default --target openads_unit_tests`
Expected: FAIL — `mg_collector.cpp` not yet in the library, link error `undefined reference to MgCollector::MgCollector`.

- [ ] **Step 4: Create the implementation file**

Create `src/mgmt/mg_collector.cpp`:

```cpp
#include "mgmt/mg_collector.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace openads::mgmt {

namespace {

// Copy `s` into a fixed UNSIGNED8[cap] field, NUL-terminated and
// truncated. Trailing bytes are zeroed so the struct has no
// uninitialised tail.
void put_field(UNSIGNED8* dst, std::size_t cap, const std::string& s) {
    std::memset(dst, 0, cap);
    if (cap == 0) return;
    std::size_t n = std::min(s.size(), cap - 1);
    std::memcpy(dst, s.data(), n);
}

}  // namespace

MgCollector::MgCollector(MgSnapshot snapshot, const MgStats& stats)
    : snapshot_(std::move(snapshot)),
      packets_in_(stats.packets_in.load()),
      packets_out_(stats.packets_out.load()),
      bytes_in_(stats.bytes_in.load()),
      bytes_out_(stats.bytes_out.load()),
      disconnects_(stats.disconnects.load()),
      partial_connects_(stats.partial_connects.load()),
      operations_(stats.operations.load()),
      logged_errors_(stats.logged_errors.load()),
      max_users_(stats.max_users.load()),
      max_connections_(stats.max_connections.load()),
      max_workareas_(stats.max_workareas.load()),
      max_tables_(stats.max_tables.load()),
      max_indexes_(stats.max_indexes.load()),
      max_locks_(stats.max_locks.load()) {
    auto now  = std::chrono::system_clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                    now - stats.start_time).count();
    uptime_seconds_ = secs < 0 ? 0 : secs;
}

ADS_MGMT_INSTALL_INFO MgCollector::install_info() const {
    ADS_MGMT_INSTALL_INFO info;
    std::memset(&info, 0, sizeof(info));
    info.ulUserOption = 0;
    put_field(info.aucRegisteredOwner, sizeof(info.aucRegisteredOwner),
              "OpenADS");
    put_field(info.aucVersionStr, sizeof(info.aucVersionStr),
              "OpenADS 1.0");
    // aucSerialNumber / aucEvalExpireDate intentionally left empty:
    // OpenADS is not serial-licensed (see spec "honesty" table).
    return info;
}

}  // namespace openads::mgmt
```

- [ ] **Step 5: Add the source to the core library**

In `src/CMakeLists.txt`, after line `    network/client.cpp` (line 52), add:

```cmake
    mgmt/mg_collector.cpp
    network/mg_wire.cpp
```

(`mg_wire.cpp` is created in Task 8; add both now so the build file is touched once. The file must exist before the first build that includes it — Task 8 creates it; until then, temporarily also create an empty-shell `src/network/mg_wire.cpp` containing only `#include "network/mg_wire.h"` is NOT possible since the header does not exist yet. Therefore: add ONLY `mgmt/mg_collector.cpp` here now, and add `network/mg_wire.cpp` in Task 8.)

Correct edit for this step — add only:

```cmake
    mgmt/mg_collector.cpp
```

- [ ] **Step 6: Run the test, verify it passes**

Run: `cmake --build build/default --target openads_unit_tests && ./build/default/tests/openads_unit_tests -tc="MgCollector::install_info reports the product string"`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/mgmt/mg_collector.cpp src/mgmt/mg_collector.h tests/unit/mg_collector_test.cpp tests/CMakeLists.txt src/CMakeLists.txt
git commit -m "feat(mgmt): MgCollector::install_info"
```

### Task 5: MgCollector::activity_info

**Files:**
- Modify: `src/mgmt/mg_collector.cpp`
- Modify: `tests/unit/mg_collector_test.cpp`

- [ ] **Step 1: Write the failing test** — append to `mg_collector_test.cpp`

```cpp
TEST_CASE("MgCollector::activity_info maps counts and uptime") {
    MgSnapshot snap;
    snap.connections    = 3;
    snap.workareas      = 7;
    snap.tables         = 5;
    snap.indexes        = 2;
    snap.locks          = 1;
    snap.worker_threads = 4;
    snap.user_list.resize(3);   // 3 users

    MgStats stats;
    stats.max_connections.store(9);
    stats.operations.store(120);
    stats.logged_errors.store(2);

    MgCollector c(snap, stats);
    ADS_MGMT_ACTIVITY_INFO a = c.activity_info();

    CHECK(a.ulOperations              == 120);
    CHECK(a.ulLoggedErrors            == 2);
    CHECK(a.stConnections.ulInUse     == 3);
    CHECK(a.stConnections.ulMaxUsed   == 9);
    CHECK(a.stWorkAreas.ulInUse       == 7);
    CHECK(a.stTables.ulInUse          == 5);
    CHECK(a.stIndexes.ulInUse         == 2);
    CHECK(a.stLocks.ulInUse           == 1);
    CHECK(a.stWorkerThreads.ulInUse   == 4);
    CHECK(a.stUsers.ulInUse           == 3);
}
```

- [ ] **Step 2: Run the test, verify it fails**

Run: `cmake --build build/default --target openads_unit_tests`
Expected: FAIL — `MgCollector::activity_info` undefined.

- [ ] **Step 3: Implement** — append to `mg_collector.cpp` (before the closing namespace brace)

```cpp
ADS_MGMT_ACTIVITY_INFO MgCollector::activity_info() const {
    ADS_MGMT_ACTIVITY_INFO a;
    std::memset(&a, 0, sizeof(a));

    a.ulOperations   = static_cast<UNSIGNED32>(operations_);
    a.ulLoggedErrors = static_cast<UNSIGNED32>(logged_errors_);

    long long up = uptime_seconds_;
    a.stUpTime.usDays    = static_cast<UNSIGNED16>(up / 86400);
    a.stUpTime.usHours   = static_cast<UNSIGNED16>((up % 86400) / 3600);
    a.stUpTime.usMinutes = static_cast<UNSIGNED16>((up % 3600) / 60);
    a.stUpTime.usSeconds = static_cast<UNSIGNED16>(up % 60);

    auto usage = [](UNSIGNED32 in_use, UNSIGNED32 max_used) {
        ADS_MGMT_USAGE_STRUCT u;
        u.ulInUse   = in_use;
        u.ulMaxUsed = max_used < in_use ? in_use : max_used;
        u.ulRejected = 0;
        return u;
    };

    UNSIGNED32 nusers = static_cast<UNSIGNED32>(snapshot_.user_list.size());
    a.stUsers        = usage(nusers, max_users_);
    a.stConnections  = usage(snapshot_.connections, max_connections_);
    a.stWorkAreas    = usage(snapshot_.workareas, max_workareas_);
    a.stTables       = usage(snapshot_.tables, max_tables_);
    a.stIndexes      = usage(snapshot_.indexes, max_indexes_);
    a.stLocks        = usage(snapshot_.locks, max_locks_);
    a.stWorkerThreads = usage(snapshot_.worker_threads, 0);
    // TPS* elem usage left zero — transaction-processing internals are
    // not exposed (see spec "honesty" table).
    return a;
}
```

- [ ] **Step 4: Run the test, verify it passes**

Run: `cmake --build build/default --target openads_unit_tests && ./build/default/tests/openads_unit_tests -tc="MgCollector::activity_info maps counts and uptime"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/mgmt/mg_collector.cpp tests/unit/mg_collector_test.cpp
git commit -m "feat(mgmt): MgCollector::activity_info"
```

### Task 6: MgCollector::comm_stats, config_params, config_memory

**Files:**
- Modify: `src/mgmt/mg_collector.cpp`
- Modify: `tests/unit/mg_collector_test.cpp`

- [ ] **Step 1: Write the failing test** — append to `mg_collector_test.cpp`

```cpp
TEST_CASE("MgCollector::comm_stats reports real packet totals only") {
    MgSnapshot snap;
    MgStats    stats;
    stats.packets_in.store(40);
    stats.packets_out.store(60);
    stats.disconnects.store(2);
    stats.partial_connects.store(1);

    MgCollector c(snap, stats);
    ADS_MGMT_COMM_STATS s = c.comm_stats();

    CHECK(s.ulTotalPackets      == 100);   // in + out
    CHECK(s.ulDisconnectedUsers == 2);
    CHECK(s.ulPartialConnects   == 1);
    // No checksum / sequencing in our framing — honest zeros.
    CHECK(s.dPercentCheckSums   == doctest::Approx(0.0));
    CHECK(s.ulCheckSumFailures  == 0);
    CHECK(s.ulRcvPktOutOfSeq    == 0);
}

TEST_CASE("MgCollector::config_params echoes live counts") {
    MgSnapshot snap;
    snap.connections = 3;
    snap.tables      = 5;
    snap.worker_threads = 4;
    MgStats stats;

    MgCollector c(snap, stats);
    ADS_MGMT_CONFIG_PARAMS p = c.config_params();
    CHECK(p.ulNumConnections  == 3);
    CHECK(p.ulNumTables       == 5);
    CHECK(p.usNumWorkerThreads == 4);
    // NetWare-era ECB fields are honest zeros.
    CHECK(p.usNumReceiveECBs  == 0);
    CHECK(p.usNumSendECBs     == 0);
}
```

- [ ] **Step 2: Run the test, verify it fails**

Run: `cmake --build build/default --target openads_unit_tests`
Expected: FAIL — `comm_stats` / `config_params` undefined.

- [ ] **Step 3: Implement** — append to `mg_collector.cpp`

```cpp
ADS_MGMT_COMM_STATS MgCollector::comm_stats() const {
    ADS_MGMT_COMM_STATS s;
    std::memset(&s, 0, sizeof(s));
    s.ulTotalPackets      = static_cast<UNSIGNED32>(
        packets_in_ + packets_out_);
    s.ulDisconnectedUsers = static_cast<UNSIGNED32>(disconnects_);
    s.ulPartialConnects   = static_cast<UNSIGNED32>(partial_connects_);
    // dPercentCheckSums, ulCheckSumFailures, ulRcvPktOutOfSeq,
    // ulRcvReqOutOfSeq, ulNotLoggedIn, ulInvalidPackets,
    // ulRecvFromErrors, ulSendToErrors — no analogue in OpenADS'
    // TCP framing; left as honest zeros (see spec "honesty" table).
    return s;
}

ADS_MGMT_CONFIG_PARAMS MgCollector::config_params() const {
    ADS_MGMT_CONFIG_PARAMS p;
    std::memset(&p, 0, sizeof(p));
    p.ulNumConnections   = snapshot_.connections;
    p.ulNumWorkAreas     = snapshot_.workareas;
    p.ulNumTables        = snapshot_.tables;
    p.ulNumIndexes       = snapshot_.indexes;
    p.ulNumLocks         = snapshot_.locks;
    p.usNumWorkerThreads = static_cast<UNSIGNED16>(
        snapshot_.worker_threads);
    // ECB / burst-packet / TPS fields left zero — NetWare-era, no
    // analogue. Path strings left empty.
    return p;
}

ADS_MGMT_CONFIG_MEMORY MgCollector::config_memory() const {
    ADS_MGMT_CONFIG_MEMORY m;
    std::memset(&m, 0, sizeof(m));
    // Per-category accounting is out of scope (no allocator
    // instrumentation). ulTotalConfigMem stays 0 here; a process-RSS
    // total can be wired in later without changing this interface.
    return m;
}
```

- [ ] **Step 4: Run the test, verify it passes**

Run: `cmake --build build/default --target openads_unit_tests && ./build/default/tests/openads_unit_tests -tc="MgCollector::comm_stats reports real packet totals only,MgCollector::config_params echoes live counts"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/mgmt/mg_collector.cpp tests/unit/mg_collector_test.cpp
git commit -m "feat(mgmt): MgCollector comm_stats + config params/memory"
```

### Task 7: MgCollector list accessors + lock_owner

**Files:**
- Modify: `src/mgmt/mg_collector.cpp`
- Modify: `tests/unit/mg_collector_test.cpp`

- [ ] **Step 1: Write the failing test** — append to `mg_collector_test.cpp`

```cpp
TEST_CASE("MgCollector list accessors map snapshot vectors") {
    MgSnapshot snap;

    openads::mgmt::MgUser u;
    u.name = "alice"; u.address = "10.0.0.2:5000";
    u.os_login = "alice"; u.conn_no = 1;
    snap.user_list.push_back(u);

    openads::mgmt::MgTable t;
    t.name = "orders.adt"; t.user = "alice";
    t.conn_no = 1; t.open_mode = 0; t.lock_type = ADS_MGMT_NO_LOCK;
    snap.table_list.push_back(t);

    openads::mgmt::MgIndex ix;
    ix.name = "orders.adi"; ix.tag = "CUSTNO";
    ix.expression = "CUSTNO";
    snap.index_list.push_back(ix);

    openads::mgmt::MgLock lk;
    lk.user = "alice"; lk.conn_no = 1; lk.recno = 42;
    snap.lock_list.push_back(lk);

    MgStats stats;
    MgCollector c(snap, stats);

    auto users = c.user_names();
    REQUIRE(users.size() == 1);
    CHECK(c_str_of(users[0].aucUserName,
                   sizeof(users[0].aucUserName)) == "alice");
    CHECK(users[0].usConnNumber == 1);

    auto tables = c.open_tables();
    REQUIRE(tables.size() == 1);
    CHECK(c_str_of(tables[0].aucTableName,
                   sizeof(tables[0].aucTableName)) == "orders.adt");

    auto idxs = c.open_indexes();
    REQUIRE(idxs.size() == 1);
    CHECK(c_str_of(idxs[0].aucTagName,
                   sizeof(idxs[0].aucTagName)) == "CUSTNO");

    auto lks = c.locks();
    REQUIRE(lks.size() == 1);
    CHECK(lks[0].ulRecordNumber == 42);

    ADS_MGMT_LOCK_INFO owner = c.lock_owner(42);
    CHECK(owner.ulRecordNumber == 42);
    CHECK(c_str_of(owner.aucUserName,
                   sizeof(owner.aucUserName)) == "alice");

    ADS_MGMT_LOCK_INFO none = c.lock_owner(999);
    CHECK(none.ulRecordNumber == 0);
}
```

- [ ] **Step 2: Run the test, verify it fails**

Run: `cmake --build build/default --target openads_unit_tests`
Expected: FAIL — list accessors undefined.

- [ ] **Step 3: Implement** — append to `mg_collector.cpp`

```cpp
std::vector<ADS_MGMT_USER_INFO> MgCollector::user_names() const {
    std::vector<ADS_MGMT_USER_INFO> out;
    out.reserve(snapshot_.user_list.size());
    for (const auto& u : snapshot_.user_list) {
        ADS_MGMT_USER_INFO i;
        std::memset(&i, 0, sizeof(i));
        put_field(i.aucUserName, sizeof(i.aucUserName), u.name);
        put_field(i.aucAddress, sizeof(i.aucAddress), u.address);
        put_field(i.aucOSUserLoginName,
                  sizeof(i.aucOSUserLoginName), u.os_login);
        put_field(i.aucAuthUserName,
                  sizeof(i.aucAuthUserName), u.name);
        i.usConnNumber = u.conn_no;
        out.push_back(i);
    }
    return out;
}

std::vector<ADS_MGMT_TABLE_INFO> MgCollector::open_tables() const {
    std::vector<ADS_MGMT_TABLE_INFO> out;
    out.reserve(snapshot_.table_list.size());
    for (const auto& t : snapshot_.table_list) {
        ADS_MGMT_TABLE_INFO i;
        std::memset(&i, 0, sizeof(i));
        put_field(i.aucTableName, sizeof(i.aucTableName), t.name);
        put_field(i.aucUserName, sizeof(i.aucUserName), t.user);
        i.usConnNumber = t.conn_no;
        i.usOpenMode   = t.open_mode;
        i.usLockType   = t.lock_type;
        out.push_back(i);
    }
    return out;
}

std::vector<ADS_MGMT_INDEX_INFO> MgCollector::open_indexes() const {
    std::vector<ADS_MGMT_INDEX_INFO> out;
    out.reserve(snapshot_.index_list.size());
    for (const auto& x : snapshot_.index_list) {
        ADS_MGMT_INDEX_INFO i;
        std::memset(&i, 0, sizeof(i));
        put_field(i.aucIndexName, sizeof(i.aucIndexName), x.name);
        put_field(i.aucTagName, sizeof(i.aucTagName), x.tag);
        put_field(i.aucExpression, sizeof(i.aucExpression),
                  x.expression);
        out.push_back(i);
    }
    return out;
}

std::vector<ADS_MGMT_LOCK_INFO> MgCollector::locks() const {
    std::vector<ADS_MGMT_LOCK_INFO> out;
    out.reserve(snapshot_.lock_list.size());
    for (const auto& l : snapshot_.lock_list) {
        ADS_MGMT_LOCK_INFO i;
        std::memset(&i, 0, sizeof(i));
        put_field(i.aucUserName, sizeof(i.aucUserName), l.user);
        i.usConnNumber   = l.conn_no;
        i.ulRecordNumber = l.recno;
        out.push_back(i);
    }
    return out;
}

std::vector<ADS_MGMT_THREAD_ACTIVITY>
MgCollector::worker_thread_activity() const {
    std::vector<ADS_MGMT_THREAD_ACTIVITY> out;
    out.reserve(snapshot_.thread_list.size());
    for (const auto& t : snapshot_.thread_list) {
        ADS_MGMT_THREAD_ACTIVITY i;
        std::memset(&i, 0, sizeof(i));
        i.ulThreadNumber = t.thread_no;
        i.usOpCode       = t.opcode;
        i.usConnNumber   = t.conn_no;
        put_field(i.aucUserName, sizeof(i.aucUserName), t.user);
        put_field(i.aucOSUserLoginName,
                  sizeof(i.aucOSUserLoginName), t.os_login);
        out.push_back(i);
    }
    return out;
}

ADS_MGMT_LOCK_INFO MgCollector::lock_owner(std::uint32_t recno) const {
    ADS_MGMT_LOCK_INFO i;
    std::memset(&i, 0, sizeof(i));
    for (const auto& l : snapshot_.lock_list) {
        if (l.recno == recno) {
            put_field(i.aucUserName, sizeof(i.aucUserName), l.user);
            i.usConnNumber   = l.conn_no;
            i.ulRecordNumber = l.recno;
            break;
        }
    }
    return i;
}
```

- [ ] **Step 4: Run the test, verify it passes**

Run: `cmake --build build/default --target openads_unit_tests && ./build/default/tests/openads_unit_tests -tc="MgCollector list accessors map snapshot vectors"`
Expected: PASS.

- [ ] **Step 5: Run the whole suite**

Run: `ctest --test-dir build/default --output-on-failure`
Expected: all tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/mgmt/mg_collector.cpp tests/unit/mg_collector_test.cpp
git commit -m "feat(mgmt): MgCollector list accessors + lock_owner"
```

---

## Phase 2 — mg_wire serialization

### Task 8: mg_wire header + MgRequestKind

**Files:**
- Create: `src/network/mg_wire.h`
- Create: `src/network/mg_wire.cpp`
- Modify: `src/CMakeLists.txt` (add `network/mg_wire.cpp`)

- [ ] **Step 1: Write `src/network/mg_wire.h`**

```cpp
#pragma once

#include "mgmt/mg_snapshot.h"
#include "util/result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openads::network {

// Selects which telemetry the server should collect for a MgRequest.
enum class MgRequestKind : std::uint8_t {
    Snapshot       = 0x01,  // full MgSnapshot (covers every Get*)
    KillUser       = 0x02,  // arg: u16 conn_no
    ResetCommStats = 0x03,
    DumpTables     = 0x04,
};

// Request payload: [u8 kind][optional args].
std::string encode_mg_request(MgRequestKind kind, std::uint16_t arg);

struct MgRequest {
    MgRequestKind kind = MgRequestKind::Snapshot;
    std::uint16_t arg  = 0;
};
util::Result<MgRequest> decode_mg_request(const std::string& payload);

// Reply payload: a fully serialized MgSnapshot, little-endian.
std::string encode_mg_snapshot(const mgmt::MgSnapshot& snap);
util::Result<mgmt::MgSnapshot> decode_mg_snapshot(
    const std::string& payload);

}  // namespace openads::network
```

- [ ] **Step 2: Write `src/network/mg_wire.cpp`**

```cpp
#include "network/mg_wire.h"

#include <cstring>

namespace openads::network {

namespace {

void put_u16(std::string& b, std::uint16_t v) {
    b.push_back(static_cast<char>(v & 0xFF));
    b.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void put_u32(std::string& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void put_u64(std::string& b, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void put_str(std::string& b, const std::string& s) {
    put_u16(b, static_cast<std::uint16_t>(s.size()));
    b.append(s);
}

// Cursor-based reader; sets ok=false on overrun.
struct Reader {
    const std::string& b;
    std::size_t        pos = 0;
    bool               ok  = true;

    std::uint16_t u16() {
        if (pos + 2 > b.size()) { ok = false; return 0; }
        std::uint16_t v = static_cast<std::uint8_t>(b[pos]) |
                          (static_cast<std::uint16_t>(
                               static_cast<std::uint8_t>(b[pos + 1])) << 8);
        pos += 2;
        return v;
    }
    std::uint32_t u32() {
        if (pos + 4 > b.size()) { ok = false; return 0; }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t>(
                     static_cast<std::uint8_t>(b[pos + i])) << (8 * i);
        pos += 4;
        return v;
    }
    std::uint64_t u64() {
        if (pos + 8 > b.size()) { ok = false; return 0; }
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= static_cast<std::uint64_t>(
                     static_cast<std::uint8_t>(b[pos + i])) << (8 * i);
        pos += 8;
        return v;
    }
    std::string str() {
        std::uint16_t n = u16();
        if (!ok || pos + n > b.size()) { ok = false; return {}; }
        std::string s = b.substr(pos, n);
        pos += n;
        return s;
    }
};

void put_tp(std::string& b,
            const std::chrono::system_clock::time_point& tp) {
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                    tp.time_since_epoch()).count();
    put_u64(b, static_cast<std::uint64_t>(secs));
}
std::chrono::system_clock::time_point get_tp(Reader& r) {
    return std::chrono::system_clock::time_point{
        std::chrono::seconds{static_cast<long long>(r.u64())}};
}

}  // namespace

std::string encode_mg_request(MgRequestKind kind, std::uint16_t arg) {
    std::string b;
    b.push_back(static_cast<char>(kind));
    put_u16(b, arg);
    return b;
}

util::Result<MgRequest> decode_mg_request(const std::string& payload) {
    if (payload.size() < 3)
        return util::Result<MgRequest>::failure("short mg request");
    MgRequest req;
    req.kind = static_cast<MgRequestKind>(
        static_cast<std::uint8_t>(payload[0]));
    req.arg = static_cast<std::uint8_t>(payload[1]) |
              (static_cast<std::uint16_t>(
                   static_cast<std::uint8_t>(payload[2])) << 8);
    return util::Result<MgRequest>::success(req);
}

std::string encode_mg_snapshot(const mgmt::MgSnapshot& s) {
    std::string b;
    put_u32(b, s.users);
    put_u32(b, s.connections);
    put_u32(b, s.workareas);
    put_u32(b, s.tables);
    put_u32(b, s.indexes);
    put_u32(b, s.locks);
    put_u32(b, s.worker_threads);
    put_u16(b, s.server_type);

    put_u32(b, static_cast<std::uint32_t>(s.user_list.size()));
    for (const auto& u : s.user_list) {
        put_str(b, u.name);
        put_str(b, u.address);
        put_str(b, u.os_login);
        put_u16(b, u.conn_no);
        put_tp(b, u.connected_at);
    }
    put_u32(b, static_cast<std::uint32_t>(s.table_list.size()));
    for (const auto& t : s.table_list) {
        put_str(b, t.name);
        put_str(b, t.user);
        put_u16(b, t.conn_no);
        put_u16(b, t.open_mode);
        put_u16(b, t.lock_type);
    }
    put_u32(b, static_cast<std::uint32_t>(s.index_list.size()));
    for (const auto& x : s.index_list) {
        put_str(b, x.name);
        put_str(b, x.tag);
        put_str(b, x.expression);
    }
    put_u32(b, static_cast<std::uint32_t>(s.lock_list.size()));
    for (const auto& l : s.lock_list) {
        put_str(b, l.user);
        put_u16(b, l.conn_no);
        put_u32(b, l.recno);
    }
    put_u32(b, static_cast<std::uint32_t>(s.thread_list.size()));
    for (const auto& t : s.thread_list) {
        put_u32(b, t.thread_no);
        put_u16(b, t.opcode);
        put_str(b, t.user);
        put_u16(b, t.conn_no);
        put_str(b, t.os_login);
    }
    return b;
}

util::Result<mgmt::MgSnapshot> decode_mg_snapshot(
    const std::string& payload) {
    Reader r{payload};
    mgmt::MgSnapshot s;
    s.users          = r.u32();
    s.connections    = r.u32();
    s.workareas      = r.u32();
    s.tables         = r.u32();
    s.indexes        = r.u32();
    s.locks          = r.u32();
    s.worker_threads = r.u32();
    s.server_type    = r.u16();

    std::uint32_t nu = r.u32();
    for (std::uint32_t i = 0; r.ok && i < nu; ++i) {
        mgmt::MgUser u;
        u.name         = r.str();
        u.address      = r.str();
        u.os_login     = r.str();
        u.conn_no      = r.u16();
        u.connected_at = get_tp(r);
        s.user_list.push_back(std::move(u));
    }
    std::uint32_t nt = r.u32();
    for (std::uint32_t i = 0; r.ok && i < nt; ++i) {
        mgmt::MgTable t;
        t.name      = r.str();
        t.user      = r.str();
        t.conn_no   = r.u16();
        t.open_mode = r.u16();
        t.lock_type = r.u16();
        s.table_list.push_back(std::move(t));
    }
    std::uint32_t nx = r.u32();
    for (std::uint32_t i = 0; r.ok && i < nx; ++i) {
        mgmt::MgIndex x;
        x.name       = r.str();
        x.tag        = r.str();
        x.expression = r.str();
        s.index_list.push_back(std::move(x));
    }
    std::uint32_t nl = r.u32();
    for (std::uint32_t i = 0; r.ok && i < nl; ++i) {
        mgmt::MgLock l;
        l.user    = r.str();
        l.conn_no = r.u16();
        l.recno   = r.u32();
        s.lock_list.push_back(std::move(l));
    }
    std::uint32_t nh = r.u32();
    for (std::uint32_t i = 0; r.ok && i < nh; ++i) {
        mgmt::MgThread t;
        t.thread_no = r.u32();
        t.opcode    = r.u16();
        t.user      = r.str();
        t.conn_no   = r.u16();
        t.os_login  = r.str();
        s.thread_list.push_back(std::move(t));
    }
    if (!r.ok)
        return util::Result<mgmt::MgSnapshot>::failure(
            "corrupt mg snapshot");
    return util::Result<mgmt::MgSnapshot>::success(std::move(s));
}

}  // namespace openads::network
```

> **Note:** `mg_wire.cpp` uses `<chrono>` via `mg_snapshot.h`. If the
> compiler flags a missing include, add `#include <chrono>` at the top.
> Verify the `util::Result<T>` factory names (`success` / `failure`)
> against `src/util/result.h` before Step 4 — if they differ (e.g.
> `ok` / `err`), use the actual names consistently.

- [ ] **Step 2b: Add to the build**

In `src/CMakeLists.txt`, after `    mgmt/mg_collector.cpp` (added in Task 4), add:

```cmake
    network/mg_wire.cpp
```

- [ ] **Step 3: Verify it compiles**

Run: `cmake --build build/default --target openads_core`
Expected: PASS (compiles; no tests yet).

- [ ] **Step 4: Commit**

```bash
git add src/network/mg_wire.h src/network/mg_wire.cpp src/CMakeLists.txt
git commit -m "feat(network): mg_wire snapshot/request serialization"
```

### Task 9: mg_wire round-trip tests

**Files:**
- Create: `tests/unit/mg_wire_test.cpp` (already added to `tests/CMakeLists.txt` in Task 4)

- [ ] **Step 1: Write the test**

```cpp
#include "doctest.h"

#include "network/mg_wire.h"

using namespace openads;

TEST_CASE("mg_wire snapshot round-trips identically") {
    mgmt::MgSnapshot in;
    in.users = 2; in.connections = 2; in.workareas = 4;
    in.tables = 3; in.indexes = 1; in.locks = 1;
    in.worker_threads = 5; in.server_type = 0;

    mgmt::MgUser u;
    u.name = "bob"; u.address = "1.2.3.4:9"; u.os_login = "bob";
    u.conn_no = 7;
    in.user_list.push_back(u);

    mgmt::MgTable t;
    t.name = "t.adt"; t.user = "bob"; t.conn_no = 7;
    t.open_mode = 1; t.lock_type = 2;
    in.table_list.push_back(t);

    mgmt::MgLock l;
    l.user = "bob"; l.conn_no = 7; l.recno = 99;
    in.lock_list.push_back(l);

    std::string blob = network::encode_mg_snapshot(in);
    auto out = network::decode_mg_snapshot(blob);
    REQUIRE(out.ok());

    const mgmt::MgSnapshot& s = out.value();
    CHECK(s.connections == 2);
    CHECK(s.worker_threads == 5);
    REQUIRE(s.user_list.size() == 1);
    CHECK(s.user_list[0].name == "bob");
    CHECK(s.user_list[0].conn_no == 7);
    REQUIRE(s.table_list.size() == 1);
    CHECK(s.table_list[0].name == "t.adt");
    CHECK(s.table_list[0].lock_type == 2);
    REQUIRE(s.lock_list.size() == 1);
    CHECK(s.lock_list[0].recno == 99);
}

TEST_CASE("mg_wire request round-trips") {
    std::string blob = network::encode_mg_request(
        network::MgRequestKind::KillUser, 13);
    auto req = network::decode_mg_request(blob);
    REQUIRE(req.ok());
    CHECK(req.value().kind == network::MgRequestKind::KillUser);
    CHECK(req.value().arg == 13);
}

TEST_CASE("mg_wire rejects a truncated snapshot") {
    mgmt::MgSnapshot in;
    in.user_list.resize(3);   // claims 3 users, body absent
    std::string blob = network::encode_mg_snapshot(in);
    blob.resize(10);          // chop the body
    auto out = network::decode_mg_snapshot(blob);
    CHECK_FALSE(out.ok());
}
```

> **Note:** `out.ok()` / `out.value()` assume the `util::Result`
> accessor names. Match them to `src/util/result.h`.

- [ ] **Step 2: Run the test, verify it fails then passes**

Run: `cmake --build build/default --target openads_unit_tests && ./build/default/tests/openads_unit_tests -tc="mg_wire*"`
Expected: PASS (implementation already exists from Task 8 — this task only adds coverage; if the round-trip reveals a bug, fix `mg_wire.cpp` before committing).

- [ ] **Step 3: Commit**

```bash
git add tests/unit/mg_wire_test.cpp
git commit -m "test(network): mg_wire serialization round-trip"
```

---

## Phase 3 — wire opcodes + server MgRequest handler

### Task 10: New wire opcodes

**Files:**
- Modify: `src/network/wire.h:149-151`

- [ ] **Step 1: Add opcodes**

In `src/network/wire.h`, the `enum class Opcode : std::uint8_t`,
change the tail (currently ending `GetLastTableUpdateAck = 0x9F;`
then `Error = 0xFF;`) to insert before `Error`:

```cpp
    GetLastTableUpdate    = 0x9E,
    GetLastTableUpdateAck = 0x9F,

    // M9.25 — management telemetry channel.
    MgConnect          = 0xA0,
    MgConnectAck       = 0xA1,
    MgRequest          = 0xA2,
    MgReplyAck         = 0xA3,

    Error              = 0xFF,
```

- [ ] **Step 2: Verify it compiles**

Run: `cmake --build build/default --target openads_core`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add src/network/wire.h
git commit -m "feat(network): MgConnect/MgRequest wire opcodes"
```

### Task 11: Server-side snapshot builder

**Files:**
- Modify: `src/network/server.h` — declare `build_mg_snapshot`
- Modify: `src/network/server.cpp` — implement it

- [ ] **Step 1: Declare the builder** — in `server.h`, inside the
  `public:` block near `sessions_snapshot()` (around line 66), add:

```cpp
    // M9.25 — build a management telemetry snapshot from the live
    // session registry. Used by the MgRequest opcode handler.
    mgmt::MgSnapshot build_mg_snapshot() const;
```

Add `#include "mgmt/mg_snapshot.h"` to the `server.h` include block.

- [ ] **Step 2: Write the failing test** — append to
  `tests/unit/network_server_test.cpp`:

```cpp
TEST_CASE("Server::build_mg_snapshot counts live sessions") {
    using openads::network::Server;
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).ok());

    Server::SessionInfo a;
    a.peer_ip = "127.0.0.1"; a.peer_port = 5001;
    a.user = "alice"; a.open_tables = 2;
    std::uint64_t id = srv.register_session(a);

    auto snap = srv.build_mg_snapshot();
    CHECK(snap.connections == 1);
    CHECK(snap.tables == 2);
    REQUIRE(snap.user_list.size() == 1);
    CHECK(snap.user_list[0].name == "alice");

    srv.unregister_session(id);
    srv.stop();
}
```

- [ ] **Step 3: Run the test, verify it fails**

Run: `cmake --build build/default --target openads_unit_tests`
Expected: FAIL — `build_mg_snapshot` undefined.

- [ ] **Step 4: Implement** — in `server.cpp`, add (near
  `sessions_snapshot`):

```cpp
mgmt::MgSnapshot Server::build_mg_snapshot() const {
    mgmt::MgSnapshot snap;
    auto sessions = sessions_snapshot();

    snap.connections = static_cast<std::uint32_t>(sessions.size());
    snap.server_type = 1;   // 1 = remote server

    {
        std::lock_guard<std::mutex> g(sessions_mu_);
        snap.worker_threads =
            static_cast<std::uint32_t>(sessions_.size());
    }

    std::uint32_t conn_no = 1;
    for (const auto& s : sessions) {
        mgmt::MgUser u;
        u.name    = s.user.empty() ? "(anonymous)" : s.user;
        u.address = s.peer_ip + ":" + std::to_string(s.peer_port);
        u.os_login     = u.name;
        u.conn_no      = static_cast<std::uint16_t>(conn_no);
        u.connected_at = s.connected_at;
        snap.user_list.push_back(std::move(u));

        snap.tables    += s.open_tables;
        snap.workareas += s.open_tables;
        ++conn_no;
    }
    snap.users = static_cast<std::uint32_t>(snap.user_list.size());
    return snap;
}
```

> **Note:** `sessions_mu_` is `private` and `build_mg_snapshot` is a
> `const` member of the same class — direct access is fine. If
> `sessions_mu_` is not `mutable`, mark it `mutable` (the existing
> `info_mu_` already is — follow that pattern).

- [ ] **Step 5: Run the test, verify it passes**

Run: `cmake --build build/default --target openads_unit_tests && ./build/default/tests/openads_unit_tests -tc="Server::build_mg_snapshot counts live sessions"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/network/server.h src/network/server.cpp tests/unit/network_server_test.cpp
git commit -m "feat(network): Server::build_mg_snapshot from session registry"
```

### Task 12: Server MgConnect / MgRequest dispatch

**Files:**
- Modify: `src/network/server.cpp` — `session_loop` opcode dispatch

- [ ] **Step 1: Locate the dispatch** — in `server.cpp`, `session_loop`
  has a dispatch over `frame.opcode`. Find where unknown opcodes fall
  through to the `Error` reply.

- [ ] **Step 2: Add the handlers** — before the unknown-opcode fallback,
  add cases:

```cpp
        case Opcode::MgConnect: {
            // Management handshake — no payload needed; reply with an
            // ack so the client can register its mgmt handle.
            Frame ack;
            ack.opcode  = Opcode::MgConnectAck;
            ack.payload = "mg-ok";
            write_frame(s, ack);
            break;
        }
        case Opcode::MgRequest: {
            auto req = decode_mg_request(frame.payload);
            Frame ack;
            if (!req.ok()) {
                ack.opcode  = Opcode::Error;
                ack.payload = "bad mg request";
                write_frame(s, ack);
                break;
            }
            switch (req.value().kind) {
                case MgRequestKind::Snapshot: {
                    ack.opcode  = Opcode::MgReplyAck;
                    ack.payload =
                        encode_mg_snapshot(build_mg_snapshot());
                    break;
                }
                case MgRequestKind::KillUser: {
                    // arg is the 1-based connection number; map it to
                    // the matching session id and kill it.
                    kill_session_by_conn_no(req.value().arg);
                    ack.opcode  = Opcode::MgReplyAck;
                    ack.payload = "";
                    break;
                }
                case MgRequestKind::ResetCommStats: {
                    mgmt::process_mg_stats().reset_comm();
                    ack.opcode  = Opcode::MgReplyAck;
                    ack.payload = "";
                    break;
                }
                case MgRequestKind::DumpTables: {
                    ack.opcode  = Opcode::MgReplyAck;
                    ack.payload = "";
                    break;
                }
            }
            write_frame(s, ack);
            break;
        }
```

Add the includes at the top of `server.cpp`:

```cpp
#include "mgmt/mg_stats.h"
#include "network/mg_wire.h"
```

- [ ] **Step 3: Add `kill_session_by_conn_no`** — `build_mg_snapshot`
  assigns `conn_no` as the 1-based index into `sessions_snapshot()`
  order. Add a private helper to `server.h`:

```cpp
    // Kill the session whose 1-based position in sessions_snapshot()
    // order equals conn_no. Returns true if one was found.
    bool kill_session_by_conn_no(std::uint16_t conn_no);
```

and implement in `server.cpp`:

```cpp
bool Server::kill_session_by_conn_no(std::uint16_t conn_no) {
    auto sessions = sessions_snapshot();
    if (conn_no == 0 || conn_no > sessions.size()) return false;
    return kill_session(sessions[conn_no - 1].id);
}
```

- [ ] **Step 4: Verify it compiles**

Run: `cmake --build build/default --target openads_core`
Expected: PASS.

- [ ] **Step 5: Run the full suite**

Run: `ctest --test-dir build/default --output-on-failure`
Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git add src/network/server.h src/network/server.cpp
git commit -m "feat(network): server MgConnect/MgRequest dispatch"
```

---

## Phase 4 — client telemetry backends + real AdsMg* exports

### Task 13: Telemetry backend types in ace_exports.cpp

**Files:**
- Modify: `src/abi/ace_exports.cpp`

- [ ] **Step 1: Add the backend scaffolding** — in `ace_exports.cpp`,
  in the anonymous namespace near the other ABI helpers, add:

```cpp
#include "mgmt/mg_collector.h"
#include "mgmt/mg_stats.h"
#include "network/client.h"
#include "network/mg_wire.h"

namespace {

// A management handle resolves to one of these. Local collects the
// in-process snapshot; Remote ships a MgRequest to the server.
struct MgBackend {
    bool                          remote = false;
    std::string                   host;       // remote only
    std::uint16_t                 port = 0;   // remote only
    // For local mode there is no live engine-wide registry yet, so the
    // snapshot describes just this process (1 connection, no tables).
};

// Registry of open mgmt handles. ADSHANDLE values for mgmt start at a
// high base so they never collide with table/connection handles.
std::mutex                                  g_mg_mu;
std::unordered_map<ADSHANDLE, MgBackend>    g_mg_handles;
ADSHANDLE                                   g_mg_next = 0x4D670001;  // 'Mg'

// Builds a MgSnapshot for whichever backend the handle names.
openads::util::Result<openads::mgmt::MgSnapshot>
fetch_mg_snapshot(const MgBackend& be) {
    if (!be.remote) {
        // Local mode: report this process. One connection (this one),
        // no server-side session registry to enumerate.
        openads::mgmt::MgSnapshot snap;
        snap.connections = 1;
        snap.users       = 1;
        snap.server_type = 0;   // 0 = local
        openads::mgmt::MgUser u;
        u.name = "(local)"; u.conn_no = 1;
        snap.user_list.push_back(u);
        return openads::util::Result<openads::mgmt::MgSnapshot>
            ::success(std::move(snap));
    }
    // Remote mode: connect, MgConnect handshake, MgRequest snapshot.
    openads::network::Client cli;
    auto conn = cli.connect(be.host, be.port);
    if (!conn.ok())
        return openads::util::Result<openads::mgmt::MgSnapshot>
            ::failure("mg connect failed");
    auto reply = cli.request(
        openads::network::Opcode::MgRequest,
        openads::network::encode_mg_request(
            openads::network::MgRequestKind::Snapshot, 0));
    if (!reply.ok())
        return openads::util::Result<openads::mgmt::MgSnapshot>
            ::failure("mg request failed");
    return openads::network::decode_mg_snapshot(reply.value());
}

}  // namespace
```

> **Note:** `openads::network::Client` is the existing remote client
> (`src/network/client.h`). Inspect its real API before Step 1 —
> method names (`connect`, `request`) and return shapes here are the
> expected pattern; adjust to the actual `Client` interface. If
> `Client` has no generic `request(opcode, payload)`, add one in a
> small preliminary task, or use the lower-level `write_frame` /
> `read_frame` helpers from `server.h` directly against a `Socket`.

- [ ] **Step 2: Verify it compiles**

Run: `cmake --build build/default --target openads_core`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add src/abi/ace_exports.cpp
git commit -m "feat(abi): MgBackend telemetry handle registry"
```

### Task 14: Real AdsMgConnect / AdsMgDisconnect

**Files:**
- Modify: `src/abi/ace_exports.cpp:9773-9775`

- [ ] **Step 1: Replace the stubs**

Replace `AdsMgConnect` and `AdsMgDisconnect` (lines 9773-9775) with:

```cpp
// AdsMgConnect — pucServer selects local vs. remote. An empty or
// "local" server string yields a local-process backend; anything of
// the form "host" or "host:port" yields a remote backend (default
// port 16262, the OpenADS server port).
UNSIGNED32 AdsMgConnect(UNSIGNED8* pucServer, UNSIGNED8* /*pucUser*/,
                        UNSIGNED8* /*pucPwd*/, ADSHANDLE* phMgmt) {
    if (phMgmt == nullptr) return openads::AE_INVALID_OPTION;

    MgBackend be;
    std::string srv = pucServer
        ? reinterpret_cast<const char*>(pucServer) : "";
    // Strip leading UNC backslashes ("\\\\host\\").
    while (!srv.empty() && (srv.front() == '\\' || srv.front() == '/'))
        srv.erase(srv.begin());
    while (!srv.empty() && (srv.back() == '\\' || srv.back() == '/'))
        srv.pop_back();

    if (srv.empty() ||
        srv == "local" || srv == "LOCAL") {
        be.remote = false;
    } else {
        be.remote = true;
        auto colon = srv.find(':');
        if (colon == std::string::npos) {
            be.host = srv;
            be.port = 16262;
        } else {
            be.host = srv.substr(0, colon);
            be.port = static_cast<std::uint16_t>(
                std::strtoul(srv.c_str() + colon + 1, nullptr, 10));
        }
    }

    std::lock_guard<std::mutex> g(g_mg_mu);
    ADSHANDLE h = g_mg_next++;
    g_mg_handles.emplace(h, std::move(be));
    *phMgmt = h;
    return openads::AE_SUCCESS;
}

UNSIGNED32 AdsMgDisconnect(ADSHANDLE hMgmt) {
    std::lock_guard<std::mutex> g(g_mg_mu);
    g_mg_handles.erase(hMgmt);
    return openads::AE_SUCCESS;
}
```

- [ ] **Step 2: Run the existing mgmt test**

Run: `cmake --build build/default --target openads_unit_tests && ./build/default/tests/openads_unit_tests -tc="M9.24 AdsMgConnect produces a synthetic mgmt handle"`
Expected: PASS — handle is non-zero, disconnect succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/abi/ace_exports.cpp
git commit -m "feat(abi): real AdsMgConnect/AdsMgDisconnect with backend registry"
```

### Task 15: Real AdsMg Get* exports

**Files:**
- Modify: `src/abi/ace_exports.cpp:9779-9818` and `:10132`

- [ ] **Step 1: Add a struct-copy helper** — in the anonymous namespace
  near `fetch_mg_snapshot`:

```cpp
namespace {

// Resolve a mgmt handle to its backend; nullptr if unknown.
const MgBackend* lookup_mg(ADSHANDLE h) {
    std::lock_guard<std::mutex> g(g_mg_mu);
    auto it = g_mg_handles.find(h);
    return it == g_mg_handles.end() ? nullptr : &it->second;
}

// Copy a POD struct into the caller's buffer, clamped to *pusLen, and
// write back the real struct size. Matches the ace.h size-in/out
// convention used elsewhere.
template <typename T>
UNSIGNED32 emit_mg_struct(const T& src, void* pBuf, UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return openads::AE_INVALID_OPTION;
    UNSIGNED16 cap = *pusLen;
    UNSIGNED16 n = static_cast<UNSIGNED16>(
        sizeof(T) < cap ? sizeof(T) : cap);
    if (pBuf != nullptr && n > 0) std::memcpy(pBuf, &src, n);
    *pusLen = static_cast<UNSIGNED16>(sizeof(T));
    return openads::AE_SUCCESS;
}

// Build a MgCollector for a handle, or return the error code.
openads::util::Result<openads::mgmt::MgCollector>
mg_collector_for(ADSHANDLE h) {
    const MgBackend* be = lookup_mg(h);
    if (be == nullptr)
        return openads::util::Result<openads::mgmt::MgCollector>
            ::failure("invalid mgmt handle");
    auto snap = fetch_mg_snapshot(*be);
    if (!snap.ok())
        return openads::util::Result<openads::mgmt::MgCollector>
            ::failure(snap.error());
    return openads::util::Result<openads::mgmt::MgCollector>
        ::success(openads::mgmt::MgCollector(
            snap.value(), openads::mgmt::process_mg_stats()));
}

}  // namespace
```

> **Note:** if `util::Result<T>` cannot hold a non-default-constructible
> `MgCollector`, change `mg_collector_for` to return
> `std::optional<MgCollector>` plus an out `UNSIGNED32*` error, or give
> `MgCollector` a private default ctor used only by `Result`. Pick one
> and apply it consistently.

- [ ] **Step 2: Replace the Get* stubs**

Replace `AdsMgGetActivityInfo`, `AdsMgGetCommStats`,
`AdsMgGetConfigInfo`, `AdsMgGetInstallInfo`, `AdsMgGetServerType`
(lines 9779-9813 region) with:

```cpp
UNSIGNED32 AdsMgGetActivityInfo(ADSHANDLE h, void* p, UNSIGNED16* l) {
    auto c = mg_collector_for(h);
    if (!c.ok()) return openads::AE_INVALID_HANDLE;
    return emit_mg_struct(c.value().activity_info(), p, l);
}
UNSIGNED32 AdsMgGetCommStats(ADSHANDLE h, void* p, UNSIGNED16* l) {
    auto c = mg_collector_for(h);
    if (!c.ok()) return openads::AE_INVALID_HANDLE;
    return emit_mg_struct(c.value().comm_stats(), p, l);
}
UNSIGNED32 AdsMgGetConfigInfo(ADSHANDLE h, void* pv, UNSIGNED16* lv,
                              void* pm, UNSIGNED16* lm) {
    auto c = mg_collector_for(h);
    if (!c.ok()) return openads::AE_INVALID_HANDLE;
    UNSIGNED32 rc = emit_mg_struct(c.value().config_params(), pv, lv);
    if (rc != openads::AE_SUCCESS) return rc;
    return emit_mg_struct(c.value().config_memory(), pm, lm);
}
UNSIGNED32 AdsMgGetInstallInfo(ADSHANDLE h, void* p, UNSIGNED16* l) {
    auto c = mg_collector_for(h);
    if (!c.ok()) return openads::AE_INVALID_HANDLE;
    return emit_mg_struct(c.value().install_info(), p, l);
}
UNSIGNED32 AdsMgGetServerType(ADSHANDLE h, UNSIGNED16* p) {
    auto c = mg_collector_for(h);
    if (!c.ok()) return openads::AE_INVALID_HANDLE;
    if (p) *p = c.value().server_type();
    return openads::AE_SUCCESS;
}
```

- [ ] **Step 3: Replace the list-valued Get* stubs**

Replace `AdsMgGetUserNames`, `AdsMgGetOpenTables`,
`AdsMgGetOpenTables2`, `AdsMgGetOpenIndexes`, `AdsMgGetLocks`,
`AdsMgGetLockOwner`, `AdsMgGetWorkerThreadActivity` with versions that
fill an array buffer. Add a generic array emitter to the anonymous
namespace:

```cpp
namespace {
// Copy a vector of POD structs into the caller's array buffer.
// *pusCount in: capacity (#elements); out: actual element count.
// pusSize (optional) out: sizeof(element).
template <typename T>
UNSIGNED32 emit_mg_array(const std::vector<T>& src, void* pBuf,
                         UNSIGNED16* pusCount, UNSIGNED16* pusSize) {
    if (pusCount == nullptr) return openads::AE_INVALID_OPTION;
    UNSIGNED16 cap = *pusCount;
    UNSIGNED16 n = static_cast<UNSIGNED16>(
        src.size() < cap ? src.size() : cap);
    if (pBuf != nullptr && n > 0)
        std::memcpy(pBuf, src.data(), static_cast<std::size_t>(n) *
                    sizeof(T));
    *pusCount = static_cast<UNSIGNED16>(src.size());
    if (pusSize != nullptr) *pusSize = static_cast<UNSIGNED16>(sizeof(T));
    return openads::AE_SUCCESS;
}
}  // namespace
```

Then:

```cpp
UNSIGNED32 AdsMgGetUserNames(ADSHANDLE h, UNSIGNED8* /*pucFile*/,
                             void* p, UNSIGNED16* c, UNSIGNED16* sz) {
    auto col = mg_collector_for(h);
    if (!col.ok()) return openads::AE_INVALID_HANDLE;
    return emit_mg_array(col.value().user_names(), p, c, sz);
}
UNSIGNED32 AdsMgGetOpenTables(ADSHANDLE h, UNSIGNED8* /*f*/,
                              UNSIGNED16 /*o*/, void* p,
                              UNSIGNED16* c, UNSIGNED16* sz) {
    auto col = mg_collector_for(h);
    if (!col.ok()) return openads::AE_INVALID_HANDLE;
    return emit_mg_array(col.value().open_tables(), p, c, sz);
}
UNSIGNED32 AdsMgGetOpenTables2(ADSHANDLE h, UNSIGNED8* /*f*/,
                               UNSIGNED16 /*o*/, void* p,
                               UNSIGNED16* c, UNSIGNED16* sz) {
    auto col = mg_collector_for(h);
    if (!col.ok()) return openads::AE_INVALID_HANDLE;
    return emit_mg_array(col.value().open_tables(), p, c, sz);
}
UNSIGNED32 AdsMgGetOpenIndexes(ADSHANDLE h, UNSIGNED8* /*f*/,
                               UNSIGNED8* /*t*/, UNSIGNED16 /*o*/,
                               void* p, UNSIGNED16* c,
                               UNSIGNED16* sz) {
    auto col = mg_collector_for(h);
    if (!col.ok()) return openads::AE_INVALID_HANDLE;
    return emit_mg_array(col.value().open_indexes(), p, c, sz);
}
UNSIGNED32 AdsMgGetLocks(ADSHANDLE h, UNSIGNED8* /*f*/,
                         UNSIGNED8* /*t*/, UNSIGNED16 /*o*/,
                         void* p, UNSIGNED16* c, UNSIGNED16* sz) {
    auto col = mg_collector_for(h);
    if (!col.ok()) return openads::AE_INVALID_HANDLE;
    return emit_mg_array(col.value().locks(), p, c, sz);
}
UNSIGNED32 AdsMgGetLockOwner(ADSHANDLE h, UNSIGNED8* /*t*/,
                             UNSIGNED32 ulRec, void* p,
                             UNSIGNED16* l, UNSIGNED16* lt) {
    auto col = mg_collector_for(h);
    if (!col.ok()) return openads::AE_INVALID_HANDLE;
    if (lt) *lt = ADS_MGMT_RECORD_LOCK;
    return emit_mg_struct(col.value().lock_owner(ulRec), p, l);
}
UNSIGNED32 AdsMgGetWorkerThreadActivity(ADSHANDLE h, void* p,
                                        UNSIGNED16* c,
                                        UNSIGNED16* sz) {
    auto col = mg_collector_for(h);
    if (!col.ok()) return openads::AE_INVALID_HANDLE;
    return emit_mg_array(col.value().worker_thread_activity(),
                         p, c, sz);
}
```

- [ ] **Step 4: Replace the mutator stubs**

```cpp
UNSIGNED32 AdsMgKillUser(ADSHANDLE h, UNSIGNED8* /*pucUser*/,
                         UNSIGNED16 usConnNo) {
    const MgBackend* be = lookup_mg(h);
    if (be == nullptr) return openads::AE_INVALID_HANDLE;
    if (!be->remote) return openads::AE_SUCCESS;   // no-op local
    openads::network::Client cli;
    auto conn = cli.connect(be->host, be->port);
    if (!conn.ok()) return openads::AE_CONNECTION_FAILED;
    cli.request(openads::network::Opcode::MgRequest,
                openads::network::encode_mg_request(
                    openads::network::MgRequestKind::KillUser,
                    usConnNo));
    return openads::AE_SUCCESS;
}
UNSIGNED32 AdsMgResetCommStats(ADSHANDLE h) {
    const MgBackend* be = lookup_mg(h);
    if (be == nullptr) return openads::AE_INVALID_HANDLE;
    if (!be->remote) {
        openads::mgmt::process_mg_stats().reset_comm();
        return openads::AE_SUCCESS;
    }
    openads::network::Client cli;
    auto conn = cli.connect(be->host, be->port);
    if (!conn.ok()) return openads::AE_CONNECTION_FAILED;
    cli.request(openads::network::Opcode::MgRequest,
                openads::network::encode_mg_request(
                    openads::network::MgRequestKind::ResetCommStats, 0));
    return openads::AE_SUCCESS;
}
```

And `AdsMgDumpInternalTables` (line 10132):

```cpp
UNSIGNED32 AdsMgDumpInternalTables(ADSHANDLE h) {
    return lookup_mg(h) ? openads::AE_SUCCESS
                        : openads::AE_INVALID_HANDLE;
}
```

> **Note:** `AdsMgKillUser`'s real ace.h signature takes a connection
> number, not a user-name string — confirm against
> `include/openads/ace.h` and the `abi_mgmt_test.cpp` declaration
> (which currently shows `UNSIGNED8* pucUser`). If the canonical
> signature is the name string, resolve the name to a conn number
> client-side from a prior `user_names()` call, or pass the name in the
> `MgRequest` payload and match it server-side. Keep the export
> signature identical to ace.h.

> **Note:** verify the error constants `AE_INVALID_HANDLE`,
> `AE_CONNECTION_FAILED`, `AE_INVALID_OPTION` exist in
> `include/openads/ace.h` / the `openads` enum. Substitute the closest
> real constant if a name differs.

- [ ] **Step 5: Verify it compiles**

Run: `cmake --build build/default --target openads_core openads_ace`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/abi/ace_exports.cpp
git commit -m "feat(abi): real AdsMg* Get/mutator exports backed by MgCollector"
```

---

## Phase 5 — comm-counter instrumentation

### Task 16: Bump MgStats from the server session loop

**Files:**
- Modify: `src/network/server.cpp`

- [ ] **Step 1: Set the uptime origin** — in `Server::start()`, after
  the listener is bound, touch the process stats so `start_time` is
  fixed at server startup:

```cpp
    // M9.25 — fix the telemetry uptime origin at server start.
    openads::mgmt::process_mg_stats().start_time =
        std::chrono::system_clock::now();
```

- [ ] **Step 2: Bump packet counters** — in `session_loop`, after each
  successful `read_frame`, and after each `write_frame`:

```cpp
    // after a successful inbound read_frame(s):
    {
        auto& st = openads::mgmt::process_mg_stats();
        st.packets_in.fetch_add(1, std::memory_order_relaxed);
        st.bytes_in.fetch_add(frame.payload.size() + 5,
                              std::memory_order_relaxed);
    }
```

```cpp
    // after each write_frame(s, ...):
    openads::mgmt::process_mg_stats()
        .packets_out.fetch_add(1, std::memory_order_relaxed);
```

- [ ] **Step 3: Bump disconnect + high-water marks** — when a session
  ends (in `unregister_session` or at `session_loop` exit):

```cpp
    openads::mgmt::process_mg_stats()
        .disconnects.fetch_add(1, std::memory_order_relaxed);
```

In `register_session`, after inserting, raise the connection
high-water mark:

```cpp
    openads::mgmt::MgStats::bump_max(
        openads::mgmt::process_mg_stats().max_connections,
        static_cast<std::uint32_t>(sessions_info_.size()));
```

- [ ] **Step 4: Verify it compiles + run suite**

Run: `cmake --build build/default --target openads_core && ctest --test-dir build/default --output-on-failure`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add src/network/server.cpp
git commit -m "feat(network): instrument MgStats comm counters in session loop"
```

---

## Phase 6 — test refresh + integration + smoke

### Task 17: Rewrite abi_mgmt_test.cpp expectations

**Files:**
- Modify: `tests/unit/abi_mgmt_test.cpp`

- [ ] **Step 1: Replace the zero-fill expectations** — the tests at
  lines 46-71 currently assert all-zero structs. Replace
  `AdsMgGetInstallInfo zero-fills...` and
  `AdsMgGetActivityInfo zero-fills...` with real-shape checks:

```cpp
TEST_CASE("AdsMgGetInstallInfo reports the product version") {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);

    ADS_MGMT_INSTALL_INFO info;
    UNSIGNED16 sz = sizeof(info);
    REQUIRE(AdsMgGetInstallInfo(h, &info, &sz) == 0);
    CHECK(sz == sizeof(ADS_MGMT_INSTALL_INFO));
    std::string ver(reinterpret_cast<const char*>(info.aucVersionStr));
    CHECK(ver.rfind("OpenADS", 0) == 0);

    REQUIRE(AdsMgDisconnect(h) == 0);
}

TEST_CASE("AdsMgGetActivityInfo on a local handle reports 1 connection") {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);

    ADS_MGMT_ACTIVITY_INFO act;
    UNSIGNED16 sz = sizeof(act);
    REQUIRE(AdsMgGetActivityInfo(h, &act, &sz) == 0);
    CHECK(act.stConnections.ulInUse == 1);

    REQUIRE(AdsMgDisconnect(h) == 0);
}

TEST_CASE("AdsMg* reject an unknown handle") {
    ADS_MGMT_ACTIVITY_INFO act;
    UNSIGNED16 sz = sizeof(act);
    CHECK(AdsMgGetActivityInfo(/*bogus*/ 0x1234, &act, &sz) != 0);
}
```

Update the `extern "C"` block at the top so the declared signatures
match the final exports (notably `AdsMgGetInstallInfo` /
`AdsMgGetActivityInfo` take the real structs, and `AdsMgKillUser`'s
final signature). Keep `AdsMgGetUserNames`, `AdsMgKillUser`,
`AdsMgResetCommStats` smoke cases but assert success only.

- [ ] **Step 2: Run the test**

Run: `cmake --build build/default --target openads_unit_tests && ./build/default/tests/openads_unit_tests -tc="AdsMg*"`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/abi_mgmt_test.cpp
git commit -m "test(abi): AdsMg* expectations updated for real telemetry"
```

### Task 18: Remote integration test

**Files:**
- Create: `tests/unit/abi_mgmt_remote_test.cpp`
- Modify: `tests/CMakeLists.txt` (add `unit/abi_mgmt_remote_test.cpp`)

- [ ] **Step 1: Add to the build** — in `tests/CMakeLists.txt` after
  `unit/mg_wire_test.cpp`, add `    unit/abi_mgmt_remote_test.cpp`.

- [ ] **Step 2: Write the test**

```cpp
#include "doctest.h"

#include "network/server.h"
#include "openads/ace.h"

#include <string>
#include <thread>

extern "C" {
UNSIGNED32 AdsMgConnect(UNSIGNED8*, UNSIGNED8*, UNSIGNED8*, ADSHANDLE*);
UNSIGNED32 AdsMgDisconnect(ADSHANDLE);
UNSIGNED32 AdsMgGetActivityInfo(ADSHANDLE, void*, UNSIGNED16*);
}

TEST_CASE("AdsMgGetActivityInfo over the wire counts a live session") {
    using openads::network::Server;
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).ok());
    std::uint16_t port = srv.port();

    Server::SessionInfo a;
    a.peer_ip = "127.0.0.1"; a.peer_port = 6001;
    a.user = "alice"; a.open_tables = 1;
    std::uint64_t id = srv.register_session(a);

    std::string server = "127.0.0.1:" + std::to_string(port);
    std::vector<UNSIGNED8> srvbuf(server.begin(), server.end());
    srvbuf.push_back(0);
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";

    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srvbuf.data(), usr, pwd, &h) == 0);

    ADS_MGMT_ACTIVITY_INFO act;
    UNSIGNED16 sz = sizeof(act);
    REQUIRE(AdsMgGetActivityInfo(h, &act, &sz) == 0);
    CHECK(act.stConnections.ulInUse == 1);

    REQUIRE(AdsMgDisconnect(h) == 0);
    srv.unregister_session(id);
    srv.stop();
}
```

- [ ] **Step 3: Run the test, verify it passes**

Run: `cmake --build build/default --target openads_unit_tests && ./build/default/tests/openads_unit_tests -tc="AdsMgGetActivityInfo over the wire counts a live session"`
Expected: PASS. If it fails on the client API, this is where the
`Client::request` shape from Task 13's note must be reconciled.

- [ ] **Step 4: Run the full suite**

Run: `ctest --test-dir build/default --output-on-failure`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add tests/unit/abi_mgmt_remote_test.cpp tests/CMakeLists.txt
git commit -m "test(abi): AdsMg* remote round-trip integration test"
```

### Task 19: Harbour smoke verification

**Files:**
- Modify: `tests/smoke/harbour/README.md` (document the manage.prg check)

- [ ] **Step 1: Document the smoke check** — append to
  `tests/smoke/harbour/README.md` a section describing the manual
  acceptance check:

```markdown
## AdsMg* telemetry (M9.25)

Build `contrib/rddads/tests/manage.prg`, then run it against a live
`openads_serverd`:

    manage

Expected: with at least one other client connected, the report shows
non-zero `Connections` / `WorkAreas` / `Tables` counters and a
non-zero `Up Time`, instead of the all-zero output the stub produced.
`AdsVersion(3)` continues to report the OpenADS engine string.
```

- [ ] **Step 2: Build the DLL and run the local unit suite once more**

Run: `cmake --build build/default && ctest --test-dir build/default --output-on-failure`
Expected: all PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/smoke/harbour/README.md
git commit -m "docs(smoke): manage.prg AdsMg* telemetry acceptance check"
```

- [ ] **Step 4: Manual remote verification** (not automated)

On a machine with Harbour + the iMac test server reachable: rebuild
`manage.prg`, run it pointed at the server, confirm non-zero
connections and uptime. Record the result in the PR description.

---

## Self-Review

**Spec coverage:**
- All ~17 AdsMg* functions — Tasks 14, 15 (Connect/Disconnect, 5
  scalar Get*, 7 list/owner Get*, 3 mutators, DumpInternalTables). ✓
- Single MgCollector source of truth — Tasks 3-7, reused server-side
  via `build_mg_snapshot` (Task 11) + dispatch (Task 12). ✓
- Remote transport, new opcodes, same port — Tasks 10-12. ✓
- Local mode reports process — Task 13 `fetch_mg_snapshot` local
  branch. ✓
- Explicit LE serialization — Tasks 8-9. ✓
- Telemetry instrumentation (uptime, comm, high-water) — Task 16. ✓
- Honesty zeros documented — encoded as comments in Tasks 5-6 and the
  spec table. ✓
- Error handling (bad handle, buffer clamp) — Task 15
  `emit_mg_struct` / `emit_mg_array` / `AE_INVALID_HANDLE`. ✓
- Testing: collector unit, wire round-trip, integration, smoke —
  Tasks 4-7, 9, 18, 19. ✓

**Open reconciliation points flagged in-plan** (resolve during
execution, against the real source — do not skip):
- `util::Result<T>` accessor/factory names (`ok`/`value`/`error` vs.
  alternatives) — Tasks 8, 13.
- `openads::network::Client` API shape — Task 13.
- `AdsMgKillUser` canonical signature (conn-no vs. name) — Task 15.
- Error-constant names in `ace.h` — Task 15.
- `MgCollector` inside `util::Result` (non-default-constructible) —
  Task 15.

These are intentional: the plan cannot see those exact symbols.
Each task says explicitly what to verify and how to adapt.

**Type consistency:** `MgSnapshot`, `MgStats`, `MgCollector`,
`MgBackend`, `MgRequestKind`, `encode_mg_snapshot` /
`decode_mg_snapshot` / `encode_mg_request` / `decode_mg_request`,
`build_mg_snapshot`, `kill_session_by_conn_no`, `fetch_mg_snapshot`,
`mg_collector_for`, `emit_mg_struct`, `emit_mg_array` — names used
identically across every task that references them. ✓
