#pragma once

#include <string>

namespace openads::sql_backend {

struct MssqlTable;

struct MssqlIndex {
    MssqlTable* parent          = nullptr;
    std::string column;
    bool        last_seek_found = false;
};

}  // namespace openads::sql_backend