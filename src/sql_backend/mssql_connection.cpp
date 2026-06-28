#include "sql_backend/mssql_connection.h"

#if defined(OPENADS_WITH_MSSQL)

#include "sql_backend/backend_aggregate.h"
#include "openads/error.h"
#include "sql_backend/mssql_uri.h"
#include "sql_backend/sql_acl_store.h"
#include "sql_backend/sql_common.h"
#include "sql_backend/tds_protocol.h"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace openads::sql_backend {

struct MssqlConnection::Impl {
    TdsTlsChannel channel;
    bool          authenticated = false;
};

MssqlConnection::MssqlConnection() = default;
MssqlConnection::~MssqlConnection() = default;
MssqlConnection::MssqlConnection(MssqlConnection&&) noexcept = default;
MssqlConnection& MssqlConnection::operator=(MssqlConnection&&) noexcept = default;

bool MssqlConnection::valid() const noexcept {
    return impl_ && impl_->authenticated && impl_->channel.valid();
}

void MssqlConnection::disconnect() noexcept {
    if (impl_) {
        impl_->channel.close();
        impl_->authenticated = false;
    }
}

util::Result<MssqlConnection> MssqlConnection::open(const MssqlUri& uri) {
    // 1. Establish the TLS-in-TDS channel (TCP + PRELOGIN + tunnelled TLS).
    auto chan = TdsTlsChannel::connect(uri);
    if (!chan) return chan.error();

    MssqlConnection conn;
    conn.impl_ = std::make_unique<Impl>();
    conn.impl_->channel = std::move(chan).value();

    // 2. Build + send the LOGIN7 message over the encrypted session.
    tds::Login7Params params;
    params.hostname    = "OpenADS";
    params.username    = uri.user;
    params.password    = uri.password;
    params.app_name    = "OpenADS";
    params.server_name = uri.host;
    params.database    = uri.database;

    std::vector<std::uint8_t> login7 = tds::build_login7(params);
    // build_login7 already prepends the 8-byte TDS header; the channel's
    // send_tds adds its own header, so strip the embedded one and let the
    // channel frame (and segment) the LOGIN7 structure itself.
    std::vector<std::uint8_t> login7_payload(login7.begin() + 8, login7.end());

    if (auto r = conn.impl_->channel.send_tds(tds::TDS_PKT_LOGIN7,
                                              login7_payload); !r) {
        return r.error();
    }

    // 3. Read the login response token stream and parse it.
    auto reply = conn.impl_->channel.recv_tds();
    if (!reply) return reply.error();
    const std::vector<std::uint8_t>& payload = reply.value().second;

    tds::LoginResult res = tds::parse_login_response(payload.data(),
                                                     payload.size());
    if (!res.authenticated) {
        // Surface the SERVER's error (number + message) — never the password
        // or connection string.  Map to a login-failed family code so the ABI
        // returns a non-zero, recognisable error.
        std::int32_t code = static_cast<std::int32_t>(res.error_number);
        if (code == 0) code = openads::AE_LOGIN_FAILED;
        std::string msg = res.message.empty()
                              ? std::string("MSSQL login failed")
                              : res.message;
        return util::Error{code, 0, msg, ""};
    }

    conn.impl_->authenticated = true;
    (void)conn.exec_sql(acl_table_ddl(SqlDdlDialect::Mssql));
    return conn;
}

util::Result<tds::QueryResult> MssqlConnection::query(const std::string& sql) {
    if (!impl_ || !impl_->channel.valid() || !impl_->authenticated) {
        return util::Error{openads::AE_NO_CONNECTION, 0,
            "MSSQL not connected", ""};
    }
    // Send SQL batch packet.
    if (auto r = impl_->channel.send_tds(tds::TDS_PKT_SQLBATCH,
                                         tds::build_sql_batch(sql)); !r) {
        return r.error();
    }
    // Receive the server reply (may span multiple TDS packets, reassembled
    // by recv_tds with the 64 MiB cap).
    auto reply = impl_->channel.recv_tds();
    if (!reply) return reply.error();
    const auto& payload = reply.value().second;

    tds::QueryResult qr = tds::parse_query_response(payload.data(),
                                                    payload.size());
    if (!qr.ok) {
        if (!qr.unsupported_type.empty()) {
            // COLMETADATA contained a TDS type token we cannot decode.
            return util::Error{openads::AE_TYPE_MISMATCH, 0,
                "unsupported MSSQL column type: " + qr.unsupported_type, ""};
        }
        // Server ERROR token: surface the server error number if non-zero;
        // fall back to AE_PARSE_ERROR so callers get a distinct, non-zero code.
        // NEVER embed the sql string in the error (could contain sensitive data).
        std::int32_t code = qr.error_number
                              ? static_cast<std::int32_t>(qr.error_number)
                              : static_cast<std::int32_t>(openads::AE_PARSE_ERROR);
        std::string msg = qr.message.empty()
                              ? std::string("MSSQL query failed")
                              : qr.message;
        return util::Error{code, 0, msg, ""};
    }
    return qr;
}

