#pragma once

#include "util/result.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace openads::engine {

// Persistent map of (table_path, recno) -> last applied LSN. Used by
// crash recovery to make the redo / undo path idempotent: a record
// whose stored LSN is >= the WAL record's LSN has already been applied
// in a previous recovery pass and must be skipped.
//
// Sidecar file format (binary, little-endian):
//   bytes 0-3   : magic 'LSNM'
//   bytes 4-7   : entry count
//   per entry   : path_len[2] path[path_len] recno[4] lsn[8]
class LsnMap {
public:
    LsnMap() = default;

    util::Result<void> open(const std::string& path);
    util::Result<void> flush();

    // Returns 0 if no record. Otherwise the highest LSN stored for the
    // given (table_path, recno).
    std::uint64_t get(const std::string& table_path,
                      std::uint32_t      recno) const noexcept;

    // Records that the WAL record with `lsn` has been applied to
    // (table_path, recno). Idempotent: only advances the stored LSN.
    void put(const std::string& table_path,
             std::uint32_t      recno,
             std::uint64_t      lsn);

    void clear();
    std::size_t size() const noexcept { return entries_.size(); }

private:
    struct Key {
        std::string   table_path;
        std::uint32_t recno;
        bool operator==(const Key& o) const noexcept {
            return recno == o.recno && table_path == o.table_path;
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept {
            std::size_t h = std::hash<std::string>{}(k.table_path);
            h ^= std::hash<std::uint32_t>{}(k.recno) + 0x9E3779B9u
                 + (h << 6) + (h >> 2);
            return h;
        }
    };

    std::string                                  path_;
    std::unordered_map<Key, std::uint64_t, KeyHash> entries_;
};

} // namespace openads::engine
