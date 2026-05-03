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
    util::Result<void> mark_deleted();
    util::Result<void> recall_deleted();
    bool               is_deleted() const noexcept;
    util::Result<void> flush();

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
    Order*             order() noexcept { return order_ ? &*order_ : nullptr; }
    const Order*       order() const noexcept { return order_ ? &*order_ : nullptr; }
    util::Result<bool> seek_key(const std::string& key, bool soft);
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
    State                                         state_  = State::Bof;
    std::uint32_t                                 recno_  = 0;
    std::vector<std::uint8_t>                     record_buf_;
};

} // namespace openads::engine