namespace {

bool ci_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string quote_ident(const std::string& name) {
    std::string out = "[";
    for (char c : name) {
        if (c == ']') out += ']';
        out += c;
    }
    out += ']';
    return out;
}

std::string quote_lit(const std::string& v) {
    std::string out = "N'";
    for (char c : v) {
        if (c == '\'') out += '\'';
        out += c;
    }
    out += '\'';
    return out;
}

std::string quote_table_lit(const std::string& v) {
    std::string out = "'";
    for (char c : v) {
        if (c == '\'') out += '\'';
        out += c;
    }
    out += '\'';
    return out;
}

std::size_t field_index_ci(const MssqlTable& tbl, const std::string& name) {
    for (std::size_t i = 0; i < tbl.field_count(); ++i) {
        if (ci_equal(tbl.field_name(i), name)) return i;
    }
    return static_cast<std::size_t>(-1);
}

std::vector<std::string> current_pk_values(const MssqlTable& tbl) {
    std::vector<std::string> vals;
    vals.reserve(tbl.pk_cols.size());
    if (tbl.bof || tbl.eof || tbl.pos >= tbl.data.rows.size()) return vals;
    const auto& row = tbl.data.rows[tbl.pos];
    for (std::size_t pk : tbl.pk_cols) {
        if (pk >= row.size() || row[pk].is_null) {
            vals.push_back(std::string{});
        } else {
            vals.push_back(row[pk].value);
        }
    }
    return vals;
}

std::string pk_where_clause(const MssqlTable& tbl,
                            const std::vector<std::string>& pk_vals) {
    std::string w;
    for (std::size_t i = 0; i < tbl.pk_cols.size(); ++i) {
        if (i) w += " AND ";
        w += quote_ident(tbl.field_name(tbl.pk_cols[i])) + " = " +
             quote_lit(pk_vals[i]);
    }
    return w;
}

void reset_cursor_bof(MssqlTable* tbl) {
    tbl->pos = 0;
    tbl->bof = true;
    tbl->eof = tbl->data.rows.empty();
}

void position_last_row(MssqlTable* tbl) {
    if (tbl->data.rows.empty()) {
        reset_cursor_bof(tbl);
        return;
    }
    tbl->pos = tbl->data.rows.size() - 1;
    tbl->bof = false;
    tbl->eof = false;
}

bool position_by_pk(MssqlTable* tbl, const std::vector<std::string>& pk_vals) {
    for (std::size_t r = 0; r < tbl->data.rows.size(); ++r) {
        bool match = true;
        for (std::size_t i = 0; i < tbl->pk_cols.size(); ++i) {
            const auto& cell = tbl->data.rows[r][tbl->pk_cols[i]];
            const std::string v = cell.is_null ? std::string{} : cell.value;
            if (v != pk_vals[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            tbl->pos = r;
            tbl->bof = false;
            tbl->eof = false;
            return true;
        }
    }
    return false;
}

void copy_current_to_staging(MssqlTable* tbl) {
    const std::size_t n = tbl->field_count();
    tbl->staging_row.assign(n, std::string{});
    tbl->staging_nulls.assign(n, true);
    for (std::size_t i = 0; i < n; ++i) {
        std::string v;
        bool is_null = false;
        if (tbl->get_field(i, v, is_null)) {
            tbl->staging_row[i]   = v;
            tbl->staging_nulls[i] = is_null;
        }
    }
}

util::Result<void> refetch(MssqlConnection& c, MssqlTable* tbl) {
    if (tbl->sql_table.empty()) {
        return util::Error{openads::AE_INTERNAL_ERROR, 0,
                           "missing table name", ""};
    }
    std::string sql = "SELECT * FROM " + quote_ident(tbl->sql_table);
    if (!tbl->where_filter.empty()) {
        sql += " WHERE (" + tbl->where_filter + ")";
    }
    auto qr = c.query(sql);
    if (!qr) return qr.error();
    tds::QueryResult result = std::move(qr).value();
    if (!result.ok) {
        return util::Error{
            static_cast<std::int32_t>(result.error_number), 0,
            result.message.empty() ? std::string("MSSQL refetch failed")
                                   : result.message,
            ""};
    }
    tbl->data = std::move(result);
    return util::Result<void>{};
}

} // namespace

util::Result<std::vector<std::string>>
MssqlConnection::discover_pk(const std::string& table_name) {
    if (!valid()) {
        return util::Error{openads::AE_NO_CONNECTION, 0,
                           "MSSQL not connected", ""};
    }
    if (!is_safe_identifier(table_name)) {
        return util::Error{openads::AE_INTERNAL_ERROR, 0,
                           "unsafe table name", table_name};
    }
    const std::string sql =
        "SELECT COLUMN_NAME "
        "FROM INFORMATION_SCHEMA.KEY_COLUMN_USAGE "
        "WHERE CONSTRAINT_NAME = ("
        "  SELECT TOP 1 CONSTRAINT_NAME "
        "  FROM INFORMATION_SCHEMA.TABLE_CONSTRAINTS "
        "  WHERE TABLE_NAME = " + quote_table_lit(table_name) +
        "    AND CONSTRAINT_TYPE = 'PRIMARY KEY'"
        ") "
        "ORDER BY ORDINAL_POSITION";
    auto qr = query(sql);
    if (!qr) return qr.error();
    tds::QueryResult result = std::move(qr).value();
    if (!result.ok) {
        return util::Error{
            static_cast<std::int32_t>(result.error_number), 0,
            result.message.empty() ? std::string("MSSQL discover_pk failed")
                                   : result.message,
            ""};
    }
    std::vector<std::string> cols;
    cols.reserve(result.rows.size());
    for (const auto& row : result.rows) {
        if (!row.empty() && !row[0].is_null) cols.push_back(row[0].value);
    }
    return cols;
}

util::Result<void> MssqlConnection::append_blank(MssqlTable* tbl) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mssql append", ""};
    }
    const std::size_t n = tbl->field_count();
    tbl->staging_row.assign(n, std::string{});
    tbl->staging_nulls.assign(n, true);
    tbl->pending_append = true;
    tbl->row_dirty      = true;
    return util::Result<void>{};
}

