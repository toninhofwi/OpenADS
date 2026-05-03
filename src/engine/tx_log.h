#pragma once

#include "platform/file.h"
#include "util/result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openads::engine {

// Append-only WAL with one record per tx event. Each record has a
// 12-byte fixed header followed by a variable payload and a trailing
// CRC-32C over the header+payload. Layout (LE everywhere):
//
//   bytes 0-3   : magic 0x57414C00 ("WAL\0") — record sync marker
//   byte  4     : type (1=BEGIN, 2=UPDATE, 3=COMMIT, 4=ABORT)
//   byte  5     : reserved (0)
//   bytes 6-7   : payload length
//   bytes 8-15  : tx_id (uint64)
//   bytes 16..  : payload (depends on type)
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
    std::uint32_t              table_id;
    std::uint32_t              recno;
    std::vector<std::uint8_t>  before;
    std::vector<std::uint8_t>  after;
};

struct TxRecord {
    TxRecordType        type;
    std::uint64_t       tx_id;
    TxUpdatePayload     update;   // populated when type == Update
};

class TxLog {
public:
    TxLog() = default;
    TxLog(const TxLog&) = delete;
    TxLog& operator=(const TxLog&) = delete;
    TxLog(TxLog&&) noexcept = default;
    TxLog& operator=(TxLog&&) noexcept = default;

    // Open or create the WAL at `path`. Existing content is preserved
    // (callers run recovery before resuming writes).
    util::Result<void> open(const std::string& path);

    util::Result<void> append_begin (std::uint64_t tx_id);
    util::Result<void> append_update(std::uint64_t tx_id,
                                     std::uint32_t table_id,
                                     std::uint32_t recno,
                                     const std::vector<std::uint8_t>& before,
                                     const std::vector<std::uint8_t>& after);
    util::Result<void> append_commit(std::uint64_t tx_id);
    util::Result<void> append_abort (std::uint64_t tx_id);

    util::Result<void> sync();

    // Read every record in the log from the start. Truncates the
    // returned set at the first malformed record.
    util::Result<std::vector<TxRecord>> read_all();

    // Truncate the log file to zero length. Used after recovery so the
    // log doesn't grow unbounded.
    util::Result<void> truncate();

    bool is_open() const noexcept { return file_.is_open(); }

private:
    util::Result<void> append_record_(TxRecordType type, std::uint64_t tx_id,
                                      const std::vector<std::uint8_t>& payload);

    platform::File   file_;
    std::uint64_t    write_offset_ = 0;
    std::string      path_;
};

} // namespace openads::engine
