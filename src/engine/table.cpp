#include "engine/table.h"

#include "engine/index_expr.h"

#include "drivers/cdx/cdx_driver.h"
#include "drivers/ntx/ntx_driver.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

namespace openads::engine {

util::Result<Table> Table::open(const std::string& path,
                                TableType type,
                                OpenMode mode,
                                LockingMode locking) {
    std::unique_ptr<drivers::IDriver> drv;
    switch (type) {
        case TableType::Cdx:
            drv = std::make_unique<drivers::cdx::CdxDriver>();
            break;
        case TableType::Ntx:
            drv = std::make_unique<drivers::ntx::NtxDriver>();
            break;
        case TableType::Adt:
        case TableType::Vfp:
            return util::Error{5004, 0,
                               "table type not yet supported in M3", path};
    }
    drivers::DriverOpenMode dmode = drivers::DriverOpenMode::ReadOnly;
    switch (mode) {
        case OpenMode::Read:      dmode = drivers::DriverOpenMode::ReadOnly;  break;
        case OpenMode::Shared:    dmode = drivers::DriverOpenMode::Shared;    break;
        case OpenMode::Exclusive: dmode = drivers::DriverOpenMode::Exclusive; break;
    }
    if (auto r = drv->open(path, dmode); !r) return r.error();
    Table t{std::move(drv), mode, locking, type};
    t.path_ = path;
    return t;
}

std::uint16_t Table::field_count() const noexcept {
    return static_cast<std::uint16_t>(driver_->fields().size());
}

const drivers::DbfField& Table::field_descriptor(std::uint16_t idx) const {
    return driver_->fields().at(idx);
}

std::int32_t Table::field_index(const std::string& name) const noexcept {
    const auto& fs = driver_->fields();
    for (std::size_t i = 0; i < fs.size(); ++i) {
        if (fs[i].name == name) return static_cast<std::int32_t>(i);
    }
    return -1;
}

std::uint32_t Table::record_count() const noexcept {
    return driver_->record_count();
}

util::Result<void> Table::load_record_(std::uint32_t recno) {
    auto buf = driver_->read_record_raw(recno);
    if (!buf) return buf.error();
    record_buf_ = std::move(buf).value();
    recno_      = recno;
    state_      = State::Positioned;
    return {};
}

std::string Table::compute_index_key_(const std::string& expr,
                                      std::uint16_t       key_len) const {
    // Compound expressions (UPPER(NAME), STR(AGE,3), concatenation,
    // SUBSTR, ...) handled by engine/index_expr.cpp. Bare field-name
    // expressions short-circuit there to the legacy raw-bytes path so
    // existing CDX files stay byte-exact.
    auto r = evaluate_index_expr(const_cast<Table&>(*this), expr, key_len);
    if (!r) return std::string(key_len, ' ');
    return r.value();
}

// Snapshot the current key for every bound index. Caller invokes
// this BEFORE mutating record_buf_ so the snapshot reflects the
// pre-write key per index; after the write, sync_all_indexes_(snap)
// erases each prior key and inserts the new one.
std::vector<std::pair<drivers::IIndex*, std::string>>
Table::snapshot_index_keys_() {
    std::vector<std::pair<drivers::IIndex*, std::string>> out;
    auto push = [&](drivers::IIndex* idx) {
        if (idx == nullptr) return;
        out.emplace_back(idx,
            compute_index_key_(idx->expression(), idx->key_length()));
    };
    if (order_ && order_->index()) push(order_->index());
    for (auto* x : extra_index_views_) push(x);
    return out;
}

util::Result<void> Table::sync_active_index_(const std::string& /*unused*/) {
    // Re-entered after a full snapshot was taken on the caller's
    // side; that path is the new sync_all_indexes_ below.
    return {};
}

util::Result<void> Table::sync_all_indexes_(
    const std::vector<std::pair<drivers::IIndex*, std::string>>& snap) {
    for (auto& [idx, prev_key] : snap) {
        std::string new_key = compute_index_key_(idx->expression(),
                                                 idx->key_length());
        if (prev_key == new_key) continue;
        // Erase prior (recno, prev_key); ignore failure — the index
        // may not have a prior entry (fresh APPEND case).
        if (!prev_key.empty()) {
            (void)idx->erase(recno_, prev_key);
        }
        if (auto e = idx->insert(recno_, new_key); !e) {
            return e.error();
        }
    }
    return {};
}

util::Result<void> Table::writeback_record_() {
    if (state_ != State::Positioned) {
        return util::Error{5000, 0, "no record positioned", ""};
    }
    if (tx_ && tx_->active()) {
        auto cur = driver_->read_record_raw(recno_);
        if (cur) {
            std::vector<std::uint8_t> before = std::move(cur).value();
            std::vector<std::uint8_t> after(record_buf_);
            tx_->note_before_image(tid_, recno_, std::move(before), std::move(after));
        }
    }
    return driver_->write_record_raw(recno_, record_buf_.data(),
                                     record_buf_.size());
}

bool Table::key_in_top_scope_(const std::string& key) const {
    if (!order_ || !order_->scope().top.has_value()) return true;
    return key >= *order_->scope().top;
}

bool Table::key_in_bottom_scope_(const std::string& key) const {
    if (!order_ || !order_->scope().bottom.has_value()) return true;
    return key <= *order_->scope().bottom;
}

void Table::set_recno_sequence(std::vector<std::uint32_t> seq) {
    recno_sequence_ = std::move(seq);
    sequence_idx_   = -1;
}

util::Result<void> Table::goto_top() {
    if (!recno_sequence_.empty()) {
        sequence_idx_ = 0;
        std::uint32_t r = recno_sequence_.front();
        return load_record_(r);
    }
    if (order_ && order_->index()) {
        auto* idx = order_->index();
        util::Result<drivers::SeekOutcome> r = drivers::SeekOutcome{};
        if (order_->descending_traverse()) {
            // Reverse traversal: "top" is the last key in ascending
            // order. Scopes flip too — top-bound becomes the upper
            // bound while walking backward.
            r = idx->seek_last();
        } else if (order_->scope().top.has_value()) {
            r = idx->seek_key(*order_->scope().top, true);
        } else {
            r = idx->seek_first();
        }
        if (!r) return r.error();
        if (!r.value().positioned) {
            state_ = State::Eof; recno_ = 0; return {};
        }
        if (!key_in_bottom_scope_(idx->current_key())) {
            state_ = State::Eof; recno_ = 0; return {};
        }
        return load_record_(r.value().recno);
    }
    if (driver_->record_count() == 0) {
        state_ = State::Eof; recno_ = 0; return {};
    }
    if (auto r = load_record_(1); !r) return r.error();
    if (filter_ && state_ == State::Positioned && !filter_(*this)) {
        return skip(1);
    }
    return {};
}

util::Result<void> Table::goto_bottom() {
    if (!recno_sequence_.empty()) {
        sequence_idx_ = static_cast<std::int64_t>(recno_sequence_.size() - 1);
        return load_record_(recno_sequence_.back());
    }
    if (order_ && order_->index()) {
        auto* idx = order_->index();
        util::Result<drivers::SeekOutcome> r = idx->seek_last();
        if (!r) return r.error();
        if (!r.value().positioned) {
            state_ = State::Eof; recno_ = 0; return {};
        }
        // Walk backwards while bottom scope is exceeded.
        while (r.value().positioned &&
               !key_in_bottom_scope_(idx->current_key())) {
            r = idx->prev();
            if (!r) return r.error();
        }
        if (!r.value().positioned ||
            !key_in_top_scope_(idx->current_key())) {
            state_ = State::Eof; recno_ = 0; return {};
        }
        return load_record_(r.value().recno);
    }
    auto n = driver_->record_count();
    if (n == 0) { state_ = State::Eof; recno_ = 0; return {}; }
    return load_record_(n);
}

util::Result<void> Table::goto_record(std::uint32_t recno) {
    if (recno == 0 || recno > driver_->record_count()) {
        state_ = State::Eof; recno_ = 0;
        return util::Error{5000, 0, "recno out of range", ""};
    }
    return load_record_(recno);
}

util::Result<void> Table::skip(std::int32_t delta) {
    if (!recno_sequence_.empty()) {
        if (delta == 0) return {};
        std::int64_t idx = sequence_idx_;
        if (state_ == State::Bof) idx = -1;
        if (state_ == State::Eof)
            idx = static_cast<std::int64_t>(recno_sequence_.size());
        idx += delta;
        if (idx < 0) {
            state_ = State::Bof; recno_ = 0; sequence_idx_ = -1; return {};
        }
        if (idx >= static_cast<std::int64_t>(recno_sequence_.size())) {
            state_ = State::Eof; recno_ = 0;
            sequence_idx_ = static_cast<std::int64_t>(recno_sequence_.size());
            return {};
        }
        sequence_idx_ = idx;
        return load_record_(recno_sequence_[static_cast<std::size_t>(idx)]);
    }
    if (order_ && order_->index()) {
        auto* idx = order_->index();
        if (delta == 0) return {};
        // M10.4: when the order is descending, forward-skip walks
        // prev() instead of next() so the cursor moves through the
        // tree in reverse from the caller's perspective.
        bool effective_forward = (delta > 0) ^ order_->descending_traverse();
        util::Result<drivers::SeekOutcome> r = drivers::SeekOutcome{};
        for (std::int32_t i = 0; i < std::abs(delta); ++i) {
            r = effective_forward ? idx->next() : idx->prev();
            if (!r) return r.error();
            if (!r.value().positioned) {
                if (delta > 0) { state_ = State::Eof; recno_ = 0; }
                else           { state_ = State::Bof; recno_ = 0; }
                return {};
            }
            if (!key_in_top_scope_(idx->current_key()) ||
                !key_in_bottom_scope_(idx->current_key())) {
                if (delta > 0) { state_ = State::Eof; recno_ = 0; }
                else           { state_ = State::Bof; recno_ = 0; }
                return {};
            }
        }
        return load_record_(r.value().recno);
    }
    auto n = driver_->record_count();
    if (n == 0) { state_ = State::Eof; recno_ = 0; return {}; }
    std::int64_t target = static_cast<std::int64_t>(recno_) + delta;
    if (state_ == State::Bof && delta > 0) target = delta;
    if (target < 1) { state_ = State::Bof; recno_ = 0; return {}; }
    if (target > static_cast<std::int64_t>(n)) {
        state_ = State::Eof; recno_ = n + 1; return {};
    }
    if (auto r = load_record_(static_cast<std::uint32_t>(target)); !r) {
        return r.error();
    }
    if (filter_) {
        std::int64_t step = (delta >= 0) ? 1 : -1;
        while (state_ == State::Positioned && !filter_(*this)) {
            std::int64_t nt = static_cast<std::int64_t>(recno_) + step;
            if (nt < 1) { state_ = State::Bof; recno_ = 0; return {}; }
            if (nt > static_cast<std::int64_t>(n)) {
                state_ = State::Eof; recno_ = n + 1; return {};
            }
            if (auto r = load_record_(static_cast<std::uint32_t>(nt)); !r) {
                return r.error();
            }
        }
    }
    return {};
}

util::Result<drivers::DbfFieldValue>
Table::read_field(std::uint16_t field_index) {
    if (state_ != State::Positioned) {
        return util::Error{5000, 0, "table not positioned on a record", ""};
    }
    if (field_index >= driver_->fields().size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    const auto& f = driver_->fields().at(field_index);
    auto v = drivers::decode_field(f, record_buf_.data(), record_buf_.size());
    if (!v) return v.error();

    // For M-type fields, the record stores a 10-byte ASCII block number
    // referencing the memo store. Resolve it here.
    if (f.type == drivers::DbfFieldType::Memo && memo_) {
        std::string raw(reinterpret_cast<const char*>(
            record_buf_.data() + f.record_offset), f.length);
        // Skip leading spaces; trailing NULs/spaces ignored by stoul.
        std::uint32_t block = 0;
        try {
            std::size_t pos = 0;
            block = static_cast<std::uint32_t>(std::stoul(raw, &pos, 10));
        } catch (...) {
            block = 0;
        }
        if (block != 0) {
            auto mr = memo_->read(block);
            if (!mr) return mr.error();
            drivers::DbfFieldValue out;
            out.as_string = std::move(mr).value();
            return out;
        }
    }
    return v;
}

util::Result<void> Table::append_record() {
    if (mode_ == OpenMode::Read) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    auto rec = drivers::make_empty_record(driver_->record_length());
    auto new_recno = driver_->append_record_raw(rec.data(), rec.size());
    if (!new_recno) return new_recno.error();
    record_buf_ = std::move(rec);
    recno_      = new_recno.value();
    state_      = State::Positioned;
    if (tx_ && tx_->active()) {
        tx_->note_append(tid_, recno_);
    }
    return {};
}

util::Result<void> Table::set_field(std::uint16_t idx, const std::string& v) {
    if (state_ != State::Positioned) {
        return util::Error{5000, 0, "no record positioned", ""};
    }
    if (idx >= driver_->fields().size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    const auto& f = driver_->fields().at(idx);

    auto snap = snapshot_index_keys_();

    // Memo fields write to the memo store, then store the resulting
    // block number as a right-aligned ASCII string in the record.
    if (f.type == drivers::DbfFieldType::Memo) {
        if (!memo_) {
            return util::Error{5004, 0, "memo store not attached", ""};
        }
        auto wm = memo_->write(v);
        if (!wm) return wm.error();
        char buf[16];
        int n = std::snprintf(buf, sizeof(buf), "%*u",
                              static_cast<int>(f.length),
                              static_cast<unsigned>(wm.value()));
        if (n < 0 || static_cast<std::size_t>(n) > f.length) {
            return util::Error{5000, 0, "memo block number overflows field", ""};
        }
        std::memcpy(record_buf_.data() + f.record_offset, buf, f.length);
        if (auto wb = writeback_record_(); !wb) return wb.error();
        return sync_all_indexes_(snap);
    }

    auto r = drivers::encode_field_string(f, record_buf_.data(),
                                          record_buf_.size(), v);
    if (!r) return r.error();
    if (auto wb = writeback_record_(); !wb) return wb.error();
    return sync_all_indexes_(snap);
}

util::Result<void> Table::set_field(std::uint16_t idx, double v) {
    if (state_ != State::Positioned) {
        return util::Error{5000, 0, "no record positioned", ""};
    }
    if (idx >= driver_->fields().size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    auto snap = snapshot_index_keys_();
    auto r = drivers::encode_field_double(driver_->fields().at(idx),
                                          record_buf_.data(),
                                          record_buf_.size(), v);
    if (!r) return r.error();
    if (auto wb = writeback_record_(); !wb) return wb.error();
    return sync_all_indexes_(snap);
}

util::Result<void> Table::set_field(std::uint16_t idx, bool v) {
    if (state_ != State::Positioned) {
        return util::Error{5000, 0, "no record positioned", ""};
    }
    if (idx >= driver_->fields().size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    auto snap = snapshot_index_keys_();
    auto r = drivers::encode_field_logical(driver_->fields().at(idx),
                                           record_buf_.data(),
                                           record_buf_.size(), v);
    if (!r) return r.error();
    if (auto wb = writeback_record_(); !wb) return wb.error();
    return sync_all_indexes_(snap);
}

util::Result<void>
Table::set_field_binary(std::uint16_t idx, const std::string& payload,
                        drivers::MemoBlockType type) {
    if (state_ != State::Positioned) {
        return util::Error{5000, 0, "no record positioned", ""};
    }
    if (idx >= driver_->fields().size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    const auto& f = driver_->fields().at(idx);
    if (f.type != drivers::DbfFieldType::Memo) {
        return util::Error{5063, 0, "field is not a memo column", ""};
    }
    if (!memo_) {
        return util::Error{5004, 0, "memo store not attached", ""};
    }
    auto snap = snapshot_index_keys_();
    auto wm = memo_->write_typed(payload, type);
    if (!wm) return wm.error();
    char buf[16];
    int n = std::snprintf(buf, sizeof(buf), "%*u",
                          static_cast<int>(f.length),
                          static_cast<unsigned>(wm.value()));
    if (n < 0 || static_cast<std::size_t>(n) > f.length) {
        return util::Error{5000, 0, "memo block number overflows field", ""};
    }
    std::memcpy(record_buf_.data() + f.record_offset, buf, f.length);
    if (auto wb = writeback_record_(); !wb) return wb.error();
    return sync_all_indexes_(snap);
}

util::Result<drivers::MemoBlockType>
Table::field_memo_type(std::uint16_t idx) {
    if (state_ != State::Positioned) {
        return util::Error{5000, 0, "no record positioned", ""};
    }
    if (idx >= driver_->fields().size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    const auto& f = driver_->fields().at(idx);
    if (f.type != drivers::DbfFieldType::Memo || !memo_) {
        return drivers::MemoBlockType::Text;
    }
    std::string raw(reinterpret_cast<const char*>(record_buf_.data() +
                                                  f.record_offset),
                    f.length);
    while (!raw.empty() && raw.front() == ' ') raw.erase(raw.begin());
    while (!raw.empty() && raw.back()  == ' ') raw.pop_back();
    if (raw.empty()) return drivers::MemoBlockType::Text;
    std::uint32_t block_no = 0;
    for (char c : raw) {
        if (c < '0' || c > '9') return drivers::MemoBlockType::Text;
        block_no = block_no * 10 + static_cast<std::uint32_t>(c - '0');
    }
    if (block_no == 0) return drivers::MemoBlockType::Text;
    return memo_->read_type(block_no);
}

util::Result<void> Table::mark_deleted() {
    if (state_ != State::Positioned) {
        return util::Error{5000, 0, "no record positioned", ""};
    }
    drivers::set_record_deleted(record_buf_.data(), record_buf_.size(), true);
    return writeback_record_();
}

util::Result<void> Table::recall_deleted() {
    if (state_ != State::Positioned) {
        return util::Error{5000, 0, "no record positioned", ""};
    }
    drivers::set_record_deleted(record_buf_.data(), record_buf_.size(), false);
    return writeback_record_();
}

bool Table::is_deleted() const noexcept {
    if (state_ != State::Positioned) return false;
    return drivers::record_is_deleted(record_buf_.data(), record_buf_.size());
}

util::Result<void> Table::zap() {
    if (mode_ == OpenMode::Read) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    if (driver_ == nullptr) {
        return util::Error{5000, 0, "no driver", ""};
    }
    // Walk every bound index and erase its entries before truncating
    // the DBF; otherwise stale (recno, key) pairs remain.
    auto erase_all = [&](drivers::IIndex* idx) -> util::Result<void> {
        if (idx == nullptr) return {};
        std::vector<std::pair<std::uint32_t, std::string>> entries;
        auto seek = idx->seek_first();
        while (seek && seek.value().positioned) {
            entries.emplace_back(seek.value().recno, idx->current_key());
            seek = idx->next();
        }
        for (auto& [rec, key] : entries) {
            (void)idx->erase(rec, key);
        }
        if (auto fl = idx->flush(); !fl) return fl.error();
        return {};
    };
    if (order_ && order_->index()) {
        if (auto r = erase_all(order_->index()); !r) return r.error();
    }
    for (auto* x : extra_index_views_) {
        if (auto r = erase_all(x); !r) return r.error();
    }
    if (auto r = driver_->zap(); !r) return r.error();
    state_ = State::Bof;
    recno_ = 0;
    record_buf_.assign(driver_->record_length(), 0);
    return {};
}

util::Result<void> Table::pack() {
    if (mode_ == OpenMode::Read) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    if (driver_ == nullptr) {
        return util::Error{5000, 0, "no driver", ""};
    }
    // 1) Copy live records down. Iterate from recno 1; track destination.
    std::uint32_t dst = 0;
    std::uint32_t total = driver_->record_count();
    for (std::uint32_t src = 1; src <= total; ++src) {
        if (auto g = goto_record(src); !g) return g.error();
        if (is_deleted()) continue;
        ++dst;
        if (dst != src) {
            if (auto w = driver_->write_record_raw(dst, record_buf_.data(),
                                                   record_buf_.size()); !w) {
                return w.error();
            }
        }
    }
    // 2) Truncate the on-disk record count to `dst` by saving the
    //    survivors, zapping the driver (DBF-only — does NOT touch
    //    bound indexes), and re-appending. Pack matches Clipper's
    //    semantics: indexes are left stale, the caller must REINDEX.
    std::vector<std::vector<std::uint8_t>> survivors;
    survivors.reserve(dst);
    for (std::uint32_t i = 1; i <= dst; ++i) {
        auto rec = driver_->read_record_raw(i);
        if (!rec) return rec.error();
        survivors.push_back(std::move(rec).value());
    }
    if (auto r = driver_->zap(); !r) return r.error();
    for (auto& buf : survivors) {
        auto a = driver_->append_record_raw(buf.data(), buf.size());
        if (!a) return a.error();
    }
    state_ = State::Bof;
    recno_ = 0;
    record_buf_.assign(driver_->record_length(), 0);
    return {};
}

util::Result<void> Table::reindex() {
    if (mode_ == OpenMode::Read) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    if (driver_ == nullptr) return {};

    // 1) Clear every bound index. Re-using the same erase walk that
    //    zap() uses keeps the IIndex file structurally intact.
    auto erase_all = [&](drivers::IIndex* idx) -> util::Result<void> {
        if (idx == nullptr) return {};
        std::vector<std::pair<std::uint32_t, std::string>> entries;
        auto seek = idx->seek_first();
        while (seek && seek.value().positioned) {
            entries.emplace_back(seek.value().recno, idx->current_key());
            seek = idx->next();
        }
        for (auto& [rec, key] : entries) {
            (void)idx->erase(rec, key);
        }
        return {};
    };
    if (order_ && order_->index()) {
        if (auto r = erase_all(order_->index()); !r) return r.error();
    }
    for (auto* x : extra_index_views_) {
        if (auto r = erase_all(x); !r) return r.error();
    }

    // 2) Walk every live record and re-insert into each index using
    //    its current key expression. Snapshot is built with empty
    //    prev_keys so sync_all_indexes_ skips the (unneeded) erase
    //    pass and performs only the insert.
    std::vector<std::pair<drivers::IIndex*, std::string>> snap;
    if (order_ && order_->index()) snap.emplace_back(order_->index(),
                                                     std::string{});
    for (auto* x : extra_index_views_) {
        if (x) snap.emplace_back(x, std::string{});
    }
    auto rec_count = driver_->record_count();
    for (std::uint32_t r = 1; r <= rec_count; ++r) {
        if (auto g = goto_record(r); !g) return g.error();
        if (is_deleted()) continue;
        if (auto s = sync_all_indexes_(snap); !s) return s.error();
    }

    // 3) Flush every index so the rebuilt entries hit disk before the
    //    caller resumes work.
    if (order_ && order_->index()) {
        if (auto r = order_->index()->flush(); !r) return r.error();
    }
    for (auto* x : extra_index_views_) {
        if (x == nullptr) continue;
        if (auto r = x->flush(); !r) return r.error();
    }
    state_ = State::Bof;
    recno_ = 0;
    return {};
}

util::Result<void> Table::flush() {
    if (auto r = driver_->flush(); !r) return r.error();
    if (order_ && order_->index()) {
        if (auto r = order_->index()->flush(); !r) return r.error();
    }
    for (auto* extra : extra_index_views_) {
        if (extra == nullptr) continue;
        if (auto r = extra->flush(); !r) return r.error();
    }
    return {};
}

TableTypeForLock Table::to_lock_type_() const noexcept {
    switch (type_) {
        case TableType::Cdx: return TableTypeForLock::Cdx;
        case TableType::Ntx: return TableTypeForLock::Ntx;
        case TableType::Adt: return TableTypeForLock::Adt;
        case TableType::Vfp: return TableTypeForLock::Vfp;
    }
    return TableTypeForLock::Cdx;
}

util::Result<void> Table::lock_record_excl(std::uint32_t recno) {
    if (mode_ == OpenMode::Read) return {};
    auto h = locks_.lock_record_excl(driver_->file(), to_lock_type_(),
                                     locking_, recno);
    if (!h) return h.error();
    recno_locks_.emplace(recno, std::move(h).value());
    return {};
}

util::Result<void> Table::try_lock_record_excl(std::uint32_t recno) {
    if (mode_ == OpenMode::Read) return {};
    auto h = locks_.try_lock_record_excl(driver_->file(), to_lock_type_(),
                                         locking_, recno);
    if (!h) return h.error();
    recno_locks_.emplace(recno, std::move(h).value());
    return {};
}

util::Result<void> Table::unlock_record(std::uint32_t recno) {
    auto it = recno_locks_.find(recno);
    if (it != recno_locks_.end()) {
        it->second.release();
        recno_locks_.erase(it);
    }
    return {};
}

util::Result<void> Table::lock_table_excl() {
    if (mode_ == OpenMode::Read) return {};
    auto h = locks_.lock_table_excl(driver_->file(), to_lock_type_(), locking_);
    if (!h) return h.error();
    table_lock_ = std::move(h).value();
    return {};
}

std::vector<std::uint32_t> Table::held_record_locks() const {
    std::vector<std::uint32_t> out;
    out.reserve(recno_locks_.size());
    for (auto& [recno, _] : recno_locks_) out.push_back(recno);
    std::sort(out.begin(), out.end());
    return out;
}

util::Result<void> Table::try_lock_table_excl() {
    if (mode_ == OpenMode::Read) return {};
    auto h = locks_.try_lock_table_excl(driver_->file(), to_lock_type_(),
                                        locking_);
    if (!h) return h.error();
    table_lock_ = std::move(h).value();
    return {};
}

util::Result<void> Table::unlock_table() {
    if (table_lock_) {
        table_lock_->release();
        table_lock_.reset();
    }
    return {};
}

void Table::attach_memo(std::unique_ptr<drivers::IMemoStore> memo) {
    memo_ = std::move(memo);
}

void Table::set_order(std::unique_ptr<drivers::IIndex> idx) {
    order_.emplace(std::move(idx));
}

void Table::clear_order() {
    order_.reset();
}

std::unique_ptr<drivers::IIndex> Table::take_order() {
    if (!order_) return nullptr;
    auto idx = order_->release();
    order_.reset();
    return idx;
}

void Table::register_extra_index_view(drivers::IIndex* idx) {
    if (idx == nullptr) return;
    for (auto* v : extra_index_views_) if (v == idx) return;
    extra_index_views_.push_back(idx);
}

void Table::unregister_extra_index_view(drivers::IIndex* idx) {
    extra_index_views_.erase(
        std::remove(extra_index_views_.begin(), extra_index_views_.end(), idx),
        extra_index_views_.end());
}

void Table::clear_extra_index_views() {
    extra_index_views_.clear();
}

util::Result<bool>
Table::seek_key(const std::string& key, bool soft) {
    if (!order_ || !order_->index()) {
        return util::Error{6105, 0, "no active index for seek", ""};
    }
    auto r = order_->index()->seek_key(key, soft);
    if (!r) return r.error();
    if (!r.value().positioned) {
        state_ = State::Eof; recno_ = 0;
        last_seek_found_ = false;
        return false;
    }
    auto load = load_record_(r.value().recno);
    if (!load) return load.error();
    bool exact = r.value().hit == drivers::SeekHit::Exact;
    last_seek_found_ = exact;
    return exact;
}

util::Result<void> Table::set_scope(bool top, const std::string& key) {
    if (!order_) {
        return util::Error{6105, 0, "no active index for scope", ""};
    }
    if (top) order_->scope().top    = key;
    else     order_->scope().bottom = key;
    return {};
}

util::Result<void> Table::clear_scope(bool top) {
    if (!order_) return {};
    if (top) order_->scope().top.reset();
    else     order_->scope().bottom.reset();
    return {};
}

util::Result<void> Table::clear_scopes() {
    if (!order_) return {};
    order_->scope().top.reset();
    order_->scope().bottom.reset();
    return {};
}

std::optional<std::string> Table::get_scope(bool top) const {
    if (!order_) return std::nullopt;
    return top ? order_->scope().top : order_->scope().bottom;
}

} // namespace openads::engine
