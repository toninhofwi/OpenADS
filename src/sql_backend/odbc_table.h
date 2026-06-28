#pragma once

#include "sql_backend/backend_field_optimizer.h"
#include "sql_backend/backend_where_builder.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace openads::sql_backend {

class OdbcConnection;

// Read-only v1 table state behind the ACE ABI. Navigation uses a
// primary-key snapshot loaded once at open_table: the ordered list of
// PK tuples is materialised in memory, so GO TOP / SKIP / GO BOTTOM are
// index arithmetic and the row payload is loaded lazily by PK. This is
// the most portable form of "PK snapshot" navigation — it needs only
// COUNT / SELECT / WHERE / ORDER BY, which every ODBC driver supports
// (no LIMIT / OFFSET / TOP, no scrollable-cursor dependency).
struct OdbcTable {
    OdbcConnection* conn = nullptr;
    std::string     name;       // ABI / AdsOpenTable name (caller casing)
    std::string     sql_table;  // driver-reported TABLE_NAME for SQL

    struct FieldDesc {
        std::string   name;
        std::uint16_t type        = 0;
        std::uint32_t length      = 0;
        std::uint16_t decimals    = 0;
        bool          nullable    = true;
        int           sql_type    = 0;   // raw ODBC SQL_* type code
        std::uint32_t column_size = 0;   // raw COLUMN_SIZE
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

    // --- write staging (navigational append / update via SQL) ---
    // Field values set since the last AppendRecord (appending) or since
    // navigating onto a row (edit), keyed by the driver-reported column
    // name. flush_table emits one INSERT (appending) or UPDATE (edit).
    std::vector<std::pair<std::string, std::string>> staged;
    bool appending = false;

    std::string where_filter;
    BackendFieldOptimizer field_optimizer;
    BackendWhereBuilder   where_builder;
};

} // namespace openads::sql_backend
