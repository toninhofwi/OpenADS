#include "sql_backend/odbc_connection.h"

#include "sql_backend/odbc_backend.h"
#include "sql_backend/sql_common.h"

#include "openads/ace.h"

#if !defined(OPENADS_WITH_ODBC)

// This translation unit is only compiled when the ODBC backend is
// enabled (see src/CMakeLists.txt, target_sources under OPENADS_WITH_ODBC).
// Guard defensively so a stray build without the flag is an empty unit
// rather than a pile of undefined-symbol errors.
namespace openads::sql_backend {}

#else

#if defined(_WIN32)
#  include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

#include <algorithm>
#include <cstdlib>

namespace openads::sql_backend {

namespace {

inline SQLCHAR* sqlstr(const std::string& s) {
    return const_cast<SQLCHAR*>(
        reinterpret_cast<const SQLCHAR*>(s.c_str()));
}

std::string odbc_diag(SQLSMALLINT handle_type, SQLHANDLE handle) {
    std::string out;
    SQLCHAR     state[6];
    SQLINTEGER  native = 0;
    SQLCHAR     msg[1024];
    SQLSMALLINT msg_len = 0;
    for (SQLSMALLINT i = 1; i <= 8; ++i) {
        SQLRETURN r = SQLGetDiagRec(handle_type, handle, i, state, &native,
                                    msg, static_cast<SQLSMALLINT>(sizeof(msg)),
                                    &msg_len);
        if (r == SQL_NO_DATA || !SQL_SUCCEEDED(r)) break;
        if (!out.empty()) out += "; ";
        out += reinterpret_cast<const char*>(state);
        out += ": ";
        out += reinterpret_cast<const char*>(msg);
    }
    return out;
}

// Read every row of an already-executed statement, each cell coerced to
// a UTF-8/ANSI string via SQL_C_CHAR (long values stitched across
// SQLGetData calls). NULL cells are flagged.
util::Result<void> read_all_rows(
    SQLHSTMT st,
    std::vector<std::vector<std::string>>& rows,
    std::vector<std::vector<bool>>& nulls) {
    SQLSMALLINT cols = 0;
    if (!SQL_SUCCEEDED(SQLNumResultCols(st, &cols))) {
        return odbc_error("odbc num cols", odbc_diag(SQL_HANDLE_STMT, st));
    }
    while (true) {
        SQLRETURN r = SQLFetch(st);
        if (r == SQL_NO_DATA) break;
        if (!SQL_SUCCEEDED(r)) {
            return odbc_error("odbc fetch", odbc_diag(SQL_HANDLE_STMT, st));
        }
        std::vector<std::string> row;
        std::vector<bool>        rn;
        row.reserve(cols);
        rn.reserve(cols);
        for (SQLSMALLINT c = 1; c <= cols; ++c) {
            std::string val;
            bool        is_null = false;
            char        buf[4096];
            SQLLEN      ind = 0;
            while (true) {
                SQLRETURN gr = SQLGetData(st, static_cast<SQLUSMALLINT>(c),
                                          SQL_C_CHAR, buf,
                                          static_cast<SQLLEN>(sizeof(buf)),
                                          &ind);
                if (gr == SQL_NO_DATA) break;
                if (!SQL_SUCCEEDED(gr)) {
                    return odbc_error("odbc getdata",
                                      odbc_diag(SQL_HANDLE_STMT, st));
                }
                if (ind == SQL_NULL_DATA) {
                    is_null = true;
                    break;
                }
                std::size_t chunk;
                if (ind == SQL_NO_TOTAL ||
                    ind >= static_cast<SQLLEN>(sizeof(buf))) {
                    chunk = sizeof(buf) - 1;  // truncated; NUL eats one byte
                } else if (ind < 0) {
                    chunk = 0;                // stray driver indicator; ignore
                } else {
                    chunk = static_cast<std::size_t>(ind);
                }
                val.append(buf, chunk);
                if (gr == SQL_SUCCESS) break;  // all data consumed
                // SQL_SUCCESS_WITH_INFO: more remains, keep reading
            }
            row.push_back(std::move(val));
            rn.push_back(is_null);
        }
        rows.push_back(std::move(row));
        nulls.push_back(std::move(rn));
    }
    return util::Result<void>{};
}

util::Result<void> run_query(
    SQLHDBC dbc, const std::string& sql,
    std::vector<std::vector<std::string>>& rows,
    std::vector<std::vector<bool>>& nulls,
    SQLULEN max_rows = 0) {
    SQLHSTMT st = SQL_NULL_HSTMT;
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) {
        return odbc_error("odbc alloc stmt", odbc_diag(SQL_HANDLE_DBC, dbc));
    }
    if (max_rows > 0) {
        SQLSetStmtAttr(st, SQL_ATTR_MAX_ROWS,
                       reinterpret_cast<SQLPOINTER>(max_rows), 0);
    }
    SQLRETURN r = SQLExecDirect(st, sqlstr(sql), SQL_NTS);
    if (!SQL_SUCCEEDED(r) && r != SQL_NO_DATA) {
        auto e = odbc_error("odbc exec", odbc_diag(SQL_HANDLE_STMT, st));
        SQLFreeHandle(SQL_HANDLE_STMT, st);
        return e;
    }
    auto rr = read_all_rows(st, rows, nulls);
    SQLFreeHandle(SQL_HANDLE_STMT, st);
    return rr;
}

std::string quote_ident(const std::string& q, const std::string& name) {
    if (q.empty()) return name;
    return q + name + q;
}

std::string escape_literal(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "''";
        else           out += c;
    }
    out += '\'';
    return out;
}

