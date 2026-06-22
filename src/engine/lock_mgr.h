#pragma once

#include "platform/file.h"
#include "platform/lock.h"
#include "util/result.h"

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace openads::engine {

enum class TableTypeForLock { Ntx, Cdx, Vfp, Adt };
enum class LockingMode      { Compatible, Proprietary };

class LockHandle {
public:
    LockHandle() = default;
    LockHandle(platform::ByteLock&& lk,
               std::uint64_t offset, std::uint64_t length) noexcept
        : lock_(std::move(lk)), offset_(offset), length_(length) {}

    LockHandle(LockHandle&&) noexcept = default;
    LockHandle& operator=(LockHandle&&) noexcept = default;

    std::uint64_t offset() const noexcept { return offset_; }
    std::uint64_t length() const noexcept { return length_; }

    void release() noexcept { lock_.release(); }

private:
    platform::ByteLock lock_;
    std::uint64_t      offset_ = 0;
    std::uint64_t      length_ = 0;
};

class LockMgr {
public:
    util::Result<LockHandle>
        lock_table_excl (platform::File& f,
                         TableTypeForLock t, LockingMode m);

    util::Result<LockHandle>
        lock_record_excl(platform::File& f,
                         TableTypeForLock t, LockingMode m,
                         std::uint32_t recno);

    util::Result<LockHandle>
        lock_record_shared(platform::File& f,
                           TableTypeForLock t, LockingMode m,
                           std::uint32_t recno);

    // Non-blocking variants (M9.18). Return AE_LOCKED when the OS-level
    // byte-range lock is held by another owner; the caller (typically
    // the ABI retry loop) decides whether to back off and try again
    // instead of blocking on the kernel.
    util::Result<LockHandle>
        try_lock_table_excl(platform::File& f,
                            TableTypeForLock t, LockingMode m);

    util::Result<LockHandle>
        try_lock_record_excl(platform::File& f,
                             TableTypeForLock t, LockingMode m,
                             std::uint32_t recno);

    // Release the per-key bookkeeping for a lock the caller is unlocking, so
    // the NEXT acquire takes a fresh OS-level byte lock. The owning LockHandle
    // (which holds the OS lock) is released by the caller separately; without
    // this the held_ entry lingers forever and every subsequent acquire on the
    // same byte range returns an empty (no-op) handle -> lost concurrency.
    void unlock_table (platform::File& f, TableTypeForLock t, LockingMode m);
    void unlock_record(platform::File& f, TableTypeForLock t, LockingMode m,
                       std::uint32_t recno);

    static std::uint64_t file_lock_offset(TableTypeForLock t, LockingMode m);
    static std::uint64_t record_lock_offset(TableTypeForLock t, LockingMode m,
                                            std::uint32_t recno);

private:
    struct Key {
        const void*   file;
        std::uint64_t offset;
        bool operator==(const Key& o) const noexcept {
            return file == o.file && offset == o.offset;
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept {
            return std::hash<const void*>{}(k.file) ^
                   std::hash<std::uint64_t>{}(k.offset);
        }
    };
    void forget_(const Key& k) noexcept;

    std::unordered_map<Key, int, KeyHash> held_;
};

} // namespace openads::engine
