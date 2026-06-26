#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openads::sql_backend {

class PostgresConnection;

struct PostgresTable {
    PostgresConnection* conn = nullptr;
    std::string         name;

    struct FieldDesc {
        std::string   name;
        std::uint16_t type     = 0;
        std::uint32_t length   = 0;
        std::uint16_t decimals = 0;
        bool          nullable = true;
        std::string   default_value;   // information_schema column_default ("" = none)
    };

    std::vector<FieldDesc> fields;
    bool                   fields_cached = false;

    std::vector<std::string> current_row;
    std::vector<bool>        current_nulls;
    bool                     row_valid = false;

    std::uint32_t current_recno      = 0;
    bool          current_deleted    = false;
    std::uint32_t cached_rec_count   = 0;
    bool          rec_count_cached     = false;

    std::vector<std::string> pk_columns;
    struct PkRow {
        std::vector<std::string> values;
    };
    std::vector<PkRow>       pk_snapshot;
    std::size_t              pos              = 0;
    bool                     positioned       = false;
    bool                     last_seek_found  = false;

    // Write staging (mirrors the FirebirdTable model): append_blank/set_field
    // stage column values here; flush_record turns them into an INSERT (when
    // pending_append) or an UPDATE keyed by the positioned row's PK.
    std::vector<std::string> staging_row;
    std::vector<bool>        staging_nulls;
    bool                     pending_append = false;
    bool                     row_dirty      = false;
};

} // namespace openads::sql_backend