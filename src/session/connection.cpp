#include "session/connection.h"

#include "drivers/cdx/cdx_driver.h"
#include "drivers/dbt/dbt_memo.h"
#include "drivers/fpt/fpt_memo.h"
#include "drivers/ntx/ntx_driver.h"
#include "platform/path.h"

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openads::session {

util::Result<Connection> Connection::open(const std::string& data_dir) {
    namespace fs = std::filesystem;
    Connection c;

    // If the input ends with ".add", treat it as a Data Dictionary path
    // and use its parent as the data directory.
    fs::path p(data_dir);
    bool is_add = p.has_extension() &&
        (p.extension() == ".add" || p.extension() == ".ADD");

    std::string actual_dir;
    if (is_add) {
        actual_dir = p.parent_path().string();
        if (actual_dir.empty()) actual_dir = ".";
    } else {
        actual_dir = data_dir;
    }

    std::error_code ec;
    if (!fs::is_directory(actual_dir, ec)) {
        return util::Error{5103, 0, "data directory not found", actual_dir};
    }
    c.data_dir_ = actual_dir;

    if (is_add) {
        if (fs::exists(p, ec)) {
            auto dd = engine::DataDict::open(p.string());
            if (!dd) return dd.error();
            c.dd_ = std::move(dd).value();
        }
    }

    fs::path log_path = fs::path(actual_dir) / "openads.txlog";
    auto lr = c.tx_log_.open(log_path.string());
    if (!lr) return lr.error();

    fs::path map_path = fs::path(actual_dir) / "openads.lsnmap";
    if (auto mr = c.lsn_map_.open(map_path.string()); !mr) return mr.error();

    if (auto rr = c.recover_orphan_tx_(); !rr) return rr.error();
    return c;
}

util::Result<Handle> Connection::open_table(const std::string& relative_path,
                                            engine::TableType  type,
                                            engine::OpenMode   mode,
                                            engine::LockingMode locking) {
    namespace fs = std::filesystem;
    std::string effective = relative_path;
    if (dd_.has_value()) effective = dd_->resolve(relative_path);
    fs::path full = fs::path(data_dir_) / effective;
    auto resolved = platform::resolve_case_insensitive(full.string());

    auto t = engine::Table::open(resolved, type, mode, locking);
    if (!t) return t.error();

    auto holder = std::make_unique<engine::Table>(std::move(t).value());

    // Auto-attach a memo store if the table has M-fields.
    bool has_memo_field = false;
    for (std::uint16_t i = 0; i < holder->field_count(); ++i) {
        if (holder->field_descriptor(i).type ==
            openads::drivers::DbfFieldType::Memo) {
            has_memo_field = true;
            break;
        }
    }
    if (has_memo_field) {
        fs::path stem = fs::path(resolved);
        auto memo_open_mode =
            (mode == engine::OpenMode::Read) ? openads::drivers::MemoOpenMode::ReadOnly
                                             : openads::drivers::MemoOpenMode::Shared;
        if (type == engine::TableType::Ntx) {
            stem.replace_extension(".dbt");
            std::error_code ec;
            if (fs::exists(stem, ec)) {
                auto m = std::make_unique<openads::drivers::dbt::DbtMemo>();
                if (m->open(stem.string(), memo_open_mode)) {
                    holder->attach_memo(std::move(m));
                }
            }
        } else {
            stem.replace_extension(".fpt");
            std::error_code ec;
            if (fs::exists(stem, ec)) {
                auto m = std::make_unique<openads::drivers::fpt::FptMemo>();
                if (m->open(stem.string(), memo_open_mode)) {
                    holder->attach_memo(std::move(m));
                }
            }
        }
    }

    Handle h = next_table_handle_++;
    if (tx_.active()) {
        holder->attach_tx(&tx_, static_cast<engine::Tx::TableId>(h));
        tx_.register_table(static_cast<engine::Tx::TableId>(h), relative_path);
    }
    tables_.emplace(h, std::move(holder));
    table_paths_.emplace(h, relative_path);
    return h;
}

util::Result<void> Connection::begin_tx() {
    if (tx_.active()) {
        return util::Error{5000, 0, "transaction already active", ""};
    }
    std::uint64_t tid = next_tx_id_++;
    tx_.activate(tid, &tx_log_);
    if (auto r = tx_log_.append_begin(tid); !r) return r.error();
    for (auto& [h, holder] : tables_) {
        holder->attach_tx(&tx_, static_cast<engine::Tx::TableId>(h));
        auto pit = table_paths_.find(h);
        if (pit != table_paths_.end()) {
            tx_.register_table(static_cast<engine::Tx::TableId>(h), pit->second);
        }
    }
    return {};
}

util::Result<void> Connection::commit_tx() {
    if (!tx_.active()) {
        return util::Error{5000, 0, "no active transaction", ""};
    }
    if (auto r = tx_log_.append_commit(tx_.id()); !r) return r.error();
    for (auto& [h, holder] : tables_) {
        (void)h;
        holder->detach_tx();
        (void)holder->flush();
    }
    tx_.clear();
    (void)tx_log_.truncate();
    return {};
}

util::Result<void> Connection::rollback_tx() {
    if (!tx_.active()) {
        return util::Error{5000, 0, "no active transaction", ""};
    }
    tx_.for_each_before_image(
        [&](const engine::Tx::RecordKey& k,
            const std::vector<std::uint8_t>& bytes) {
            auto it = tables_.find(static_cast<Handle>(k.table));
            if (it == tables_.end()) return;
            auto* drv = it->second->driver();
            if (drv) {
                (void)drv->write_record_raw(k.recno, bytes.data(), bytes.size());
            }
        });
    tx_.for_each_append([&](const engine::Tx::RecordKey& k) {
        auto it = tables_.find(static_cast<Handle>(k.table));
        if (it == tables_.end()) return;
        auto* drv = it->second->driver();
        if (!drv) return;
        auto rec = drv->read_record_raw(k.recno);
        if (!rec) return;
        auto buf = std::move(rec).value();
        openads::drivers::set_record_deleted(buf.data(), buf.size(), true);
        (void)drv->write_record_raw(k.recno, buf.data(), buf.size());
    });
    if (auto r = tx_log_.append_abort(tx_.id()); !r) return r.error();
    for (auto& [h, holder] : tables_) {
        (void)h;
        holder->detach_tx();
        (void)holder->flush();
    }
    tx_.clear();
    (void)tx_log_.truncate();
    return {};
}

util::Result<void>
Connection::create_savepoint(const std::string& name) {
    if (!tx_.active()) {
        return util::Error{5000, 0, "no active transaction", ""};
    }
    tx_.create_savepoint(name);
    return {};
}

util::Result<void>
Connection::rollback_to_savepoint(const std::string& name) {
    if (!tx_.active()) {
        return util::Error{5000, 0, "no active transaction", ""};
    }
    std::size_t idx = tx_.savepoint_index(name);
    if (idx == static_cast<std::size_t>(-1)) {
        return util::Error{5000, 0, "savepoint not found", name};
    }
    const auto& ops = tx_.ops();
    for (std::size_t i = ops.size(); i > idx; --i) {
        const auto& op = ops[i - 1];
        auto it = tables_.find(static_cast<Handle>(op.table));
        if (it == tables_.end()) continue;
        auto* drv = it->second->driver();
        if (!drv) continue;
        if (op.is_append) {
            auto rec = drv->read_record_raw(op.recno);
            if (!rec) continue;
            auto buf = std::move(rec).value();
            openads::drivers::set_record_deleted(buf.data(), buf.size(), true);
            (void)drv->write_record_raw(op.recno, buf.data(), buf.size());
        } else {
            (void)drv->write_record_raw(op.recno,
                                        op.before.data(), op.before.size());
        }
    }
    tx_.truncate_ops_to(idx);
    return {};
}

util::Result<void> Connection::recover_orphan_tx_() {
    auto recs_r = tx_log_.read_all();
    if (!recs_r) return recs_r.error();
    auto recs = std::move(recs_r).value();
    if (recs.empty()) return {};

    // Group records by tx_id and find which transactions ended.
    std::unordered_set<std::uint64_t> ended;
    for (const auto& r : recs) {
        if (r.type == engine::TxRecordType::Commit ||
            r.type == engine::TxRecordType::Abort) {
            ended.insert(r.tx_id);
        }
    }

    // For each orphan tx, walk its UPDATEs in reverse and write back
    // the before-image directly through the driver. Open each
    // referenced table on demand.
    std::unordered_map<std::string, std::unique_ptr<engine::Table>> opened;
    auto open_for = [&](const std::string& path) -> engine::Table* {
        auto it = opened.find(path);
        if (it != opened.end()) return it->second.get();
        std::filesystem::path full =
            std::filesystem::path(data_dir_) / path;
        auto resolved = platform::resolve_case_insensitive(full.string());
        // Detect type by extension. For now any non-NTX is treated as CDX.
        auto type = engine::TableType::Cdx;
        if (resolved.size() >= 4 &&
            (resolved.substr(resolved.size() - 4) == ".ntx")) {
            type = engine::TableType::Ntx;
        }
        auto t = engine::Table::open(resolved, type, engine::OpenMode::Shared);
        if (!t) return nullptr;
        auto h = std::make_unique<engine::Table>(std::move(t).value());
        engine::Table* raw = h.get();
        opened.emplace(path, std::move(h));
        return raw;
    };

    // Build list of orphan UPDATE records in reverse log order.
    std::vector<const engine::TxRecord*> orphan_updates;
    for (auto it = recs.rbegin(); it != recs.rend(); ++it) {
        if (it->type == engine::TxRecordType::Update &&
            ended.find(it->tx_id) == ended.end()) {
            orphan_updates.push_back(&*it);
        }
    }
    for (const engine::TxRecord* up : orphan_updates) {
        // Skip records that a previous (interrupted) recovery pass has
        // already applied. lsn_map_ tracks the highest LSN applied per
        // (table_path, recno); any record whose LSN is <= the stored
        // value is idempotent and can be skipped.
        if (lsn_map_.get(up->update.table_path, up->update.recno) >= up->lsn) {
            continue;
        }
        engine::Table* t = open_for(up->update.table_path);
        if (!t || !t->driver()) continue;
        if (!up->update.before.empty()) {
            (void)t->driver()->write_record_raw(
                up->update.recno,
                up->update.before.data(),
                up->update.before.size());
            lsn_map_.put(up->update.table_path, up->update.recno, up->lsn);
        }
    }
    // Append ABORT for each orphan tx so the log is well-formed.
    std::unordered_set<std::uint64_t> orphan_ids;
    for (const auto& r : recs) {
        if (ended.find(r.tx_id) == ended.end()) orphan_ids.insert(r.tx_id);
    }
    for (auto id : orphan_ids) (void)tx_log_.append_abort(id);

    // Flush any open tables, persist the lsn_map, then truncate the
    // log. If the process crashes between the lsn_map flush and the
    // log truncate, the next recovery pass will see the same orphan
    // records, look up their LSN in the lsn_map, and skip the redo —
    // making the recovery path crash-safe across multiple iterations.
    for (auto& [_, t] : opened) (void)t->flush();
    (void)lsn_map_.flush();
    (void)tx_log_.truncate();
    // Once the log is empty, the lsn_map's contents are no longer
    // needed for correctness; drop them so the sidecar does not grow
    // unbounded across the connection's lifetime.
    lsn_map_.clear();
    (void)lsn_map_.flush();
    return {};
}

void Connection::close_table(Handle h) {
    tables_.erase(h);
    table_paths_.erase(h);
}

engine::Table* Connection::lookup_table(Handle h) {
    auto it = tables_.find(h);
    if (it == tables_.end()) return nullptr;
    return it->second.get();
}

} // namespace openads::session
