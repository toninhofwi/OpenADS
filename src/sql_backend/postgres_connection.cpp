#include "sql_backend/postgres_connection.h"

#include "sql_backend/postgres_backend.h"
#include "sql_backend/sql_common.h"

#include <algorithm>
#include <vector>

#if defined(OPENADS_WITH_POSTGRESQL)
#include <libpq-fe.h>
#endif

namespace openads::sql_backend {

namespace {

#if defined(OPENADS_WITH_POSTGRESQL)

std::string quote_ident(const std::string& name) {
    return '"' + name + '"';
}

std::string pk_select_list(const PostgresTable& tbl) {
    std::string out;
    for (std::size_t i = 0; i < tbl.pk_columns.size(); ++i) {
        if (i > 0) out += ", ";
        out += quote_ident(tbl.pk_columns[i]);
    }
    return out;
}

// "col1" = $1 AND "col2" = $2 ...  (placeholders bound positionally)
std::string pk_where_clause(const PostgresTable& tbl) {
    std::string sql;
    for (std::size_t i = 0; i < tbl.pk_columns.size(); ++i) {
        if (i > 0) sql += " AND ";
        sql += quote_ident(tbl.pk_columns[i]) + " = $" +
               std::to_string(i + 1);
    }
    return sql;
}

util::Result<void> load_current_row(PGconn* conn, PostgresTable* tbl);

util::Result<std::vector<std::string>>
discover_pk_columns(PGconn* conn, const std::string& table_name) {
    const char* params[1] = {table_name.c_str()};
    PGresult* res = PQexecParams(
        conn,
        "SELECT kcu.column_name "
        "FROM information_schema.table_constraints tc "
        "JOIN information_schema.key_column_usage kcu "
        "  ON kcu.constraint_schema = tc.constraint_schema "
        " AND kcu.constraint_name = tc.constraint_name "
        "WHERE tc.constraint_type = 'PRIMARY KEY' "
        "  AND tc.table_schema = ANY (current_schemas(true)) "
        "  AND tc.table_name = $1 "
        "ORDER BY kcu.ordinal_position",
        1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        const char* msg = PQerrorMessage(conn);
        PQclear(res);
        return postgres_error("pk discovery", msg);
    }
    const int rows = PQntuples(res);
    std::vector<std::string> cols;
    cols.reserve(static_cast<std::size_t>(rows));
    for (int r = 0; r < rows; ++r) {
        cols.emplace_back(PQgetvalue(res, r, 0));
    }
    PQclear(res);
    if (cols.empty()) {
        return util::Error{5001, 0, "table has no primary key", table_name};
    }
    return cols;
}

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

util::Result<void> position_at_pk(PGconn* conn, PostgresTable* tbl,
                                  const PostgresTable::PkRow& pk) {
    if (tbl == nullptr || conn == nullptr) {
        return util::Error{5001, 0, "invalid postgres table state", ""};
    }
    auto it = std::find_if(
        tbl->pk_snapshot.begin(), tbl->pk_snapshot.end(),
        [&](const PostgresTable::PkRow& row) {
            return row.values == pk.values;
        });
    if (it == tbl->pk_snapshot.end()) {
        tbl->positioned = false;
        tbl->row_valid  = false;
        return util::Result<void>{};
    }
    tbl->pos           = static_cast<std::size_t>(
        std::distance(tbl->pk_snapshot.begin(), it));
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
        "SELECT * FROM " + quote_ident(tbl->name) + " WHERE " +
        pk_where_clause(*tbl);
    const PostgresTable::PkRow& pk = tbl->pk_snapshot[tbl->pos];
    std::vector<const char*> params;
    params.reserve(pk.values.size());
    for (const std::string& v : pk.values) params.push_back(v.c_str());

    PGresult* res = PQexecParams(
        conn, sql.c_str(), static_cast<int>(params.size()), nullptr,
        params.data(), nullptr, nullptr, 0);
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

    auto pk = discover_pk_columns(impl_->conn, table_name);
    if (!pk) return pk.error();
    tbl->pk_columns = std::move(pk).value();

    const std::string sel = pk_select_list(*tbl);
    const std::string sql =
        "SELECT " + sel + " FROM " + quote_ident(table_name) +
        " ORDER BY " + sel;
    PGresult* res = PQexec(impl_->conn, sql.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        const char* msg = PQerrorMessage(impl_->conn);
        PQclear(res);
        return postgres_error("pk snapshot", msg);
    }
    const int rows    = PQntuples(res);
    const int pk_cols = PQnfields(res);
    tbl->pk_snapshot.reserve(static_cast<std::size_t>(rows));
    for (int r = 0; r < rows; ++r) {
        PostgresTable::PkRow pk_row;
        pk_row.values.resize(static_cast<std::size_t>(pk_cols));
        for (int c = 0; c < pk_cols; ++c) {
            pk_row.values[static_cast<std::size_t>(c)] =
                PQgetisnull(res, r, c) ? std::string{}
                                       : PQgetvalue(res, r, c);
        }
        tbl->pk_snapshot.push_back(std::move(pk_row));
    }
    PQclear(res);

    tbl->cached_rec_count = static_cast<std::uint32_t>(tbl->pk_snapshot.size());
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
    if (tbl->pk_snapshot.empty()) {
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
    if (tbl->pk_snapshot.empty()) {
        tbl->positioned    = false;
        tbl->row_valid     = false;
        tbl->current_recno = 0;
        tbl->pos           = 0;
        return util::Result<void>{};
    }
    tbl->pos           = tbl->pk_snapshot.size() - 1;
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
    if (tbl->pk_snapshot.empty()) {
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
    if (static_cast<std::size_t>(next) >= tbl->pk_snapshot.size()) {
        tbl->positioned = false;
        tbl->row_valid  = false;
        tbl->pos        = tbl->pk_snapshot.size();
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
    if (tbl->pk_snapshot.empty()) return true;
    if (!tbl->positioned && tbl->pos >= tbl->pk_snapshot.size()) return true;
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
    if (tbl->pk_snapshot.empty()) return true;
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
    tbl->cached_rec_count = static_cast<std::uint32_t>(tbl->pk_snapshot.size());
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

    const std::string sel  = pk_select_list(*tbl);
    const std::string qcol = quote_ident(column);
    const std::string from = " FROM " + quote_ident(tbl->name) + " WHERE ";

    std::string sql;
    if (last_key) {
        sql = soft
            ? "SELECT " + sel + from + qcol + " <= $1 ORDER BY " + qcol +
              " DESC LIMIT 1"
            : "SELECT " + sel + from + qcol + " = $1 ORDER BY " + qcol +
              " DESC LIMIT 1";
    } else {
        sql = soft
            ? "SELECT " + sel + from + qcol + " >= $1 ORDER BY " + qcol +
              " ASC LIMIT 1"
            : "SELECT " + sel + from + qcol + " = $1 LIMIT 1";
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
        const int pk_cols = PQnfields(res);
        PostgresTable::PkRow pk;
        pk.values.resize(static_cast<std::size_t>(pk_cols));
        for (int c = 0; c < pk_cols; ++c) {
            pk.values[static_cast<std::size_t>(c)] =
                PQgetisnull(res, 0, c) ? std::string{}
                                       : PQgetvalue(res, 0, c);
        }
        PQclear(res);
        if (auto p = position_at_pk(impl_->conn, tbl, pk); !p) {
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
