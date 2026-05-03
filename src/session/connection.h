#pragma once

#include "engine/table.h"
#include "session/handle_registry.h"
#include "util/result.h"

#include <cstdint>
#include <memory>
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
                   engine::TableType  type);

    void close_table(Handle h);

    engine::Table* lookup_table(Handle h);

    const std::string& data_dir() const noexcept { return data_dir_; }

private:
    std::string                                                data_dir_;
    std::unordered_map<Handle, std::unique_ptr<engine::Table>> tables_;
    Handle                                                     next_table_handle_ = 1;
};

} // namespace openads::session
