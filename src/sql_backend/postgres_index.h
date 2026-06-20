#pragma once

#include <string>

namespace openads::sql_backend {

struct PostgresTable;

struct PostgresIndex {
    PostgresTable* parent          = nullptr;
    std::string    column;
    bool           last_seek_found = false;
};

} // namespace openads::sql_backend