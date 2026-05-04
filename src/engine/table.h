#pragma once

#include "drivers/driver_trait.h"
#include "drivers/dbf_common.h"
#include "drivers/index_trait.h"
#include "drivers/memo_trait.h"
#include "engine/lock_mgr.h"
#include "engine/order.h"
#include "engine/tx.h"
#include "util/result.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openads::engine {

enum class TableType { Cdx, Ntx, Adt, Vfp };
enum class OpenMode  { Read, Shared, Exclusive };

class Table {
public:
    Table() = default;
    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;
    Table(Table&&) noexcept = default;
    Table& operator=(Table&&) noexcept = default;
    ~Table() = default;

    static util::Result<Table> open(const std::string& path,
                                    TableType type,
                                    OpenMode mode = OpenMode::Read,
                                    LockingMode locking = LockingMode::Compatible);

    const std::string& path() const noexcept { return path_; }

    std::uint16_t field_count() const noexcept;
    const drivers::DbfField& field_descriptor(std::uint16_t idx) const;
    std::int32_t field_index(const std::string& name) const noexcept;

    std::uint32_t record_count() const noexcept;
    std::uint32_t recno() const noexcept { return recno_; }
    bool eof() const noexcept { return state_ == State::Eof; }
    bool bof() const noexcept { return state_ == State::Bof; }

    util::Result<void> goto_top();
    util::Result<void> goto_bottom();
    util::Result<void> goto_record(std::uint32_t recno);
    util::Result<void> skip(std::int32_t delta);

    util::Result<drivers::DbfFieldValue>
        read_field(std::uint16_t field_index);

    // Write surface.
    util::Result<void> append_record();
    util::Result<void> set_field(std::uint16_t field_index,
                                 const std::string& value);
    util::Result<void> set_field(std::uint16_t field_index, double value);
    util::Result<void> set_field(std::uint16_t field_index, bool value);

    // Memo write with an explicit FPT block-type tag (M9.13). Mirrors
    // set_field(idx, std::string) but routes through the memo store's
    // write_typed so the on-disk block carries the binary/picture flag
    // instead of the default Text marker.
    util::Result<void>
        set_field_binary(std::uint16_t                field_index,
                         const std::string&           payload,
                         drivers::MemoBlockType       type);

    // Look up the FPT block-type tag for a memo field at the current
    // record. Returns Text if the field is empty or the memo store
    // doesn't track types (e.g. DBT).
    util::Result<drivers::MemoBlockType>
        field_memo_type(std::uint16_t field_index);
    util::Result<void> mark_deleted();
    util::Result<void> recall_deleted();
    bool               is_deleted() const noexcept;
    util::Result<void> flush();

    // Drop every record. Header rec count -> 0 and every bound index
    // (active order plus parked extras) is walked to erase its
    // entries. Indexes left empty but structurally intact.
    util::Result<void> zap();

    // Physically remove deleted records: copy live rows downward,
    // truncate header reccount to live count, and rebuild every
    // bound index so its entries point at the new recnos.
    util::Result<void> pack();

    // Re-build every bound index in place by clearing its entries
    // and re-inserting (recno, evaluate_index_expr) for each live
    // record. Used by AdsReindex when an app has mutated the DBF
    // outside the index sync path or the key expression has been
    // changed externally.
    util::Result<void> reindex();

    // Locking surface.
    util::Result<void> lock_record_excl(std::uint32_t recno);
    util::Result<void> unlock_record    (std::uint32_t recno);
    util::Result<void> lock_table_excl();
    util::Result<void> unlock_table();

    // Memo surface (M4).
    void               attach_memo(std::unique_ptr<drivers::IMemoStore> memo);
    drivers::IMemoStore* memo() noexcept { return memo_.get(); }

    // Row filter (M7.3). When set, navigation methods automatically
    // advance past non-matching records in their movement direction.
    using RowPredicate = std::function<bool(Table&)>;
    void set_filter(RowPredicate p)   { filter_ = std::move(p); }
    void clear_filter()                { filter_ = nullptr; }
    bool has_filter() const noexcept   { return static_cast<bool>(filter_); }