// Injection-safe numeric-literal check: only [0-9 . + - e E], at least
// one digit. The xBase key/PK values arrive as strings; numeric columns
// must NOT be quoted (Jet/Access and others reject 'text' = int_column),
// so a validated number is emitted bare.
bool is_numeric_literal(const std::string& s) {
    if (s.empty()) return false;
    bool any_digit = false;
    for (char c : s) {
        if (c >= '0' && c <= '9') { any_digit = true; continue; }
        if (c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E') continue;
        return false;
    }
    return any_digit;
}

// Emit a SQL literal for `value` against `column`: bare for numeric
// columns (when the value is a clean number), single-quoted otherwise.
std::string format_literal(const OdbcTable& tbl, const std::string& column,
                           const std::string& value) {
    const std::size_t idx = field_index_ci(tbl, column);
    const bool numeric = idx != static_cast<std::size_t>(-1) &&
        (tbl.fields[idx].type == ADS_INTEGER ||
         tbl.fields[idx].type == ADS_DOUBLE);
    if (numeric && is_numeric_literal(value)) return value;
    return escape_literal(value);
}

std::string index_column_sql(const std::string& q, const std::string& column,
                             IndexExprKind kind) {
    const std::string qcol = quote_ident(q, column);
    return kind == IndexExprKind::UpperColumn ? "UPPER(" + qcol + ")" : qcol;
}

std::string pk_select_list(const std::string& q, const OdbcTable& tbl) {
    std::string out;
    for (std::size_t i = 0; i < tbl.pk_columns.size(); ++i) {
        if (i > 0) out += ", ";
        out += quote_ident(q, tbl.pk_columns[i]);
    }
    return out;
}

std::string pk_where_clause(const std::string& q, const OdbcTable& tbl,
                            const OdbcTable::PkRow& pk) {
    std::string out;
    for (std::size_t i = 0; i < tbl.pk_columns.size(); ++i) {
        if (i > 0) out += " AND ";
        out += quote_ident(q, tbl.pk_columns[i]) + " = " +
               format_literal(tbl, tbl.pk_columns[i], pk.values[i]);
    }
    return out;
}

std::vector<std::string> order_keyed(std::vector<std::pair<int, std::string>> k) {
    std::stable_sort(k.begin(), k.end(),
                     [](const auto& a, const auto& b) {
                         return a.first < b.first;
                     });
    std::vector<std::string> out;
    out.reserve(k.size());
    for (auto& kv : k) out.push_back(kv.second);
    return out;
}

// Try SQLPrimaryKeys, falling back to the first UNIQUE index reported by
// SQLStatistics. Many drivers (the Access/Jet ODBC driver among them) do
// not implement SQLPrimaryKeys but do report a unique index, which is an
// equally valid stable row key for snapshot navigation.
util::Result<std::vector<std::string>>
discover_pk(SQLHDBC dbc, const std::string& name) {
    // 1) SQLPrimaryKeys — columns (1-based): 4 COLUMN_NAME, 5 KEY_SEQ.
    {
        SQLHSTMT st = SQL_NULL_HSTMT;
        if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) {
            return odbc_error("odbc alloc stmt", odbc_diag(SQL_HANDLE_DBC, dbc));
        }
        SQLRETURN r = SQLPrimaryKeys(st, nullptr, 0, nullptr, 0,
                                     sqlstr(name), SQL_NTS);
        if (SQL_SUCCEEDED(r)) {
            std::vector<std::vector<std::string>> rows;
            std::vector<std::vector<bool>>        nulls;
            auto rr = read_all_rows(st, rows, nulls);
            SQLFreeHandle(SQL_HANDLE_STMT, st);
            if (!rr) return rr.error();
            // Pin to the first schema seen (col 2 = TABLE_SCHEM): with a
            // null schema arg, drivers that support schemas can return PK
            // columns for same-named tables across schemas; mixing them
            // would corrupt the key.
            std::string chosen_schema;
            bool        has_schema = false;
            std::vector<std::pair<int, std::string>> keyed;
            for (auto& row : rows) {
                std::string schem = row.size() >= 2 ? row[1] : std::string();
                std::string col   = row.size() >= 4 ? row[3] : std::string();
                int seq = row.size() >= 5 ? std::atoi(row[4].c_str()) : 0;
                if (!has_schema) { chosen_schema = schem; has_schema = true; }
                if (schem != chosen_schema) continue;
                if (!col.empty()) keyed.emplace_back(seq, col);
            }
            if (!keyed.empty()) return order_keyed(std::move(keyed));
        } else {
            // Unsupported (IM001) or failed — fall through to statistics.
            SQLFreeHandle(SQL_HANDLE_STMT, st);
        }
    }

    // 2) SQLStatistics unique index — columns (1-based): 4 NON_UNIQUE
    // (0 = unique), 6 INDEX_NAME, 8 ORDINAL_POSITION, 9 COLUMN_NAME.
    {
        SQLHSTMT st = SQL_NULL_HSTMT;
        if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) {
            return odbc_error("odbc alloc stmt", odbc_diag(SQL_HANDLE_DBC, dbc));
        }
        SQLRETURN r = SQLStatistics(st, nullptr, 0, nullptr, 0,
                                    sqlstr(name), SQL_NTS,
                                    SQL_INDEX_UNIQUE, SQL_QUICK);
        if (SQL_SUCCEEDED(r)) {
            std::vector<std::vector<std::string>> rows;
            std::vector<std::vector<bool>>        nulls;
            auto rr = read_all_rows(st, rows, nulls);
            SQLFreeHandle(SQL_HANDLE_STMT, st);
            if (!rr) return rr.error();
            std::string chosen;
            std::string chosen_schema;
            bool        has_schema = false;
            std::vector<std::pair<int, std::string>> keyed;
            for (auto& row : rows) {
                const std::string schem      = row.size() >= 2 ? row[1] : "";
                const std::string non_unique = row.size() >= 4 ? row[3] : "";
                const std::string idx_name   = row.size() >= 6 ? row[5] : "";
                const std::string ord_s      = row.size() >= 8 ? row[7] : "";
                const std::string col_name   = row.size() >= 9 ? row[8] : "";
                if (col_name.empty()) continue;     // table-statistic row
                if (non_unique != "0") continue;    // only unique indexes
                if (!has_schema) { chosen_schema = schem; has_schema = true; }
                if (schem != chosen_schema) continue;
                if (chosen.empty()) chosen = idx_name;
                if (idx_name != chosen) continue;
                keyed.emplace_back(std::atoi(ord_s.c_str()), col_name);
            }
            if (!keyed.empty()) return order_keyed(std::move(keyed));
        } else {
            SQLFreeHandle(SQL_HANDLE_STMT, st);
        }
    }

    return util::Error{5001, 0,
        "table has no primary key or unique index (ODBC backend v1 "
        "navigates by a stable key)", name};
}

