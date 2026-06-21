#pragma once

#include "sql_backend/postgres_table.h"
#include "sql_backend/postgres_uri.h"
#include "util/result.h"

#include <cstdint>
#include <memory>
#include <string>

namespace openads::sql_backend {

class PostgresConnection {
public:
    PostgresConnection();
    ~PostgresConnection();

    PostgresConnection(PostgresConnection&&) noexcept;
    PostgresConnection& operator=(PostgresConnection&&) noexcept;

    PostgresConnection(const PostgresConnection&)            = delete;
    PostgresConnection& operator=(const PostgresConnection&) = delete;

    static util::Result<PostgresConnection> open(const PostgresUri& uri);

    void disconnect() noexcept;
    bool valid() const noexcept;

    util::Result<std::unique_ptr<PostgresTable>>
        open_table(const std::string& table_name);

    util::Result<void> goto_top(PostgresTable* tbl);
    util::Result<void> goto_bottom(PostgresTable* tbl);
    util::Result<void> skip(PostgresTable* tbl, std::int32_t step);

    util::Result<bool>          at_eof(PostgresTable* tbl) const;
    util::Result<bool>          at_bof(PostgresTable* tbl) const;
    util::Result<std::uint32_t> record_count(PostgresTable* tbl);

    util::Result<std::vector<PostgresTable::FieldDesc>>
        describe_table(PostgresTable* tbl);

    util::Result<void> read_field(PostgresTable* tbl,
                                  const std::string& field_name,
                                  std::string& buf,
                                  bool& is_null) const;

    util::Result<bool> seek_index(PostgresTable* tbl,
                                  const std::string& column,
                                  const std::string& key,
                                  bool soft,
                                  bool last_key);

    const std::string& conninfo() const noexcept { return conninfo_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string           conninfo_;
};

} // namespace openads::sql_backend