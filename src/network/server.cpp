#include "network/server.h"

#include <cstring>

namespace openads::network {

util::Result<void> recv_exact(Socket& s, std::uint8_t* buf,
                              std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        auto r = sock_recv(s, buf + got, n - got);
        if (!r) return r.error();
        if (r.value() == 0) {
            return util::Error{5000, 0, "peer closed connection", ""};
        }
        got += r.value();
    }
    return {};
}

util::Result<Frame> read_frame(Socket& s) {
    std::uint8_t hdr[5];
    if (auto r = recv_exact(s, hdr, sizeof(hdr)); !r) return r.error();
    std::uint32_t n =
        (static_cast<std::uint32_t>(hdr[0]) << 24) |
        (static_cast<std::uint32_t>(hdr[1]) << 16) |
        (static_cast<std::uint32_t>(hdr[2]) <<  8) |
         static_cast<std::uint32_t>(hdr[3]);
    Frame f;
    f.opcode = static_cast<Opcode>(hdr[4]);
    if (n > 0) {
        f.payload.resize(n);
        if (auto r = recv_exact(s, f.payload.data(), n); !r) return r.error();
    }
    return f;
}

util::Result<void> write_frame(Socket& s, const Frame& f) {
    auto enc = encode_frame(f);
    if (!enc) return enc.error();
    auto& bytes = enc.value();
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        auto r = sock_send(s, bytes.data() + sent, bytes.size() - sent);
        if (!r) return r.error();
        if (r.value() == 0) {
            return util::Error{5000, 0, "send returned 0", ""};
        }
        sent += r.value();
    }
    return {};
}

Server::~Server() { stop(); }

util::Result<void> Server::start(const std::string& host,
                                 std::uint16_t port) {
    if (running_.load()) return {};
    ListenerOptions opts;
    opts.host = host;
    opts.port = port;
    auto l = listen_tcp(opts);
    if (!l) return l.error();
    listener_ = l.value();
    auto p = socket_local_port(listener_);
    if (!p) {
        sock_close(listener_);
        return p.error();
    }
    port_ = p.value();
    running_.store(true);
    accept_thread_ = std::thread([this]() { this->accept_loop(); });
    return {};
}

void Server::stop() noexcept {
    if (!running_.exchange(false)) return;
    // Closing the listener wakes the blocking accept() with an
    // error on both Win32 and POSIX.
    sock_close(listener_);
    if (accept_thread_.joinable()) accept_thread_.join();
    std::lock_guard<std::mutex> lk(sessions_mu_);
    for (auto& t : sessions_) {
        if (t.joinable()) t.join();
    }
    sessions_.clear();
}

void Server::accept_loop() {
    while (running_.load()) {
        auto cli = accept_one(listener_);
        if (!cli) {
            // accept failed — likely listener closed by stop().
            break;
        }
        Socket s = cli.value();
        std::lock_guard<std::mutex> lk(sessions_mu_);
        sessions_.emplace_back([this, s]() mutable {
            this->session_loop(s);
        });
    }
}

void Server::session_loop(Socket s) {
    while (true) {
        auto fr = read_frame(s);
        if (!fr) break;                       // connection closed
        const Frame& f = fr.value();
        Frame reply;
        switch (f.opcode) {
            case Opcode::Hello: {
                reply.opcode = Opcode::HelloAck;
                std::string v = "openads/0.3.2";
                reply.payload.assign(v.begin(), v.end());
                break;
            }
            case Opcode::Connect: {
                reply.opcode = Opcode::ConnectAck;
                std::string p = "connected:";
                p.append(reinterpret_cast<const char*>(f.payload.data()),
                         f.payload.size());
                reply.payload.assign(p.begin(), p.end());
                break;
            }
            case Opcode::Disconnect: {
                sock_close(s);
                return;
            }
            default: {
                reply.opcode = Opcode::Error;
                std::string e = "unsupported opcode";
                reply.payload.assign(e.begin(), e.end());
                break;
            }
        }
        if (auto wr = write_frame(s, reply); !wr) break;
    }
    sock_close(s);
}

} // namespace openads::network
