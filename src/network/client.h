#pragma once

#include "network/server.h"
#include "network/socket.h"
#include "network/wire.h"
#include "util/result.h"

#include <cstdint>
#include <mutex>
#include <string>

namespace openads::network {

// M12.5 — wire client used by ace64.dll's dual-mode dispatch.
// `RemoteConnection` opens a TCP socket to an OpenADS server,
// sends a Connect frame for the data_dir, and exposes a small
// surface mirroring the local Connection methods that the
// remote-routed Ads* functions need.

class RemoteConnection {
public:
    RemoteConnection() = default;
    ~RemoteConnection() { disconnect(); }
    RemoteConnection(const RemoteConnection&) = delete;
    RemoteConnection& operator=(const RemoteConnection&) = delete;

    util::Result<void> connect(const std::string& host,
                               std::uint16_t port,
                               const std::string& data_dir,
                               const std::string& user     = "",
                               const std::string& password = "");
    void               disconnect() noexcept;
    bool               valid() const noexcept { return sock_.valid(); }

    util::Result<std::uint32_t> open_table(const std::string& rel);
    util::Result<void>          close_table(std::uint32_t id);
    util::Result<void>          goto_top(std::uint32_t id);
    util::Result<void>          skip(std::uint32_t id, std::int32_t step);
    util::Result<std::string>   get_field(std::uint32_t id,
                                           const std::string& field_name);
    util::Result<std::uint32_t> record_count(std::uint32_t id);
    util::Result<bool>          at_eof(std::uint32_t id);
    // M12.6 — remote write surface.
    util::Result<void>          append_blank(std::uint32_t id);
    util::Result<void>          set_field(std::uint32_t id,
                                          const std::string& field_name,
                                          const std::string& value);
    util::Result<void>          delete_record(std::uint32_t id);
    util::Result<void>          recall_record(std::uint32_t id);
    util::Result<void>          goto_record(std::uint32_t id,
                                            std::uint32_t recno);
    util::Result<void>          flush_table(std::uint32_t id);
    // M12.7 — remote SQL exec. Returns cursor table-id (0 = no cursor,
    // i.e. INSERT / UPDATE / DELETE / DDL).
    util::Result<std::uint32_t> execute_sql(const std::string& sql);
    // M12.8 — remote index ops.
    util::Result<void>          reindex(std::uint32_t id);

private:
    util::Result<Frame> request(const Frame& f);

    Socket      sock_;
    std::mutex  mu_;
};

// Per-handle wrapper for a remote table. Stores back-pointer to
// the connection plus the server-side table id.
struct RemoteTable {
    RemoteConnection* conn = nullptr;
    std::uint32_t     id   = 0;
};

// Parse `tcp://host:port/<data_dir>` into its parts. Returns
// false when the input doesn't carry the tcp:// prefix.
bool parse_tcp_uri(const std::string& uri,
                   std::string& host,
                   std::uint16_t& port,
                   std::string& data_dir);

} // namespace openads::network
