#pragma once

#include "mgmt/mg_snapshot.h"
#include "mgmt/mg_stats.h"
#include "openads/ace.h"

#include <vector>

namespace openads::mgmt {

// Copy the live MgStats counters (uptime origin, cumulative comm
// totals, high-water marks) into a MgSnapshot. Called by whichever
// process builds the snapshot — server or local DLL — so the
// cumulative telemetry travels the wire alongside the live counts.
void capture_mg_stats(MgSnapshot& snap, const MgStats& stats);

// Formats a raw MgSnapshot into the SAP-canonical ADS_MGMT_* structs
// declared in include/openads/ace.h. Pure: holds a copy of the
// snapshot and never touches global state, so it is trivially
// unit-testable with a fabricated snapshot. The snapshot carries
// everything — live counts AND the captured MgStats values — so a
// remote caller formats the server's telemetry, not its own.
class MgCollector {
public:
    explicit MgCollector(MgSnapshot snapshot);

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
    MgSnapshot snapshot_;
};

}  // namespace openads::mgmt