util::Result<void> describe_columns(SQLHDBC dbc, OdbcTable* tbl) {
    if (!is_safe_identifier(tbl->name)) {
        return util::Error{5001, 0, "invalid table name", tbl->name};
    }
    SQLHSTMT st = SQL_NULL_HSTMT;
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) {
        return odbc_error("odbc alloc stmt", odbc_diag(SQL_HANDLE_DBC, dbc));
    }
    SQLRETURN r = SQLColumns(st, nullptr, 0, nullptr, 0,
                             sqlstr(tbl->name), SQL_NTS, nullptr, 0);
    if (!SQL_SUCCEEDED(r)) {
        auto e = odbc_error("SQLColumns", odbc_diag(SQL_HANDLE_STMT, st));
        SQLFreeHandle(SQL_HANDLE_STMT, st);
        return e;
    }
    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<bool>>        nulls;
    auto rr = read_all_rows(st, rows, nulls);
    SQLFreeHandle(SQL_HANDLE_STMT, st);
    if (!rr) return rr.error();
    if (rows.empty()) {
        return util::Error{5001, 0, "table not found or has no columns",
                           tbl->name};
    }
    // SQLColumns columns (1-based): 4 COLUMN_NAME, 5 DATA_TYPE,
    // 7 COLUMN_SIZE, 9 DECIMAL_DIGITS, 11 NULLABLE. Rows arrive in
    // ORDINAL_POSITION order per the ODBC spec.
    std::string chosen_schema;
    bool        has_schema = false;
    std::vector<OdbcTable::FieldDesc> out;
    out.reserve(rows.size());
    for (auto& row : rows) {
        auto cell = [&](std::size_t one_based) -> std::string {
            std::size_t i = one_based - 1;
            return i < row.size() ? row[i] : std::string();
        };
        // Pin to the first schema seen (col 2 = TABLE_SCHEM) so a null
        // schema arg cannot mix columns of same-named tables in different
        // schemas into one field list.
        const std::string schem = cell(2);
        if (!has_schema) { chosen_schema = schem; has_schema = true; }
        if (schem != chosen_schema) continue;
        const std::string cname = cell(4);
        const int dtype = std::atoi(cell(5).c_str());
        const int csize = std::atoi(cell(7).c_str());
        const int ddig  = std::atoi(cell(9).c_str());
        const int ncode = std::atoi(cell(11).c_str());  // 0=NO_NULLS,1=NULLABLE
        out.push_back(map_odbc_column(cname, dtype, ncode != 0, csize, ddig));
    }
    tbl->fields        = std::move(out);
    tbl->fields_cached = true;
    return util::Result<void>{};
}

