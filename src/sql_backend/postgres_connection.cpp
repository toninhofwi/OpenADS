#include "sql_backend/postgres_connection.h"

#include "sql_backend/postgres_backend.h"
#include "sql_backend/sql_common.h"

#include <algorithm>

#if defined(OPENADS_WITH_POSTGRESQL)
#include <libpq-fe.h>
#endif

namespace openads::sql_backend {

namespace {

#if defined(OPENADS_WITH_POSTGRESQL)

util::Result<void> load_current_row(PGconn* conn, PostgresTable* tbl);

util::Result<std::vector<PostgresTable::FieldDesc>>
describe_table_impl(PGconn* conn, PostgresTable* tbl) {
    if (!is_safe_identifier(tbl->name)) {
        return util::Error{5001, 0, "invalid table name", tbl->name};
    }
    const char* params[1] = {tbl->name.c_str()};
    PGresult* res = PQexecParams(
        conn,
        "SELECT column_name, data_type, is_nullable, "
        "character_maximum_length, numeric_precision, numeric_scale "
        "FROM information_schema.columns "
        "WHERE table_schema = ANY (current_schemas(true)) "
        "AND table_name = $1 "
        "ORDER BY ordinal_position",
        1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        const char* msg = PQerrorMessage(conn);
        PQclear(res);
        return postgres_error("describe_table", msg);
    }
    const int rows = PQntuples(res);
    if (rows <= 0) {
        PQclear(res);
        return util::Error{5001, 0, "table not found or has no columns",
                           tbl->name};
    }
    std::vector<PostgresTable::FieldDesc> out;
    out.reserve(static_cast<std::size_t>(rows));
    for (int r = 0; r < rows; ++r) {
        const char* nullable = PQgetvalue(res, r, 2);
        out.push_back(map_pg_column(
            PQgetvalue(res, r, 0),
            PQgetvalue(res, r, 1),
            nullable && nullable[0] == 'Y',
            std::atoi(PQgetvalue(res, r, 3)),
            std::atoi(PQgetvalue(res, r, 4)),
            std::atoi(PQgetvalue(res, r, 5))));
    }
    PQclear(res);
    tbl->fields        = out;
    tbl->fields_cached = true;
    return out;
}

util::Result<void> position_at_ctid(PGconn* conn, PostgresTable* tbl,
                                    const std::string& ctid) {
    if (tbl == nullptr || conn == nullptr) {
        return util::Error{5001, 0, "invalid postgres table state", ""};
    }
    auto it = std::find(tbl->ctids.begin(), tbl->ctids.end(), ctid);
    if (it == tbl->ctids.end()) {
        tbl->positioned = false;
        tbl->row_valid  = false;
        return util::Result<void>{};
    }
    tbl->pos           = static_cast<std::size_t>(
        std::distance(tbl->ctids.begin(), it));
    tbl->positioned    = true;
    tbl->current_recno = static_cast<std::uint32_t>(tbl->pos + 1);
    return load_current_row(conn, tbl);
}

util::Result<void> load_current_row(PGconn* conn, PostgresTable* tbl) {
    if (tbl == nullptr || conn == nullptr) {
        return util::Error{5001, 0, "invalid postgres table state", ""};
    }
    if (!tbl->positioned) {
        tbl->row_valid      = false;
        tbl->current_row.clear();
        tbl->current_nulls.clear();
        return util::Result<void>{};
    }
    if (!tbl->fields_cached) {
        auto d = describe_table_impl(conn, tbl);
        if (!d) return d.error();
    }

    const std::string sql =
        "SELECT * FROM \"" + tbl->name + "\" WHERE ctid = $1::tid";
    const std::string& ctid = tbl->ctids[tbl->pos];
    const char* params[1]   = {ctid.c_str()};
    PGresult* res = PQexecParams(conn, sql.c_str(), 1, nullptr, params,
                                 nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        const char* msg = PQerrorMessage(conn);
        PQclear(res);
        return postgres_error("load row", msg);
    }

    tbl->current_row.clear();
    tbl->current_nulls.clear();
    tbl->row_valid = false;

    if (PQntuples(res) == 1) {
        const int cols = PQnfields(res);
        tbl->current_row.resize(static_cast<std::size_t>(cols));
        tbl->current_nulls.resize(static_cast<std::size_t>(cols));
        for (int c = 0; c < cols; ++c) {
            bool is_null = false;
            tbl->current_row[c] = format_pg_value(res, 0, c, is_null);
            tbl->current_nulls[c] = is_null;
        }
        tbl->row_valid = true;
    } else {
        tbl->positioned = false;
    }
    PQclear(res);
    return util::Result<void>{};
}

#endif

} // namespace

struct PostgresConnection::Impl {
#if defined(OPENADS_WITH_POSTGRESQL)
    PGconn* conn = nullptr;
#endif
};

PostgresConnection::PostgresConnection() = default;
PostgresConnection::~PostgresConnection() { disconnect(); }

PostgresConnection::PostgresConnection(PostgresConnection&& other) noexcept
    : impl_(std::move(other.impl_)), conninfo_(std::move(other.conninfo_)) {}

PostgresConnection& PostgresConnection::operator=(
    PostgresConnection&& other) noexcept {
    if (this != &other) {
        disconnect();
        impl_      = std::move(other.impl_);
        conninfo_  = std::move(other.conninfo_);
    }
    return *this;
}

util::Result<PostgresConnection> PostgresConnection::open(
    const PostgresUri& uri) {
#if defined(OPENADS_WITH_POSTGRESQL)
    PostgresConnection conn;
    conn.conninfo_ = uri.conninfo;
    conn.impl_     = std::make_unique<Impl>();

    PGconn* raw = PQconnectdb(uri.conninfo.c_str());
    if (raw == nullptr) {
        return util::Error{5001, 0, "PQconnectdb failed", ""};
    }
    if (PQstatus(raw) != CONNECTION_OK) {
        util::Error e = postgres_error("connect", PQerrorMessage(raw));
        PQfinish(raw);
        return e;
    }
    conn.impl_->conn = raw;
    return std::move(conn);
#else
    (void)uri;
    return util::Error{5004, 0,
                       "postgresql backend requires "
                       "OPENADS_WITH_POSTGRESQL=ON",
                       ""};
#endif
}

void PostgresConnection::disconnect() noexcept {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (impl_ && impl_->conn) {
        PQfinish(impl_->conn);
        impl_->conn = nullptr;
    }
#endif
    impl_.reset();
}

bool PostgresConnection::valid() const noexcept {
#if defined(OPENADS_WITH_POSTGRESQL)
    return impl_ && impl_->conn != nullptr &&
           PQstatus(impl_->conn) == CONNECTION_OK;
#else
    return false;
#endif
}

util::Result<std::unique_ptr<PostgresTable>>
PostgresConnection::open_table(const std::string& table_name) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid()) {
        return util::Error{5001, 0, "postgres connection not open", ""};
    }
    if (!is_safe_identifier(table_name)) {
        return util::Error{5001, 0, "invalid table name", table_name};
    }

    auto tbl = std::make_unique<PostgresTable>();
    tbl->conn = this;
    tbl->name = table_name;

    const std::string sql =
        "SELECT ctid::text FROM \"" + table_name + "\" ORDER BY ctid";
    PGresult* res = PQexec(impl_->conn, sql.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        const char* msg = PQerrorMessage(impl_->conn);
        PQclear(res);
        return postgres_error("ctid list", msg);
    }
    const int rows = PQntuples(res);
    tbl->ctids.reserve(static_cast<std::size_t>(rows));
    for (int r = 0; r < rows; ++r) {
        tbl->ctids.emplace_back(PQgetvalue(res, r, 0));
    }
    PQclear(res);

    tbl->cached_rec_count = static_cast<std::uint32_t>(tbl->ctids.size());
    tbl->rec_count_cached = true;
    tbl->positioned       = false;
    tbl->row_valid        = false;
    tbl->current_recno    = 0;
    tbl->current_deleted  = false;
    tbl->pos              = 0;

    if (auto d = describe_table_impl(impl_->conn, tbl.get()); !d) {
        return d.error();
    }
    return tbl;
