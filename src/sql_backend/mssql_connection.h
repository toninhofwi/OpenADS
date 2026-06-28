#pragma once

// Native MS SQL Server backend connection.  Orchestrates the full TDS 7.4
// handshake over a TLS-in-TDS channel:
//   TCP connect -> PRELOGIN -> tunnelled TLS handshake -> LOGIN7 -> LOGINACK.
//
// Table navigation buffers a SELECT * result set in memory; navigational
// write (append / update / delete) stages field values and flushes one
// INSERT / UPDATE / DELETE per record, then refetches the table.

#if defined(OPENADS_WITH_MSSQL)

#include "sql_backend/backend_tx_manager.h"
#include "sql_backend/mssql_table.h"
#include "engine/aggregate.h"
#include "sql_backend/tds_protocol.h"
#include "sql_backend/tds_tls_channel.h"
#include "util/result.h"

#include <memory>
#include <string>
#include <vector>

namespace openads::sql_backend {

struct MssqlUri;  // sql_backend/mssql_uri.h

class MssqlConnection {
public:
    MssqlConnection();
    ~MssqlConnection();

    MssqlConnection(MssqlConnection&&) noexcept;
    MssqlConnection& operator=(MssqlConnection&&) noexcept;

    MssqlConnection(const MssqlConnection&)            = delete;
    MssqlConnection& operator=(const MssqlConnection&) = delete;

    // Connect + authenticate.  On a wrong password / bad login the server
    // sends a TDS ERROR token; open() returns that error number (NEVER the
    // password or connection string).
    static util::Result<MssqlConnection> open(const MssqlUri& uri);

    void disconnect() noexcept;
    bool valid() const noexcept;

    // Execute a SQL batch and return the decoded result set.
    // The connection must be authenticated (valid() == true).
    // SQL text is backend-generated; NEVER put secrets or credentials in sql.
    util::Result<tds::QueryResult> query(const std::string& sql);

    util::Result<void> exec_sql(const std::string& sql);

    util::Result<void> set_filter(MssqlTable* tbl, const std::string& where);
    util::Result<void> refresh_where_filter(MssqlTable* tbl);

    util::Result<std::vector<engine::AggValue>>
        aggregate(MssqlTable* tbl,
                  const std::string& where_sql,
                  const std::vector<engine::AggSpec>& specs);

    util::Result<bool> seek_index(MssqlTable* tbl,
                                  const std::string& column,
                                  const std::string& key,
                                  bool soft,
                                  bool last_key);

    util::Result<void> lock_record(MssqlTable* tbl, std::uint32_t recno);
    util::Result<void> unlock_record(MssqlTable* tbl, std::uint32_t recno);
    util::Result<void> lock_table(MssqlTable* tbl);
    util::Result<void> unlock_table(MssqlTable* tbl);

    // Discover primary-key column names via INFORMATION_SCHEMA.
    util::Result<std::vector<std::string>>
        discover_pk(const std::string& table_name);

    // Navigational write surface (mirrors MariaDB / ODBC backends).
    util::Result<void> append_blank(MssqlTable* tbl);
    util::Result<void> set_field(MssqlTable* tbl,
                                 const std::string& field_name,
                                 const std::string& value);
    util::Result<void> flush_record(MssqlTable* tbl);
    util::Result<void> delete_record(MssqlTable* tbl);

    BackendTxManager& tx_manager() noexcept { return tx_mgr_; }
    const BackendTxManager& tx_manager() const noexcept { return tx_mgr_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    BackendTxManager        tx_mgr_;
};

} // namespace openads::sql_backend

#endif // defined(OPENADS_WITH_MSSQL)