util::Result<void> load_pk_snapshot(SQLHDBC dbc, const std::string& q,
                                    OdbcTable* tbl) {
    const std::string list = pk_select_list(q, *tbl);
    const std::string sql =
        "SELECT " + list + " FROM " + quote_ident(q, tbl->name) +
        " ORDER BY " + list;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<bool>>        nulls;
    auto r = run_query(dbc, sql, rows, nulls);
    if (!r) return r.error();
    tbl->pk_snapshot.clear();
    tbl->pk_snapshot.reserve(rows.size());
    for (auto& row : rows) {
        OdbcTable::PkRow pk;
        pk.values = std::move(row);
        tbl->pk_snapshot.push_back(std::move(pk));
    }
    return util::Result<void>{};
}

util::Result<void> load_current_row(SQLHDBC dbc, const std::string& q,
                                    OdbcTable* tbl, std::size_t idx) {
    if (idx >= tbl->pk_snapshot.size()) {
        tbl->positioned = false;
        tbl->row_valid  = false;
        return util::Result<void>{};
    }
    const std::string sql =
        "SELECT * FROM " + quote_ident(q, tbl->name) + " WHERE " +
        pk_where_clause(q, *tbl, tbl->pk_snapshot[idx]);
    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<bool>>        nulls;
    auto r = run_query(dbc, sql, rows, nulls, /*max_rows=*/1);
    if (!r) return r.error();
    if (rows.empty()) {
        tbl->positioned = false;
        tbl->row_valid  = false;
        return util::Result<void>{};
    }
    tbl->current_row   = std::move(rows[0]);
    tbl->current_nulls = std::move(nulls[0]);
    tbl->pos           = idx;
    tbl->current_recno = static_cast<std::uint32_t>(idx + 1);
    tbl->positioned    = true;
    tbl->row_valid     = true;
    return util::Result<void>{};
}

} // namespace