#else
    (void)table_name;
    return util::Error{5004, 0,
                       "postgresql backend requires "
                       "OPENADS_WITH_POSTGRESQL=ON",
                       ""};
#endif
}

util::Result<void> PostgresConnection::goto_top(PostgresTable* tbl) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres goto_top", ""};
    }
    if (tbl->ctids.empty()) {
        tbl->positioned    = false;
        tbl->row_valid     = false;
        tbl->current_recno = 0;
        tbl->pos           = 0;
        return util::Result<void>{};
    }
    tbl->pos           = 0;
    tbl->positioned    = true;
    tbl->current_recno = 1;
    return load_current_row(impl_->conn, tbl);
#else
    (void)tbl;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<void> PostgresConnection::goto_bottom(PostgresTable* tbl) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres goto_bottom", ""};
    }
    if (tbl->ctids.empty()) {
        tbl->positioned    = false;
        tbl->row_valid     = false;
        tbl->current_recno = 0;
        tbl->pos           = 0;
        return util::Result<void>{};
    }
    tbl->pos           = tbl->ctids.size() - 1;
    tbl->positioned    = true;
    tbl->current_recno = static_cast<std::uint32_t>(tbl->pos + 1);
    return load_current_row(impl_->conn, tbl);
#else
    (void)tbl;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<void> PostgresConnection::skip(PostgresTable* tbl,
                                            std::int32_t step) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres skip", ""};
    }
    if (step == 0) return util::Result<void>{};
    if (tbl->ctids.empty()) {
        tbl->positioned = false;
        tbl->row_valid  = false;
        tbl->pos        = 0;
        return util::Result<void>{};
    }

    std::int64_t next = 0;
    if (!tbl->positioned) {
        if (tbl->pos == 0) {
            if (step > 0) next = step - 1;
            else return util::Error{5026, 0, "bof", ""};
        } else {
            if (step < 0) {
                next = static_cast<std::int64_t>(tbl->pos) + step;
            } else {
                return util::Result<void>{};
            }
        }
    } else {
        next = static_cast<std::int64_t>(tbl->pos) + step;
    }

    if (next < 0) {
        tbl->positioned = false;
        tbl->row_valid  = false;
        tbl->pos        = 0;
        return util::Error{5026, 0, "bof", ""};
    }
    if (static_cast<std::size_t>(next) >= tbl->ctids.size()) {
        tbl->positioned = false;
        tbl->row_valid  = false;
        tbl->pos        = tbl->ctids.size();
        return util::Result<void>{};
    }

    tbl->pos           = static_cast<std::size_t>(next);
    tbl->positioned    = true;
    tbl->current_recno = static_cast<std::uint32_t>(tbl->pos + 1);
    return load_current_row(impl_->conn, tbl);
