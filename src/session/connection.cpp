#include "session/connection.h"

#include "drivers/dbt/dbt_memo.h"
#include "drivers/fpt/fpt_memo.h"
#include "platform/path.h"

#include <filesystem>
#include <memory>
#include <utility>

namespace openads::session {

util::Result<Connection> Connection::open(const std::string& data_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(data_dir, ec)) {
        return util::Error{5103, 0, "data directory not found", data_dir};
    }
    Connection c;
    c.data_dir_ = data_dir;
    return c;
}

util::Result<Handle> Connection::open_table(const std::string& relative_path,
                                            engine::TableType  type,
                                            engine::OpenMode   mode,
                                            engine::LockingMode locking) {
    namespace fs = std::filesystem;
    fs::path full = fs::path(data_dir_) / relative_path;
    auto resolved = platform::resolve_case_insensitive(full.string());

    auto t = engine::Table::open(resolved, type, mode, locking);
    if (!t) return t.error();

    auto holder = std::make_unique<engine::Table>(std::move(t).value());

    // Auto-attach a memo store if the table has M-fields and a sibling
    // memo file exists. CDX/VFP tables look for .fpt; NTX (Clipper)
    // looks for .dbt.
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
    tables_.emplace(h, std::move(holder));
    return h;
}

void Connection::close_table(Handle h) {
    tables_.erase(h);
}

engine::Table* Connection::lookup_table(Handle h) {
    auto it = tables_.find(h);
    if (it == tables_.end()) return nullptr;
    return it->second.get();
}

} // namespace openads::session