util::Result<void> MssqlConnection::set_field(
    MssqlTable* tbl, const std::string& field_name, const std::string& value) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mssql set_field", ""};
    }
    if (!tbl->pending_append &&
        (tbl->bof || tbl->eof || tbl->pos >= tbl->data.rows.size())) {
        return util::Error{5026, 0, "no current record", ""};
    }
    const std::size_t idx = field_index_ci(*tbl, field_name);
    if (idx == static_cast<std::size_t>(-1)) {
        return util::Error{5063, 0, "column not found", field_name};
    }
    if (!tbl->row_dirty && !tbl->pending_append) {
        copy_current_to_staging(tbl);
    }
    if (tbl->staging_row.size() < tbl->field_count()) {
        tbl->staging_row.resize(tbl->field_count());
        tbl->staging_nulls.resize(tbl->field_count(), true);
    }
    tbl->staging_row[idx]   = value;
    tbl->staging_nulls[idx] = false;
    tbl->row_dirty          = true;
    return util::Result<void>{};
}

util::Result<void> MssqlConnection::flush_record(MssqlTable* tbl) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mssql flush", ""};
    }
    if (!tbl->row_dirty && !tbl->pending_append) return util::Result<void>{};
    if (tbl->sql_table.empty()) {
        return util::Error{5001, 0, "missing table name", ""};
    }

    const std::string qtbl = quote_ident(tbl->sql_table);

    if (tbl->pending_append) {
        std::string cols, vals;
        bool any = false;
        for (std::size_t i = 0; i < tbl->field_count(); ++i) {
            if (i >= tbl->staging_nulls.size() || tbl->staging_nulls[i]) {
                continue;
            }
            if (any) {
                cols += ", ";
                vals += ", ";
            }
            cols += quote_ident(tbl->field_name(i));
            vals += quote_lit(tbl->staging_row[i]);
            any = true;
        }
        if (!any) {
            return util::Error{5001, 0, "insert has no columns", tbl->sql_table};
        }
        const std::string sql = "INSERT INTO " + qtbl +
                                " (" + cols + ") VALUES (" + vals + ")";
        auto ins = query(sql);
        if (!ins) return ins.error();
        const tds::QueryResult& qr = ins.value();
        if (!qr.ok) {
            return util::Error{
                static_cast<std::int32_t>(qr.error_number), 0,
                qr.message.empty() ? std::string("MSSQL insert failed")
                                   : qr.message,
                ""};
        }
        tbl->pending_append = false;
        tbl->row_dirty      = false;
        if (auto rf = refetch(*this, tbl); !rf) return rf.error();
        position_last_row(tbl);
        return util::Result<void>{};
    }

    if (tbl->pk_cols.empty()) {
        return util::Error{5001, 0,
                           "table has no primary key; UPDATE not supported",
                           tbl->sql_table};
    }
    const std::vector<std::string> pk_vals = current_pk_values(*tbl);
    if (pk_vals.size() != tbl->pk_cols.size()) {
        return util::Error{5026, 0, "no current record", ""};
    }

    std::vector<bool> is_pk(tbl->field_count(), false);
    for (std::size_t pk : tbl->pk_cols) {
        if (pk < is_pk.size()) is_pk[pk] = true;
    }

    std::string set_clause;
    bool any = false;
    for (std::size_t i = 0; i < tbl->field_count(); ++i) {
        if (is_pk[i]) continue;
        if (i >= tbl->staging_row.size()) continue;
        if (any) set_clause += ", ";
        if (i < tbl->staging_nulls.size() && tbl->staging_nulls[i]) {
            set_clause += quote_ident(tbl->field_name(i)) + " = NULL";
        } else {
            set_clause += quote_ident(tbl->field_name(i)) + " = " +
                          quote_lit(tbl->staging_row[i]);
        }
        any = true;
    }
    if (!any) {
        tbl->row_dirty = false;
        return util::Result<void>{};
    }

    const std::string sql = "UPDATE " + qtbl + " SET " + set_clause +
                            " WHERE " + pk_where_clause(*tbl, pk_vals);
    auto upd = query(sql);
    if (!upd) return upd.error();
    const tds::QueryResult& qr = upd.value();
    if (!qr.ok) {
        return util::Error{
            static_cast<std::int32_t>(qr.error_number), 0,
            qr.message.empty() ? std::string("MSSQL update failed") : qr.message,
            ""};
    }
    tbl->row_dirty = false;
    if (auto rf = refetch(*this, tbl); !rf) return rf.error();
    if (!position_by_pk(tbl, pk_vals)) reset_cursor_bof(tbl);
    return util::Result<void>{};
}