    // Transaction binding (M5). When a Connection has an active Tx,
    // it points each open Table at it via attach_tx so Table writes
    // record before-images for rollback.
    void               attach_tx(Tx* tx, Tx::TableId tid) noexcept {
        tx_ = tx; tid_ = tid;
    }
    void               detach_tx() noexcept { tx_ = nullptr; }

    drivers::IDriver*  driver() noexcept { return driver_.get(); }

    // Order + scope surface (M3).
    void               set_order(std::unique_ptr<drivers::IIndex> idx);
    void               clear_order();
    // Take ownership of the active index back from the Table, leaving
    // it without an order. Returns nullptr if no order was set.
    std::unique_ptr<drivers::IIndex> take_order();
    Order*             order() noexcept { return order_ ? &*order_ : nullptr; }

    // Non-owning views of additional indexes the ABI layer parks
    // outside the Table. Every record mutation (`set_field`,
    // `append_record`) walks both the active order and these views,
    // so a multi-tag CDX stays consistent across all tags. The ABI
    // owns the IIndex lifetime; the Table only borrows it for sync.
    void register_extra_index_view(drivers::IIndex* idx);
    void unregister_extra_index_view(drivers::IIndex* idx);
    void clear_extra_index_views();
    const Order*       order() const noexcept { return order_ ? &*order_ : nullptr; }
    util::Result<bool> seek_key(const std::string& key, bool soft);
    bool last_seek_found() const noexcept { return last_seek_found_; }
    util::Result<void> set_scope(bool top, const std::string& key);
    util::Result<void> clear_scope(bool top);
    util::Result<void> clear_scopes();
    std::optional<std::string> get_scope(bool top) const;

private:
    enum class State { Bof, Positioned, Eof };

    Table(std::unique_ptr<drivers::IDriver> drv,
          OpenMode mode, LockingMode locking, TableType type) noexcept
        : driver_(std::move(drv)), mode_(mode),
          locking_(locking), type_(type) {}

    util::Result<void> load_record_(std::uint32_t recno);
    util::Result<void> writeback_record_();

    // Sync the active index with the current record's key. Called
    // after every record mutation (set_field / append_record). For
    // appends `prev_key` is empty so erase is skipped; for modifies
    // it carries the index key as it was just before the write so
    // the old entry can be removed before the new one is inserted.
    util::Result<void> sync_active_index_(const std::string& prev_key);

    // Snapshot every bound index's current key (active order + extra
    // views), and replay the snapshot after a write to update the
    // entries in lockstep. Multi-index variant of sync_active_index_.
    std::vector<std::pair<drivers::IIndex*, std::string>>
        snapshot_index_keys_();
    util::Result<void> sync_all_indexes_(
        const std::vector<std::pair<drivers::IIndex*, std::string>>& snap);

    // Compute the index key bytes for the current `record_buf_`
    // given an index expression. Currently supports bare field names
    // only (e.g., "NAME"); compound expressions land later.
    std::string compute_index_key_(const std::string& expr,
                                   std::uint16_t       key_len) const;

    bool key_in_top_scope_   (const std::string& key) const;
    bool key_in_bottom_scope_(const std::string& key) const;

    TableTypeForLock to_lock_type_() const noexcept;

    std::unique_ptr<drivers::IDriver>             driver_;
    std::unique_ptr<drivers::IMemoStore>          memo_;
    RowPredicate                                  filter_;
    Tx*                                           tx_       = nullptr;
    Tx::TableId                                   tid_      = 0;
    OpenMode                                      mode_     = OpenMode::Read;
    LockingMode                                   locking_  = LockingMode::Compatible;
    TableType                                     type_     = TableType::Cdx;
    LockMgr                                       locks_;
    std::unordered_map<std::uint32_t, LockHandle> recno_locks_;
    std::optional<LockHandle>                     table_lock_;
    std::optional<Order>                          order_;
    std::vector<drivers::IIndex*>                 extra_index_views_;
    State                                         state_  = State::Bof;
    std::uint32_t                                 recno_  = 0;
    std::vector<std::uint8_t>                     record_buf_;
    std::string                                   path_;
    bool                                          last_seek_found_ = false;
};

} // namespace openads::engine