struct OdbcConnection::Impl {
    SQLHENV     env = SQL_NULL_HENV;
    SQLHDBC     dbc = SQL_NULL_HDBC;
    std::string quote;
};

OdbcConnection::OdbcConnection() = default;
OdbcConnection::~OdbcConnection() { disconnect(); }

OdbcConnection::OdbcConnection(OdbcConnection&& other) noexcept
    : impl_(std::move(other.impl_)) {}

OdbcConnection& OdbcConnection::operator=(OdbcConnection&& other) noexcept {
    if (this != &other) {
        disconnect();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

util::Result<OdbcConnection> OdbcConnection::open(const OdbcUri& uri) {
    OdbcConnection conn;
    conn.impl_ = std::make_unique<Impl>();

    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE,
                                      &conn.impl_->env))) {
        return util::Error{5001, 0, "odbc: SQLAllocHandle(ENV) failed", ""};
    }
    if (!SQL_SUCCEEDED(SQLSetEnvAttr(conn.impl_->env, SQL_ATTR_ODBC_VERSION,
                                     reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3),
                                     0))) {
        return odbc_error("odbc: SQLSetEnvAttr(ODBC_VERSION)",
                          odbc_diag(SQL_HANDLE_ENV, conn.impl_->env));
    }
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, conn.impl_->env,
                                      &conn.impl_->dbc))) {
        return odbc_error("odbc: SQLAllocHandle(DBC)",
                          odbc_diag(SQL_HANDLE_ENV, conn.impl_->env));
    }

    SQLCHAR     outbuf[2048];
    SQLSMALLINT outlen = 0;
    SQLRETURN r = SQLDriverConnect(
        conn.impl_->dbc, nullptr, sqlstr(uri.connstr), SQL_NTS,
        outbuf, static_cast<SQLSMALLINT>(sizeof(outbuf)), &outlen,
        SQL_DRIVER_NOPROMPT);
    if (!SQL_SUCCEEDED(r)) {
        return odbc_error("odbc connect",
                          odbc_diag(SQL_HANDLE_DBC, conn.impl_->dbc));
    }

    SQLCHAR     q[16] = {0};
    SQLSMALLINT qlen  = 0;
    if (SQL_SUCCEEDED(SQLGetInfo(conn.impl_->dbc, SQL_IDENTIFIER_QUOTE_CHAR,
                                 q, static_cast<SQLSMALLINT>(sizeof(q)),
                                 &qlen))) {
        std::string qs(reinterpret_cast<const char*>(q));
        if (!qs.empty() && qs != " ") conn.impl_->quote = qs;
    }
    return std::move(conn);
}

