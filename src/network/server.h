#pragma once

#include "network/socket.h"
#include "network/wire.h"
#include "util/result.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
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

private:
    void accept_loop();
    void session_loop(Socket s);

    Socket                   listener_;
    std::uint16_t            port_ = 0;
    std::atomic<bool>        running_{false};
    std::thread              accept_thread_;
    std::mutex               sessions_mu_;
    std::vector<std::thread> sessions_;
};

// Read exactly `n` bytes into `buf` (handles partial recvs).
util::Result<void> recv_exact(Socket& s, std::uint8_t* buf,
                              std::size_t n);

// Read one wire frame (4-byte BE length + opcode + payload).
util::Result<Frame> read_frame(Socket& s);

// Write `f` to `s` as a single wire frame.
util::Result<void> write_frame(Socket& s, const Frame& f);

} // namespace openads::network
