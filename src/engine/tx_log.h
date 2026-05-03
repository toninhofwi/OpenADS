#pragma once

#include "platform/file.h"
#include "util/result.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace openads::engine {

// Append-only WAL with one record per tx event. Each record has a
// 24-byte fixed header followed by a variable payload and a trailing
// CRC-32C over the header+payload. Layout (LE everywhere):
//
//   bytes 0-3   : magic 0x57414C00 ("WAL\0") — record sync marker
//   byte  4     : type (1=BEGIN, 2=UPDATE, 3=COMMIT, 4=ABORT)
//   byte  5     : reserved (0)
//   bytes 6-7   : payload length
//   bytes 8-15  : tx_id (uint64)
//   bytes 16-23 : lsn   (uint64, monotonic per log)
//   bytes 24..  : payload (depends on type)
//   trailing 4  : CRC-32C of header+payload
//
// UPDATE payload: table_id[4] recno[4] before_len[2] before[N] after_len[2] after[M]

enum class TxRecordType : std::uint8_t {
    Begin   = 1,
    Update  = 2,
    Commit  = 3,
    Abort   = 4,
};

struct TxUpdatePayload {
    std::string                table_path; // relative to data dir
    std::uint32_t              recno;
    std::vector<std::uint8_t>  before;
    std::vector<std::uint8_t>  after;
};

struct TxRecord {
    TxRecordType        type;
    std::uint64_t       tx_id;
    std::uint64_t       lsn = 0;
    TxUpdatePayload     update;   // populated when type == Update
};

class TxLog {
public:
    TxLog() = default;
    TxLog(const TxLog&) = delete;
    TxLog& operator=(const TxLog&) = delete;

    // Custom move: std::mutex and std::atomic are not movable, so we
    // copy the atomic's value and default-construct fresh mutexes.
    // Caller is responsible for ensuring no thread holds the source's
    // locks at the time of the move.
    TxLog(TxLog&& o) noexcept
        : file_(std::move(o.file_)),
          write_offset_(o.write_offset_),
          path_(std::move(o.path_)),
          next_lsn_(o.next_lsn_.load()),
          last_synced_lsn_(o.last_synced_lsn_) {}

    TxLog& operator=(TxLog&& o) noexcept {
        if (this != &o) {
            file_           = std::move(o.file_);
            write_offset_   = o.write_offset_;
            path_           = std::move(o.path_);
            next_lsn_.store(o.next_lsn_.load());
            last_synced_lsn_ = o.last_synced_lsn_;
        }
        return *this;
    }

    // Open or create the WAL at `path`. Existing content is preserved
    // (callers run recovery before resuming writes). Resumes the LSN
    // counter from the highest LSN already on disk so newly appended
    // records don't collide with surviving ones.
    util::Result<void> open(const std::string& path);

    // append_*_async: append the record and return its assigned LSN.
    // No fsync is performed. Caller is responsible for calling
    // sync_to(lsn) (or append_commit, which syncs) before treating the
    // record as durable.
    util::Result<std::uint64_t> append_begin_async (std::uint64_t tx_id);
    util::Result<std::uint64_t> append_update_async(std::uint64_t tx_id,
                                     const std::string& table_path,
                                     std::uint32_t recno,
                                     const std::vector<std::uint8_t>& before,
                                     const std::vector<std::uint8_t>& after);
    util::Result<std::uint64_t> append_commit_async(std::uint64_t tx_id);
    util::Result<std::uint64_t> append_abort_async (std::uint64_t tx_id);

    // Legacy synchronous wrappers — append + sync up to the new record.
    util::Result<void> append_begin (std::uint64_t tx_id);
    util::Result<void> append_update(std::uint64_t tx_id,
                                     const std::string& table_path,
                                     std::uint32_t recno,
                                     const std::vector<std::uint8_t>& before,
                                     const std::vector<std::uint8_t>& after);
    util::Result<void> append_commit(std::uint64_t tx_id);
    util::Result<void> append_abort (std::uint64_t tx_id);

    // Group commit primitive: ensure every record up to and including
    // `lsn` is durable. The first thread that observes
    // last_synced_lsn_ < lsn issues a single fsync covering every
    // record appended so far; concurrent waiters with smaller targets
    // see last_synced_lsn_ already advanced and return immediately.
    util::Result<void> sync_to(std::uint64_t lsn);

    // Force every appended record to disk regardless of LSN.
    util::Result<void> sync();

    std::uint64_t last_synced_lsn() const noexcept { return last_synced_lsn_; }
    std::uint64_t high_water_lsn () const noexcept { return next_lsn_.load(); }

    // Read every record in the log from the start. Truncates the
    // returned set at the first malformed record.
    util::Result<std::vector<TxRecord>> read_all();

    // Truncate the log file to zero length. Used after recovery so the
    // log doesn't grow unbounded. Resets the LSN counter.
    util::Result<void> truncate();

    bool is_open() const noexcept { return file_.is_open(); }

private:
    util::Result<std::uint64_t>
        append_record_(TxRecordType type, std::uint64_t tx_id,
                       const std::vector<std::uint8_t>& payload);

    platform::File          file_;
    std::uint64_t           write_offset_     = 0;
    std::string             path_;

    // Group-commit state.
    std::atomic<std::uint64_t> next_lsn_       {1};
    std::uint64_t              last_synced_lsn_ = 0;
    std::mutex                 sync_mu_;
    std::mutex                 append_mu_;     // serialises file_.write_at
};

} // namespace openads::engine