util::Result<void> MssqlConnection::delete_record(MssqlTable* tbl) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mssql delete", ""};
    }
    if (tbl->pending_append) {
        return util::Error{5026, 0, "no current record", ""};
    }
    if (tbl->pk_cols.empty()) {
        return util::Error{5001, 0,
                           "table has no primary key; DELETE not supported",
                           tbl->sql_table};
    }
    if (tbl->bof || tbl->eof || tbl->pos >= tbl->data.rows.size()) {
        return util::Error{5026, 0, "no current record", ""};
    }
    const std::vector<std::string> pk_vals = current_pk_values(*tbl);
    if (pk_vals.size() != tbl->pk_cols.size()) {
        return util::Error{5026, 0, "no current record", ""};
    }

    const std::string sql = "DELETE FROM " + quote_ident(tbl->sql_table) +
                            " WHERE " + pk_where_clause(*tbl, pk_vals);
    auto del = query(sql);
    if (!del) return del.error();
    const tds::QueryResult& qr = del.value();
    if (!qr.ok) {
        return util::Error{
            static_cast<std::int32_t>(qr.error_number), 0,
            qr.message.empty() ? std::string("MSSQL delete failed") : qr.message,
            ""};
    }
    tbl->row_dirty      = false;
    tbl->pending_append = false;
    if (auto rf = refetch(*this, tbl); !rf) return rf.error();
    reset_cursor_bof(tbl);
    return util::Result<void>{};
}

