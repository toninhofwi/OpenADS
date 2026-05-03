#pragma once

#include "engine/data_dict.h"
#include "engine/table.h"
#include "engine/tx.h"
#include "engine/tx_log.h"
#include "session/handle_registry.h"
#include "util/result.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace openads::session {

class Connection {
public:
    Connection() = default;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection&&) noexcept = default;

    static util::Result<Connection> open(const std::string& data_dir);

    util::Result<Handle>
        open_table(const std::string& relative_path,
                   engine::TableType  type,
                   engine::OpenMode   mode = engine::OpenMode::Shared,
                   engine::LockingMode locking = engine::LockingMode::Compatible);

    void close_table(Handle h);

    engine::Table* lookup_table(Handle h);

    const std::string& data_dir() const noexcept { return data_dir_; }

    // Transaction surface (M5).
    util::Result<void> begin_tx();
    util::Result<void> commit_tx();
    util::Result<void> rollback_tx();
    util::Result<void> create_savepoint(const std::string& name);
    util::Result<void> rollback_to_savepoint(const std::string& name);
    bool               in_tx() const noexcept { return tx_.active(); }

    // Data Dictionary surface (M6).
    bool has_dd() const noexcept { return dd_.has_value(); }
    engine::DataDict* dd() noexcept {
        return dd_.has_value() ? &*dd_ : nullptr;
    }

private:
    util::Result<void> recover_orphan_tx_();

    std::string                                                data_dir_;
    std::unordered_map<Handle, std::unique_ptr<engine::Table>> tables_;
    std::unordered_map<Handle, std::string>                    table_paths_;
    Handle                                                     next_table_handle_ = 1;

    engine::TxLog                                              tx_log_;
    engine::Tx                                                 tx_;
    std::uint64_t                                              next_tx_id_ = 1;

    std::optional<engine::DataDict>                            dd_;
};

} // namespace openads::session