void OdbcConnection::disconnect() noexcept {
    if (impl_) {
        if (impl_->dbc != SQL_NULL_HDBC) {
            SQLDisconnect(impl_->dbc);
            SQLFreeHandle(SQL_HANDLE_DBC, impl_->dbc);
            impl_->dbc = SQL_NULL_HDBC;
        }
        if (impl_->env != SQL_NULL_HENV) {
            SQLFreeHandle(SQL_HANDLE_ENV, impl_->env);
            impl_->env = SQL_NULL_HENV;
        }
    }
    impl_.reset();
}

bool OdbcConnection::valid() const noexcept {
    return impl_ && impl_->dbc != SQL_NULL_HDBC;
}

util::Result<std::unique_ptr<OdbcTable>>
OdbcConnection::open_table(const std::string& table_name) {
    if (!valid()) {
        return util::Error{5001, 0, "odbc connection not open", ""};
    }
    if (!is_safe_identifier(table_name)) {
        return util::Error{5001, 0, "invalid table name", table_name};
    }
    auto tbl  = std::make_unique<OdbcTable>();
    tbl->conn = this;
    tbl->name = table_name;

    auto pk = discover_pk(impl_->dbc, table_name);
    if (!pk) return pk.error();
    tbl->pk_columns = std::move(pk).value();

    if (auto d = describe_columns(impl_->dbc, tbl.get()); !d) {
        return d.error();
    }
    if (auto s = load_pk_snapshot(impl_->dbc, impl_->quote, tbl.get()); !s) {
        return s.error();
    }
    tbl->cached_rec_count = static_cast<std::uint32_t>(tbl->pk_snapshot.size());
    tbl->rec_count_cached = true;
    tbl->positioned       = false;
    tbl->row_valid        = false;
    tbl->pos              = 0;
    tbl->current_recno    = 0;
    return tbl;
}

util::Result<void> OdbcConnection::goto_top(OdbcTable* tbl) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid odbc goto_top", ""};
    }
    if (tbl->cached_rec_count == 0) {
        tbl->positioned    = false;
        tbl->row_valid     = false;
        tbl->current_recno = 0;
        tbl->pos           = 0;
        return util::Result<void>{};
    }
    return load_current_row(impl_->dbc, impl_->quote, tbl, 0);
}

util::Result<void> OdbcConnection::goto_bottom(OdbcTable* tbl) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid odbc goto_bottom", ""};
    }
    if (tbl->cached_rec_count == 0) {
        tbl->positioned    = false;
        tbl->row_valid     = false;
        tbl->current_recno = 0;
        tbl->pos           = 0;
        return util::Result<void>{};
    }
    return load_current_row(impl_->dbc, impl_->quote, tbl,
                            tbl->cached_rec_count - 1);
}

util::Result<void> OdbcConnection::skip(OdbcTable* tbl, std::int32_t step) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid odbc skip", ""};
    }
    if (step == 0) return util::Result<void>{};
    if (tbl->cached_rec_count == 0) {
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
            if (step < 0) next = static_cast<std::int64_t>(tbl->pos) + step;
            else return util::Result<void>{};
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
    if (static_cast<std::uint32_t>(next) >= tbl->cached_rec_count) {
        tbl->positioned = false;
        tbl->row_valid  = false;
        tbl->pos        = tbl->cached_rec_count;
        return util::Result<void>{};
    }
    return load_current_row(impl_->dbc, impl_->quote, tbl,
                            static_cast<std::size_t>(next));
}

util::Result<bool> OdbcConnection::at_eof(OdbcTable* tbl) const {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid odbc at_eof", ""};
    }
    if (tbl->cached_rec_count == 0) return true;
    if (!tbl->positioned && tbl->pos >= tbl->cached_rec_count) return true;
    return false;
}