util::Result<void> MssqlConnection::exec_sql(const std::string& sql) {
    if (!valid()) {
        return util::Error{openads::AE_NO_CONNECTION, 0, "MSSQL not connected", ""};
    }
    auto qr = query(sql);
    if (!qr) return qr.error();
    if (!qr.value().ok) {
        return util::Error{
            static_cast<std::int32_t>(qr.value().error_number), 0,
            qr.value().message.empty() ? std::string("MSSQL exec failed")
                                       : qr.value().message,
            ""};
    }
    return util::Result<void>{};
}

util::Result<void>
MssqlConnection::set_filter(MssqlTable* tbl, const std::string& where) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mssql set_filter", ""};
    }
    tbl->where_builder.aof_filter = where;
    tbl->where_filter = tbl->where_builder.build();
    if (auto rf = refetch(*this, tbl); !rf) return rf.error();
    reset_cursor_bof(tbl);
    return util::Result<void>{};
}

util::Result<void> MssqlConnection::refresh_where_filter(MssqlTable* tbl) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mssql refresh_where_filter", ""};
    }
    tbl->where_filter = tbl->where_builder.build();
    if (auto rf = refetch(*this, tbl); !rf) return rf.error();
    reset_cursor_bof(tbl);
    return util::Result<void>{};
}

util::Result<std::vector<engine::AggValue>>
MssqlConnection::aggregate(MssqlTable* tbl,
                             const std::string& where_sql,
                             const std::vector<engine::AggSpec>& specs) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mssql aggregate", ""};
    }
    std::vector<AggregateFieldDesc> fields;
    fields.reserve(tbl->field_count());
    for (std::size_t i = 0; i < tbl->field_count(); ++i) {
        fields.push_back({tbl->field_name(i), tbl->field_type(i)});
    }
    std::string bad;
    const std::string sql = build_aggregate_sql(
        quote_ident(tbl->sql_table), where_sql, specs, fields,
        quote_ident, &bad);
    if (sql.empty()) {
        return util::Error{5001, 0, "invalid mssql aggregate field: " + bad, ""};
    }
    auto qr = query(sql);
    if (!qr) return qr.error();
    const tds::QueryResult& result = qr.value();
    if (!result.ok) {
        return util::Error{
            static_cast<std::int32_t>(result.error_number), 0,
            result.message.empty() ? std::string("MSSQL aggregate failed")
                                   : result.message,
            ""};
    }
    std::vector<std::string> vals;
    vals.resize(specs.size());
    if (!result.rows.empty()) {
        const auto& row = result.rows[0];
        for (std::size_t i = 0; i < specs.size(); ++i) {
            if (i < row.size() && !row[i].is_null) vals[i] = row[i].value;
            else vals[i].clear();
        }
    }
    return parse_aggregate_row(specs, fields, vals, !result.rows.empty());
}

namespace {

std::size_t column_index_ci(const MssqlTable& tbl, const std::string& name) {
    for (std::size_t i = 0; i < tbl.field_count(); ++i) {
        if (ci_equal(tbl.field_name(i), name)) return i;
    }
    return static_cast<std::size_t>(-1);
}

int compare_keys(const std::string& a, const std::string& b, bool soft) {
    if (soft) return a.compare(b);
    return (a < b) ? -1 : (a > b) ? 1 : 0;
}

std::string mssql_record_lock_key(const MssqlTable& tbl, std::size_t pos) {
    std::string k = "R\x1f" + tbl.sql_table;
    if (!tbl.bof && !tbl.eof && pos < tbl.data.rows.size()) {
        const auto& row = tbl.data.rows[pos];
        for (std::size_t pk : tbl.pk_cols) {
            if (pk < row.size() && !row[pk].is_null) k += "\x1f" + row[pk].value;
        }
    }
    return k;
}

std::string mssql_table_lock_key(const MssqlTable& tbl) {
    return "T\x1f" + tbl.sql_table;
}

util::Result<int> mssql_applock(MssqlConnection& c, const std::string& key,
                                bool acquire) {
    std::string esc;
    for (char ch : key) {
        if (ch == '\'') esc += '\'';
        esc += ch;
    }
    const std::string sql =
        acquire
            ? "DECLARE @r int; EXEC @r = sp_getapplock @Resource=N'" + esc +
              "', @LockMode='Exclusive', @LockOwner='Session', @LockTimeout=0; "
              "SELECT @r;"
            : "DECLARE @r int; EXEC @r = sp_releaseapplock @Resource=N'" + esc +
              "', @LockOwner='Session'; SELECT @r;";
    auto qr = c.query(sql);
    if (!qr) return qr.error();
    const tds::QueryResult& result = qr.value();
    if (!result.ok || result.rows.empty() || result.rows[0].empty()) {
        return util::Error{5001, 0, "applock returned no value", ""};
    }
    return std::atoi(result.rows[0][0].value.c_str());
}

}  // namespace

