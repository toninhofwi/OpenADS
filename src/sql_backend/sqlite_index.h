#pragma once

#include <string>

namespace openads::sql_backend {

struct SqliteTable;

// Logical index on a single SQLite column (OpenADS Plus). No .cdx file —
// AdsOpenIndex on a SqliteTable registers one of these per tag/column.
struct SqliteIndex {
    SqliteTable* parent           = nullptr;
    std::string  column;
    bool         last_seek_found  = false;
};

} // namespace openads::sql_backend