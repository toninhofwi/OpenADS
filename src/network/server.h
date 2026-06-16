#pragma once

#include "mgmt/mg_snapshot.h"
#include "network/socket.h"
#include "network/transport.h"
#include "network/wire.h"
#include "util/result.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace openads::network {

// M12.3 — Phase 2 server skeleton. Spawns an accept thread that
// hands each connection to a dedicated session thread. The session
// thread reads framed messages and dispatches a small handful of
// opcodes:
//
//   Hello    → HelloAck (payload = server version string)
//   Connect  → ConnectAck (payload = "connected:<data_dir>")
//   any other → Error (payload = "unsupported opcode")
//
// Real remote table ops (OpenTable, ExecuteSQL, Fetch) plug into
// this dispatcher in M12.4.

class Server {
public:
    Server() = default;
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    ~Server();

    util::Result<void> start(const std::string& host,
                             std::uint16_t port);
    std::uint16_t      port() const noexcept { return port_; }
    bool               running() const noexcept { return running_.load(); }
    void               stop() noexcept;

    // M12.9 — auth. When at least one credential is registered, every
    // Connect frame must carry a matching user / password pair; an
    // empty map accepts any client (back-compat / dev mode).
    void add_credential(const std::string& user,
                        const std::string& password);
    bool require_auth() const noexcept;

    // Set the data root directory. Relative paths from Connect frames
    // are resolved under this directory.
    void set_data_dir(const std::string& dir) { data_dir_ = dir; }

    // studio.web.0.4 — observable session info exposed for the
    // Studio "Sessions" tab. Snapshot is taken under a mutex so
    // concurrent reads from the HTTP console are safe.
    struct SessionInfo {
        std::uint64_t                                       id = 0;
        std::string                                         peer_ip;
        std::uint16_t                                       peer_port = 0;
        std::string                                         user;
        std::string                                         data_dir;
        std::chrono::system_clock::time_point               connected_at{};
        std::chrono::system_clock::time_point               last_activity{};
        std::uint64_t                                       frames_in   = 0;
        std::uint64_t                                       frames_out  = 0;
        std::uint32_t                                       open_tables = 0;
    };
    std::vector<SessionInfo> sessions_snapshot() const;

    // M9.25 — build a management telemetry snapshot from the live
    // session registry. Used by the MgRequest opcode handler.
    mgmt::MgSnapshot build_mg_snapshot() const;

private:
    void accept_loop();
    void session_loop(Socket s);

    Socket                   listener_;
    std::uint16_t            port_ = 0;
    std::atomic<bool>        running_{false};
    std::thread              accept_thread_;
    mutable std::mutex       sessions_mu_;
    std::vector<std::thread> sessions_;

    // Data root directory: relative client paths are resolved under it.
    std::string                                   data_dir_;

    // M12.9 — credential map (user -> password). Read-only after
    // start() returns; set up at construction time / before start.
    std::unordered_map<std::string, std::string> creds_;

    // studio.web.0.4 — live session registry. session_loop
    // updates these counters; the HTTP console reads them via
    // sessions_snapshot().
    mutable std::mutex                          info_mu_;
    std::unordered_map<std::uint64_t, SessionInfo> sessions_info_;
    std::atomic<std::uint64_t>                  next_session_id_{1};

public:
    // Internal helpers used by session_loop; public so the
    // anonymous-namespace functions in server.cpp can reach
    // them without exposing extra friend declarations.
    std::uint64_t register_session(const SessionInfo& info);
    void          unregister_session(std::uint64_t id);
    void          touch_session(std::uint64_t id, bool inbound,
                                 bool outbound);
    void          set_session_user(std::uint64_t id,
                                    const std::string& user,
                                    const std::string& data_dir);
    void          add_session_table(std::uint64_t id,
                                     std::int32_t delta);

    // studio.web.0.11 — drop a single session by id (closes its
    // socket; the session_loop's next read_frame returns and the
    // session thread exits cleanly). Returns true if the id was
    // known.
    bool          kill_session(std::uint64_t id);
    // Internal — session_loop's RAII guard uses these to install /
    // remove its accepted socket from the kill registry.
    void          install_session_socket(std::uint64_t id, Socket s);
    void          erase_session_socket  (std::uint64_t id);

private:
    // Kill the session whose 1-based position in sessions_snapshot()
    // order equals conn_no. Returns true if one was found.
    bool kill_session_by_conn_no(std::uint16_t conn_no);

    // Track each session_loop's accepted socket so kill_session
    // can close it from outside the thread that owns it.
    std::unordered_map<std::uint64_t, Socket> sockets_;
};

// Read exactly `n` bytes into `buf` (handles partial recvs).
util::Result<void> recv_exact(Socket& s, std::uint8_t* buf,
                              std::size_t n);
util::Result<void> recv_exact(ITransport& t, std::uint8_t* buf,
                              std::size_t n);

// Read one wire frame (4-byte BE length + opcode + payload).
util::Result<Frame> read_frame(Socket& s);
util::Result<Frame> read_frame(ITransport& t);

// Write `f` to `s` as a single wire frame.
util::Result<void> write_frame(Socket& s, const Frame& f);
util::Result<void> write_frame(ITransport& t, const Frame& f);

} // namespace openads::network
