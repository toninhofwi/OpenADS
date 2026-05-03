#include "session/connection.h"

#include "platform/path.h"

#include <filesystem>
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
                                            engine::TableType  type) {
    namespace fs = std::filesystem;
    fs::path full = fs::path(data_dir_) / relative_path;
    auto resolved = platform::resolve_case_insensitive(full.string());

    auto t = engine::Table::open(resolved, type);
    if (!t) return t.error();

    auto holder = std::make_unique<engine::Table>(std::move(t).value());
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