#else
    (void)tbl;
    (void)step;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<bool> PostgresConnection::at_eof(PostgresTable* tbl) const {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres at_eof", ""};
    }
    if (tbl->ctids.empty()) return true;
    if (!tbl->positioned && tbl->pos >= tbl->ctids.size()) return true;
    return false;
#else
    (void)tbl;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<bool> PostgresConnection::at_bof(PostgresTable* tbl) const {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres at_bof", ""};
    }
    if (tbl->ctids.empty()) return true;
    return !tbl->positioned && tbl->pos == 0;
#else
    (void)tbl;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<std::uint32_t> PostgresConnection::record_count(PostgresTable* tbl) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres record_count", ""};
    }
    if (tbl->rec_count_cached) return tbl->cached_rec_count;
    tbl->cached_rec_count = static_cast<std::uint32_t>(tbl->ctids.size());
    tbl->rec_count_cached = true;
    return tbl->cached_rec_count;
#else
    (void)tbl;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<std::vector<PostgresTable::FieldDesc>>
PostgresConnection::describe_table(PostgresTable* tbl) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres describe_table", ""};
    }
    if (tbl->fields_cached) return tbl->fields;
    return describe_table_impl(impl_->conn, tbl);
#else
    (void)tbl;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<void> PostgresConnection::read_field(
    PostgresTable* tbl, const std::string& field_name,
    std::string& buf, bool& is_null) const {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres read_field", ""};
    }
    if (!tbl->fields_cached) {
        return util::Error{5001, 0, "schema not cached", ""};
    }
    if (!tbl->row_valid) {
        return util::Error{5026, 0, "no current record", ""};
    }

    const std::size_t idx = field_index_ci(*tbl, field_name);
    if (idx == static_cast<std::size_t>(-1)) {
        return util::Error{5063, 0, "column not found", field_name};
    }
    if (idx >= tbl->current_row.size()) {
        return util::Error{5001, 0, "row cache mismatch", ""};
    }
    is_null = tbl->current_nulls[idx];
    buf     = tbl->current_row[idx];
    return util::Result<void>{};
