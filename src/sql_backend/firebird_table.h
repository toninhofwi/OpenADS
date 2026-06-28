#pragma once

#include "sql_backend/backend_field_optimizer.h"
#include "sql_backend/backend_where_builder.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openads::sql_backend {

class FirebirdConnection;

// Table state behind the ACE ABI for the native Firebird backend. Like the
// ODBC driver, navigation uses a primary-key snapshot loaded once at
// open_table: the ordered list of PK tuples lives in memory, so GO TOP /
// SKIP / GO BOTTOM are index arithmetic and the row payload is loaded lazily
// by PK. Write support reuses the maria-style staging buffer: edits land in
// staging_row and are flushed as INSERT / UPDATE on flush_record.
struct FirebirdTable {
    FirebirdConnection* conn = nullptr;
    std::string         name;       // ABI / AdsOpenTable name (caller casing)
    std::uint32_t       connect_handle = 0;
    std::uint16_t       check_rights   = 0;
    std::uint16_t       aof_opt_level  = 0;
    std::string         sql_table;  // catalog name used for SQL (folded upper)

    struct FieldDesc {
        std::string   name;
        std::uint16_t type     = 0;  // ADS_* type code
        std::uint32_t length   = 0;
        std::uint16_t decimals = 0;
        bool          nullable = true;
    };

    std::vector<FieldDesc> fields;
    bool                   fields_cached = false;

    std::vector<std::string> current_row;
    std::vector<bool>        current_nulls;
    bool                     row_valid = false;

    std::uint32_t current_recno    = 0;
    bool          current_deleted  = false;
    std::uint32_t cached_rec_count = 0;
    bool          rec_count_cached = false;

    std::vector<std::string> pk_columns;
    struct PkRow {
        std::vector<std::string> values;
        bool operator==(const PkRow& o) const { return values == o.values; }
    };
    // PK snapshot: every row's PK tuple, ordered by PK ascending.
    std::vector<PkRow> pk_snapshot;

    std::size_t pos             = 0;
    bool        positioned      = false;
    bool        last_seek_found = false;

    // Write staging (maria-style): set_field writes here; flush_record turns
    // a pending append into INSERT and a dirty positioned row into UPDATE.
    bool                     pending_append = false;
    bool                     row_dirty      = false;
    std::vector<std::string> staging_row;
    std::vector<bool>        staging_nulls;

    // Result-set cursor (AdsExecuteSQLDirect SELECT passthrough): rows are
    // materialized in memory; navigation serves current_row from result_rows.
    bool                                  is_result = false;
    std::vector<std::vector<std::string>> result_rows;
    std::vector<std::vector<bool>>        result_nulls;

    std::string where_filter;
    BackendFieldOptimizer field_optimizer;
    BackendWhereBuilder   where_builder;
};

} // namespace openads::sql_backend
