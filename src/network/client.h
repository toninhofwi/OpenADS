#pragma once

#include "network/server.h"
#include "network/socket.h"
#include "network/transport.h"
#include "network/wire.h"
#include "util/result.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

    // M12.12 — connect using a pre-built transport (e.g. a
    // TlsTransport built by `connect_tls`). The Connect frame is
    // sent through the supplied transport; ownership passes to
    // RemoteConnection.
    util::Result<void>
        connect_with_transport(std::unique_ptr<ITransport> transport,
                               const std::string& data_dir,
                               const std::string& user     = "",
                               const std::string& password = "");

    void               disconnect() noexcept;
    bool               valid() const noexcept {
        return transport_ && transport_->valid();
    }

    util::Result<std::uint32_t> open_table(const std::string& rel);
    util::Result<void>          close_table(std::uint32_t id);
    util::Result<void>          goto_top(std::uint32_t id);
    util::Result<void>          skip(std::uint32_t id, std::int32_t step);
    util::Result<std::string>   get_field(std::uint32_t id,
                                           const std::string& field_name);
    util::Result<std::uint32_t> record_count(std::uint32_t id);
    util::Result<bool>          at_eof(std::uint32_t id);
    // M12.14 — remote field metadata + extended cursor state.
    struct FieldDesc {
        std::string   name;
        std::uint16_t type     = 0;     // ADS_* code
        std::uint32_t length   = 0;
        std::uint16_t decimals = 0;
    };
    util::Result<std::vector<FieldDesc>>
                                describe_table(std::uint32_t id);
    util::Result<bool>          at_bof(std::uint32_t id);
    util::Result<std::uint32_t> get_record_num(std::uint32_t id);
    util::Result<bool>          is_record_deleted(std::uint32_t id);
    util::Result<void>          goto_bottom(std::uint32_t id);
    // M12.15 — remote info / lock / maintenance / AOF.
    util::Result<bool>          is_found(std::uint32_t id);
    util::Result<void>          refresh_record(std::uint32_t id);
    util::Result<std::uint16_t> get_table_type(std::uint32_t id);
    util::Result<std::uint32_t> get_record_length(std::uint32_t id);
    util::Result<std::uint16_t> get_num_indexes(std::uint32_t id);
    util::Result<std::uint32_t> get_last_autoinc(std::uint32_t id);
    util::Result<void>          lock_record(std::uint32_t id, std::uint32_t recno);
    util::Result<void>          unlock_record(std::uint32_t id, std::uint32_t recno);
    util::Result<void>          lock_table(std::uint32_t id);
    util::Result<void>          unlock_table(std::uint32_t id);
    util::Result<void>          pack_table(std::uint32_t id);
    util::Result<void>          zap_table(std::uint32_t id);
    util::Result<void>          flush_file_buffers(std::uint32_t id);
    util::Result<void>          close_all_indexes(std::uint32_t id);
    util::Result<void>          set_aof(std::uint32_t id, const std::string& cond);
    util::Result<void>          clear_aof(std::uint32_t id);
    util::Result<std::uint16_t> get_aof_opt_level(std::uint32_t id);
    // M12.16 — remote index handle subsystem.
    util::Result<std::vector<std::uint32_t>>
                                open_index(std::uint32_t table_id,
                                           const std::string& path);
    util::Result<void>          close_index(std::uint32_t index_id);
    util::Result<void>          set_order(std::uint32_t table_id,
                                          std::uint32_t index_id);
    util::Result<void>          set_order_by_name(std::uint32_t table_id,
                                                   const std::string& tag);
    struct SeekOutcome {
        std::uint8_t  hit  = 0;     // 1 = exact, 0 = soft / not found
        std::uint32_t recno = 0;
    };
    util::Result<SeekOutcome>   seek(std::uint32_t index_id,
                                     const std::string& key,
                                     std::uint8_t soft,
                                     std::uint8_t last);
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
    // M12.11 — batch row read. Walks up to `max_rows` rows starting
    // at the current cursor position, returning a row-major matrix of
    // column values. Empty result set ⇒ empty outer vector. Reduces
    // per-row Skip/GetField round-trips for WAN-latency callers.
    util::Result<std::vector<std::vector<std::string>>>
        fetch_batch(std::uint32_t                   id,
                    std::uint32_t                   max_rows,
                    const std::vector<std::string>& columns);

private:
    util::Result<Frame> request(const Frame& f);

    std::unique_ptr<ITransport> transport_;
    std::mutex                  mu_;
};

// Per-handle wrapper for a remote table. Stores back-pointer to
// the connection plus the server-side table id.
struct RemoteTable {
    RemoteConnection* conn = nullptr;
    std::uint32_t     id   = 0;
    // M12.14 — schema cache populated lazily on first
    // AdsGetNumFields / AdsGetFieldName / ... call so rddads'
    // adsOpen field-iteration loop stays at one wire round-trip.
    std::vector<RemoteConnection::FieldDesc> fields;
    bool fields_cached = false;
};

// M12.16 — per-handle wrapper for a remote index. Each tag
// returned by AdsOpenIndex (the multi-tag CDX case) gets one of
// these so the ABI surface can hand the host a real ADSHANDLE.
struct RemoteIndex {
    RemoteConnection* conn  = nullptr;
    std::uint32_t     id    = 0;     // server-side index id
    std::uint32_t     tbl_id = 0;    // server-side table id this binds to
};

// Parse `tcp://host:port/<data_dir>` into its parts. Returns
// false when the input doesn't carry the tcp:// prefix.
bool parse_tcp_uri(const std::string& uri,
                   std::string& host,
                   std::uint16_t& port,
                   std::string& data_dir);

// M12.12 — TLS URI scheme `tls://host:port/<data_dir>` is reserved
// but not yet implemented. parse_tls_uri only returns true when the
// input carries the tls:// prefix; AdsConnect60 surfaces
// AE_FUNCTION_NOT_AVAILABLE so apps get a clear failure instead of
// a silent plaintext fallback. Real TLS (vendored mbedtls /
// platform-native) lands in v0.4.0.
bool parse_tls_uri(const std::string& uri,
                   std::string& host,
                   std::uint16_t& port,
                   std::string& data_dir);

} // namespace openads::network