util::Result<bool> MssqlConnection::seek_index(MssqlTable* tbl,
                                               const std::string& column,
                                               const std::string& key,
                                               bool soft,
                                               bool last_key) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mssql seek", ""};
    }
    if (!is_safe_identifier(column)) {
        return util::Error{5001, 0, "invalid seek column", column};
    }
    const std::size_t ci = column_index_ci(*tbl, column);
    if (ci == static_cast<std::size_t>(-1)) {
        return util::Error{5063, 0, "seek column not found", column};
    }
    if (tbl->data.rows.empty()) {
        tbl->last_seek_found = false;
        return false;
    }
    std::size_t lo = 0;
    std::size_t hi = tbl->data.rows.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        const auto& cell = tbl->data.rows[mid][ci];
        const std::string val = cell.is_null ? std::string{} : cell.value;
        const int cmp = compare_keys(val, key, soft);
        if (cmp < 0) lo = mid + 1;
        else hi = mid;
    }
    std::size_t hit = lo;
    if (last_key && hit > 0) {
        const auto& cell = tbl->data.rows[hit - 1][ci];
        const std::string val = cell.is_null ? std::string{} : cell.value;
        if (compare_keys(val, key, soft) <= 0) hit = hit - 1;
    }
    if (hit >= tbl->data.rows.size()) {
        tbl->last_seek_found = false;
        return false;
    }
    const auto& cell = tbl->data.rows[hit][ci];
    const std::string val = cell.is_null ? std::string{} : cell.value;
    const int cmp = compare_keys(val, key, soft);
    const bool found = soft ? (cmp <= 0) : (cmp == 0);
    if (found) {
        tbl->pos = hit;
        tbl->bof = false;
        tbl->eof = false;
        tbl->last_seek_found = true;
        return true;
    }
    tbl->last_seek_found = false;
    return false;
}

util::Result<void> MssqlConnection::lock_record(MssqlTable* tbl,
                                                std::uint32_t recno) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mssql lock", ""};
    }
    const std::size_t pos =
        (recno == 0)
            ? (tbl->bof || tbl->eof ? static_cast<std::size_t>(-1) : tbl->pos)
            : static_cast<std::size_t>(recno - 1);
    if (pos >= tbl->data.rows.size()) {
        return util::Error{5026, 0, "no current record", ""};
    }
    auto r = mssql_applock(*this, mssql_record_lock_key(*tbl, pos), true);
    if (!r) return r.error();
    if (r.value() < 0) return util::Error{5035, 0, "record locked", ""};
    return util::Result<void>{};
}

util::Result<void> MssqlConnection::unlock_record(MssqlTable* tbl,
                                                  std::uint32_t recno) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mssql unlock", ""};
    }
    const std::size_t pos =
        (recno == 0)
            ? (tbl->bof || tbl->eof ? static_cast<std::size_t>(-1) : tbl->pos)
            : static_cast<std::size_t>(recno - 1);
    if (pos >= tbl->data.rows.size()) return util::Result<void>{};
    auto r = mssql_applock(*this, mssql_record_lock_key(*tbl, pos), false);
    if (!r) return r.error();
    return util::Result<void>{};
}

util::Result<void> MssqlConnection::lock_table(MssqlTable* tbl) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mssql lock", ""};
    }
    auto r = mssql_applock(*this, mssql_table_lock_key(*tbl), true);
    if (!r) return r.error();
    if (r.value() < 0) return util::Error{5035, 0, "table locked", ""};
    return util::Result<void>{};
}

util::Result<void> MssqlConnection::unlock_table(MssqlTable* tbl) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mssql unlock", ""};
    }
    auto r = mssql_applock(*this, mssql_table_lock_key(*tbl), false);
    if (!r) return r.error();
    return util::Result<void>{};
}

} // namespace openads::sql_backend

#endif // defined(OPENADS_WITH_MSSQL)