#else
    (void)tbl;
    (void)field_name;
    (void)buf;
    (void)is_null;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<bool> PostgresConnection::seek_index(
    PostgresTable* tbl, const std::string& column, const std::string& key,
    bool soft, bool last_key) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres seek", ""};
    }
    if (!is_safe_identifier(column)) {
        return util::Error{5001, 0, "invalid seek column", column};
    }
    if (!tbl->fields_cached) {
        if (auto d = describe_table_impl(impl_->conn, tbl); !d) return d.error();
    }
    if (field_index_ci(*tbl, column) == static_cast<std::size_t>(-1)) {
        return util::Error{5063, 0, "seek column not found", column};
    }

    std::string sql;
    if (last_key) {
        sql = soft
            ? "SELECT ctid::text FROM \"" + tbl->name + "\" WHERE \"" +
              column + "\" <= $1 ORDER BY \"" + column + "\" DESC LIMIT 1"
            : "SELECT ctid::text FROM \"" + tbl->name + "\" WHERE \"" +
              column + "\" = $1 ORDER BY \"" + column + "\" DESC LIMIT 1";
    } else {
        sql = soft
            ? "SELECT ctid::text FROM \"" + tbl->name + "\" WHERE \"" +
              column + "\" >= $1 ORDER BY \"" + column + "\" ASC LIMIT 1"
            : "SELECT ctid::text FROM \"" + tbl->name + "\" WHERE \"" +
              column + "\" = $1 LIMIT 1";
    }

    const char* params[1] = {key.c_str()};
    PGresult* res = PQexecParams(impl_->conn, sql.c_str(), 1, nullptr, params,
                                 nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        const char* msg = PQerrorMessage(impl_->conn);
        PQclear(res);
        return postgres_error("seek", msg);
    }

    bool found = false;
    if (PQntuples(res) == 1) {
        const std::string ctid = PQgetvalue(res, 0, 0);
        PQclear(res);
        if (auto p = position_at_ctid(impl_->conn, tbl, ctid); !p) {
            return p.error();
        }
        found = tbl->positioned && tbl->row_valid;
        tbl->last_seek_found = found;
    } else {
        PQclear(res);
        tbl->positioned      = false;
        tbl->row_valid       = false;
        found                = false;
        tbl->last_seek_found = false;
    }
    return found;
#else
    (void)tbl;
    (void)column;
    (void)key;
    (void)soft;
    (void)last_key;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

} // namespace openads::sql_backend