util::Result<bool> OdbcConnection::at_bof(OdbcTable* tbl) const {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid odbc at_bof", ""};
    }
    if (tbl->cached_rec_count == 0) return true;
    return !tbl->positioned && tbl->pos == 0;
}

util::Result<std::uint32_t> OdbcConnection::record_count(OdbcTable* tbl) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid odbc record_count", ""};
    }
    if (tbl->rec_count_cached) return tbl->cached_rec_count;
    tbl->cached_rec_count = static_cast<std::uint32_t>(tbl->pk_snapshot.size());
    tbl->rec_count_cached = true;
    return tbl->cached_rec_count;
}

util::Result<std::vector<OdbcTable::FieldDesc>>
OdbcConnection::describe_table(OdbcTable* tbl) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid odbc describe_table", ""};
    }
    if (tbl->fields_cached) return tbl->fields;
    if (auto d = describe_columns(impl_->dbc, tbl); !d) return d.error();
    return tbl->fields;
}

util::Result<void> OdbcConnection::read_field(
    OdbcTable* tbl, const std::string& field_name,
    std::string& buf, bool& is_null) const {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid odbc read_field", ""};
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
}

util::Result<bool> OdbcConnection::seek_index(
    OdbcTable* tbl, const std::string& column, IndexExprKind kind,
    const std::string& key, bool soft, bool last_key) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid odbc seek", ""};
    }
    if (!is_safe_identifier(column)) {
        return util::Error{5001, 0, "invalid seek column", column};
    }
    if (!tbl->fields_cached) {
        if (auto d = describe_columns(impl_->dbc, tbl); !d) return d.error();
    }
    if (field_index_ci(*tbl, column) == static_cast<std::size_t>(-1)) {
        return util::Error{5063, 0, "seek column not found", column};
    }

    const std::string& q      = impl_->quote;
    const std::string  pkcols = pk_select_list(q, *tbl);
    const std::string  esc    = (kind == IndexExprKind::UpperColumn)
                                    ? escape_literal(key)
                                    : format_literal(*tbl, column, key);
    const std::string  qexpr  = index_column_sql(q, column, kind);
    const std::string  from   = " FROM " + quote_ident(q, tbl->name);

    std::string sql;
    if (last_key) {
        sql = soft
            ? "SELECT " + pkcols + from + " WHERE " + qexpr + " <= " + esc +
              " ORDER BY " + qexpr + " DESC"
            : "SELECT " + pkcols + from + " WHERE " + qexpr + " = " + esc +
              " ORDER BY " + qexpr + " DESC";
    } else {
        sql = soft
            ? "SELECT " + pkcols + from + " WHERE " + qexpr + " >= " + esc +
              " ORDER BY " + qexpr + " ASC"
            : "SELECT " + pkcols + from + " WHERE " + qexpr + " = " + esc;
    }

    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<bool>>        nulls;
    auto r = run_query(impl_->dbc, sql, rows, nulls, /*max_rows=*/1);
    if (!r) return r.error();

    bool found = false;
    if (!rows.empty()) {
        OdbcTable::PkRow pk;
        pk.values = rows[0];
        std::size_t pos = static_cast<std::size_t>(-1);
        for (std::size_t i = 0; i < tbl->pk_snapshot.size(); ++i) {
            if (tbl->pk_snapshot[i] == pk) {
                pos = i;
                break;
            }
        }
        if (pos != static_cast<std::size_t>(-1)) {
            if (auto lr = load_current_row(impl_->dbc, q, tbl, pos); !lr) {
                return lr.error();
            }
            found = tbl->positioned && tbl->row_valid;
        } else {
            tbl->positioned = false;
            tbl->row_valid  = false;
            found           = false;
        }
    } else {
        tbl->positioned = false;
        tbl->row_valid  = false;
        found           = false;
    }
    tbl->last_seek_found = found;
    return found;
}

} // namespace openads::sql_backend

#endif // OPENADS_WITH_ODBC
