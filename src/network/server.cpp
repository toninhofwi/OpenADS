#include "network/server.h"

#include "engine/aof_eval.h"
#include "engine/aof_expr.h"
#include "engine/table.h"
#include "mgmt/mg_collector.h"
#include "mgmt/mg_stats.h"
#include "network/mg_wire.h"
#include "platform/proc.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "session/connection.h"
#include "sql_backend/enterprise_config.h"

#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

util::Result<void> recv_exact(ITransport& t, std::uint8_t* buf,
                              std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        auto r = t.recv(buf + got, n - got);
        if (!r) return r.error();
        if (r.value() == 0) {
            return util::Error{5000, 0, "peer closed connection", ""};
        }
        got += r.value();
    }
    return {};
}

namespace {

template <class ReadPayloadFn>
util::Result<Frame> decode_after_recv(std::uint8_t hdr[5],
                                       ReadPayloadFn&& read_payload) {
    std::uint32_t n =
        (static_cast<std::uint32_t>(hdr[0]) << 24) |
        (static_cast<std::uint32_t>(hdr[1]) << 16) |
        (static_cast<std::uint32_t>(hdr[2]) <<  8) |
         static_cast<std::uint32_t>(hdr[3]);
    if (n > kMaxFramePayload) {
        return util::Error{5000, 0, "frame payload too large", ""};
    }
    Frame f;
    f.opcode = static_cast<Opcode>(hdr[4]);
    if (n > 0) {
        f.payload.resize(n);
        if (auto r = read_payload(f.payload.data(), n); !r) return r.error();
    }
    return f;
}

} // namespace

util::Result<Frame> read_frame(Socket& s) {
    std::uint8_t hdr[5];
    if (auto r = recv_exact(s, hdr, sizeof(hdr)); !r) return r.error();
    return decode_after_recv(hdr, [&](std::uint8_t* b, std::size_t n) {
        return recv_exact(s, b, n);
    });
}

util::Result<Frame> read_frame(ITransport& t) {
    std::uint8_t hdr[5];
    if (auto r = recv_exact(t, hdr, sizeof(hdr)); !r) return r.error();
    return decode_after_recv(hdr, [&](std::uint8_t* b, std::size_t n) {
        return recv_exact(t, b, n);
    });
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

util::Result<void> write_frame(ITransport& t, const Frame& f) {
    auto enc = encode_frame(f);
    if (!enc) return enc.error();
    auto& bytes = enc.value();
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        auto r = t.send(bytes.data() + sent, bytes.size() - sent);
        if (!r) return r.error();
        if (r.value() == 0) {
            return util::Error{5000, 0, "send returned 0", ""};
        }
        sent += r.value();
    }
    return {};
}

Server::~Server() { stop(); }

void Server::add_credential(const std::string& user,
                            const std::string& password) {
    creds_[user] = password;
}

bool Server::require_auth() const noexcept { return !creds_.empty(); }

std::vector<Server::SessionInfo> Server::sessions_snapshot() const {
    std::lock_guard<std::mutex> lk(info_mu_);
    std::vector<SessionInfo> out;
    out.reserve(sessions_info_.size());
    for (auto& kv : sessions_info_) out.push_back(kv.second);
    return out;
}

mgmt::MgSnapshot Server::build_mg_snapshot() const {
    mgmt::MgSnapshot snap;
    auto sessions = sessions_snapshot();

    snap.connections = static_cast<std::uint32_t>(sessions.size());
    snap.server_type = 1;   // 1 = remote server
    snap.server_port = port_;
    snap.rss_bytes   = openads::platform::process_rss_bytes();

    {
        std::lock_guard<std::mutex> g(sessions_mu_);
        snap.worker_threads =
            static_cast<std::uint32_t>(session_threads_.size());
    }

    std::uint32_t conn_no = 1;
    for (const auto& s : sessions) {
        mgmt::MgUser u;
        u.name    = s.user.empty() ? "(anonymous)" : s.user;
        u.address = s.peer_ip + ":" + std::to_string(s.peer_port);
        u.os_login     = u.name;
        u.conn_no      = static_cast<std::uint16_t>(conn_no);
        u.connected_at = s.connected_at;
        snap.user_list.push_back(std::move(u));

        snap.tables    += s.open_tables;
        snap.workareas += s.open_tables;
        ++conn_no;
    }
    snap.users = static_cast<std::uint32_t>(snap.user_list.size());
    // Fold in this server's cumulative MgStats (uptime, comm totals,
    // high-water marks) so it travels the wire with the live counts.
    mgmt::capture_mg_stats(snap, mgmt::process_mg_stats());
    return snap;
}

std::uint64_t Server::register_session(const SessionInfo& info) {
    std::lock_guard<std::mutex> lk(info_mu_);
    auto id = next_session_id_.fetch_add(1);
    SessionInfo si = info;
    si.id = id;
    sessions_info_.emplace(id, std::move(si));
    // M9.25 — raise the connection high-water mark under info_mu_.
    openads::mgmt::MgStats::bump_max(
        openads::mgmt::process_mg_stats().max_connections,
        static_cast<std::uint32_t>(sessions_info_.size()));
    return id;
}

void Server::unregister_session(std::uint64_t id) {
    std::lock_guard<std::mutex> lk(info_mu_);
    sessions_info_.erase(id);
    // M9.25 — count one disconnect per terminated session. This runs
    // exactly once per session via session_loop's SessionGuard dtor.
    openads::mgmt::process_mg_stats()
        .disconnects.fetch_add(1, std::memory_order_relaxed);
}

void Server::touch_session(std::uint64_t id, bool inbound, bool outbound) {
    std::lock_guard<std::mutex> lk(info_mu_);
    auto it = sessions_info_.find(id);
    if (it == sessions_info_.end()) return;
    it->second.last_activity = std::chrono::system_clock::now();
    if (inbound)  ++it->second.frames_in;
    if (outbound) ++it->second.frames_out;
}

void Server::set_session_user(std::uint64_t id,
                               const std::string& user,
                               const std::string& data_dir) {
    std::lock_guard<std::mutex> lk(info_mu_);
    auto it = sessions_info_.find(id);
    if (it == sessions_info_.end()) return;
    it->second.user     = user;
    it->second.data_dir = data_dir;
}

void Server::add_session_table(std::uint64_t id, std::int32_t delta) {
    std::lock_guard<std::mutex> lk(info_mu_);
    auto it = sessions_info_.find(id);
    if (it == sessions_info_.end()) return;
    auto& n = it->second.open_tables;
    if (delta < 0) {
        std::uint32_t d = static_cast<std::uint32_t>(-delta);
        n = (n > d) ? n - d : 0;
    } else {
        n += static_cast<std::uint32_t>(delta);
    }
}

void Server::install_session_socket(std::uint64_t id, Socket s) {
    std::lock_guard<std::mutex> lk(info_mu_);
    sockets_[id] = s;
}

void Server::erase_session_socket(std::uint64_t id) {
    std::lock_guard<std::mutex> lk(info_mu_);
    sockets_.erase(id);
}

bool Server::kill_session(std::uint64_t id) {
    Socket sock;
    {
        std::lock_guard<std::mutex> lk(info_mu_);
        auto it = sockets_.find(id);
        if (it == sockets_.end()) return false;
        sock = it->second;
        sockets_.erase(it);
    }
    // Closing the socket from outside the owning thread wakes its
    // blocking recv() with EINTR / connection reset; session_loop
    // breaks out of its while-loop and the unregister cleanup
    // runs as normal.
    if (sock.valid()) sock_close(sock);
    return true;
}

bool Server::kill_session_by_conn_no(std::uint16_t conn_no) {
    auto sessions = sessions_snapshot();
    if (conn_no == 0 || conn_no > sessions.size()) return false;
    return kill_session(sessions[conn_no - 1].id);
}

util::Result<void> Server::start(const std::string& host,
                                 std::uint16_t port) {
    if (running_.load()) return {};
    // Resolve enterprise limits once at start. A test/embedder override wins;
    // otherwise take the env-loaded EnterpriseConfig (OPENADS_SERVER_*).
    const auto& ecfg = openads::sql_backend::enterprise_config();
    max_sessions_ = (max_sessions_override_ != 0)
                        ? max_sessions_override_
                        : ecfg.server_max_sessions;
    ListenerOptions opts;
    opts.host = host;
    opts.port = port;
    opts.backlog = static_cast<int>(ecfg.server_listen_backlog);
    auto l = listen_tcp(opts);
    if (!l) return l.error();
    listener_ = l.value();
    auto p = socket_local_port(listener_);
    if (!p) {
        sock_close(listener_);
        return p.error();
    }
    port_ = p.value();
    // M9.25 — fix the telemetry uptime origin at server start.
    openads::mgmt::process_mg_stats().start_time =
        std::chrono::system_clock::now();
    running_.store(true);
    accept_thread_ = std::thread([this]() { this->accept_loop(); });
    return {};
}

void Server::stop() noexcept {
    if (!running_.exchange(false)) return;
    // Closing the listener wakes blocking accept() on Linux + Win32,
    // but macOS BSD sockets don't always abort a pending accept on
    // close/shutdown. Force a self-connect on the listener's port
    // first so accept() returns; the accept-loop then notices
    // running_ == false and exits.
    auto port_r = socket_local_port(listener_);
    if (port_r) {
        auto wake = connect_tcp("127.0.0.1", port_r.value());
        if (wake) sock_close(wake.value());
    }
    sock_close(listener_);
    if (accept_thread_.joinable()) accept_thread_.join();
    std::lock_guard<std::mutex> lk(sessions_mu_);
    for (auto& kv : session_threads_) {
        if (kv.second.joinable()) kv.second.join();
    }
    session_threads_.clear();
    finished_threads_.clear();
}

void Server::accept_loop() {
    while (running_.load()) {
        auto cli = accept_one(listener_);
        if (!cli) {
            // accept failed — listener closed by stop().
            break;
        }
        // M12.3 — stop() may have used a self-connect to drain a
        // BSD accept that doesn't honor close-during-accept. If
        // running_ is now false, the connection is the wake-up
        // probe; drop it and exit.
        if (!running_.load()) {
            Socket s = cli.value();
            sock_close(s);
            break;
        }
        Socket s = cli.value();
        // Reap threads whose session_loop already returned so the live set
        // stays bounded on a long-running server.
        reap_finished_threads_();
        {
            std::lock_guard<std::mutex> lk(sessions_mu_);
            if (max_sessions_ != 0 &&
                session_threads_.size() >= max_sessions_) {
                // At capacity: refuse this connection instead of spawning an
                // unbounded number of threads. The client sees a dropped
                // connection (a future milestone can send a "busy" frame).
                rejected_sessions_.fetch_add(1);
                sock_close(s);
                continue;
            }
            std::uint64_t tid = thread_seq_.fetch_add(1);
            // The session records its own id in finished_threads_ when its loop
            // returns; the next reap joins and drops it. The push takes
            // sessions_mu_, which this scope holds during emplace, so the id is
            // in the map before the thread can mark itself finished.
            session_threads_.emplace(tid,
                std::thread([this, s, tid]() mutable {
                    this->session_loop(s);
                    std::lock_guard<std::mutex> lk2(sessions_mu_);
                    finished_threads_.push_back(tid);
                }));
        }
    }
}

void Server::reap_finished_threads_() {
    std::lock_guard<std::mutex> lk(sessions_mu_);
    for (std::uint64_t id : finished_threads_) {
        auto it = session_threads_.find(id);
        if (it == session_threads_.end()) continue;
        if (it->second.joinable()) it->second.join();
        session_threads_.erase(it);
    }
    finished_threads_.clear();
}

std::uint32_t Server::active_session_threads() const {
    std::lock_guard<std::mutex> lk(sessions_mu_);
    return static_cast<std::uint32_t>(session_threads_.size());
}

namespace {

inline std::uint16_t read_u16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(p[0]) |
        (static_cast<std::uint16_t>(p[1]) << 8));
}

inline std::uint32_t read_u32_le(const std::uint8_t* p) {
    return  static_cast<std::uint32_t>(p[0])        |
           (static_cast<std::uint32_t>(p[1]) <<  8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

inline void write_u32_le(std::uint32_t v, std::vector<std::uint8_t>& out) {
    out.push_back(static_cast<std::uint8_t>( v        & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

} // namespace

void Server::session_loop(Socket s) {
    // studio.web.0.4 — register an entry in the live sessions
    // registry so the Studio "Sessions" tab can list this peer.
    SessionInfo init;
    if (auto pa = socket_peer_addr(s); pa) {
        init.peer_ip   = pa.value().ip;
        init.peer_port = pa.value().port;
    }
    init.connected_at  = std::chrono::system_clock::now();
    init.last_activity = init.connected_at;
    std::uint64_t sid = this->register_session(init);
    this->install_session_socket(sid, s);
    struct SessionGuard {
        Server* srv; std::uint64_t id;
        ~SessionGuard() {
            if (srv) {
                srv->erase_session_socket(id);
                srv->unregister_session(id);
            }
        }
    } sg{this, sid};

    // M12.4 — per-session state. Connection is opened by the
    // Connect frame; OpenTable allocates a session-scoped 32-bit
    // table id keyed into engine handles.
    //
    // M12.7 — SQL exec opens a parallel ABI connection on first use
    // (AdsConnect60(local) → AdsCreateSQLStatement). Cursors returned
    // by AdsExecuteSQLDirect are wrapped in cursor_tbls and routed
    // back to the client through the M12.4 read ops; the read-side
    // dispatch checks cursor_tbls first so the same wire opcodes
    // (GotoTop / Skip / GetField / GetRecordCount / AtEOF /
    // CloseTable) work for either an OpenTable handle or a SELECT
    // result cursor.
    std::unique_ptr<openads::session::Connection> sess_conn;
    std::unordered_map<std::uint32_t, openads::session::Handle> tbls;
    std::unordered_map<std::uint32_t, ADSHANDLE>                cursor_tbls;
    // M12.21 option C — set from the Connect payload's capability word.
    // Gates the sequential-prefetch lookahead block: only a client that
    // advertised kCapPrefetchConsume (and therefore does the consumed-
    // counter resync) gets lookahead rows; everyone else keeps the
    // one-round-trip-per-Skip behavior, so no version mix desyncs.
    bool client_prefetch_ok = false;
    // M12.16 — lazy-promoted ABI handle parallel to tbls. Populated
    // on demand the first time the wire dispatch needs to call an
    // ABI-only function (index ops). Once present, every operation
    // on this client-side id routes through the ABI handle so the
    // engine and ABI cursor states never drift.
    std::unordered_map<std::uint32_t, ADSHANDLE>                tbls_h;
    // M12.16 — index_h[index_id] holds the ABI hIndex returned from
    // AdsOpenIndex. AdsCloseIndex removes the entry. Lifetime tied
    // to the session.
    std::unordered_map<std::uint32_t, ADSHANDLE>                index_h;
    // M12.16 — reverse map index_id -> table_id so Seek can sync
    // the engine cursor on the parent table after the ABI cursor
    // moves.
    std::unordered_map<std::uint32_t, std::uint32_t>            index_table;
    ADSHANDLE                                                   abi_conn = 0;
    ADSHANDLE                                                   abi_stmt = 0;
    std::uint32_t next_id = 1;

    auto cleanup = [&]() {
        for (auto& [id, h] : cursor_tbls) {
            (void)AdsCloseTable(h);
        }
        cursor_tbls.clear();
        for (auto& [id, h] : tbls_h) {
            (void)AdsCloseTable(h);
        }
        tbls_h.clear();
        index_h.clear();
        if (abi_stmt != 0) {
            (void)AdsCloseSQLStatement(abi_stmt);
            abi_stmt = 0;
        }
        if (abi_conn != 0) {
            (void)AdsDisconnect(abi_conn);
            abi_conn = 0;
        }
    };

    // M12.16 — lazy-init the per-session ABI connection (same one
    // M12.7 SQL exec uses).
    auto ensure_abi_conn = [&]() -> bool {
        if (abi_conn != 0) return true;
        if (!sess_conn) return false;
        // Prefer the full .add path so the ABI connection inherits the DD.
        const std::string& conn_path = sess_conn->dd_path().empty()
            ? sess_conn->data_dir() : sess_conn->dd_path();
        std::vector<UNSIGNED8> srvbuf(conn_path.size() + 1);
        std::memcpy(srvbuf.data(), conn_path.c_str(), conn_path.size() + 1);
        return AdsConnect60(srvbuf.data(), ADS_LOCAL_SERVER,
                            nullptr, nullptr, 0, &abi_conn) == 0;
    };

    // M12.16 — open a parallel ABI handle alongside the engine
    // handle in tbls[id], stash in tbls_h[id]. The engine handle
    // stays open so subsequent navigation handlers (GotoTop/Skip/…)
    // keep their existing path; only index operations route
    // through the ABI handle. After an index op that moves the
    // cursor (Seek / SeekLast) the helper sync_engine_cursor
    // re-positions the engine cursor to the same recno so the two
    // states never drift. SQL cursor handles (cursor_tbls) are
    // returned as-is since they are already ABI-side.
    auto ensure_abi_handle = [&](std::uint32_t id) -> ADSHANDLE {
        if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
            return cit->second;
        }
        if (auto hit = tbls_h.find(id); hit != tbls_h.end()) {
            return hit->second;
        }
        auto eit = tbls.find(id);
        if (eit == tbls.end() || !sess_conn) return 0;
        if (!ensure_abi_conn()) return 0;
        auto* tbl = sess_conn->lookup_table(eit->second);
        if (!tbl) return 0;
        std::filesystem::path p(tbl->path());
        std::string fname = p.filename().string();
        std::vector<UNSIGNED8> nb(fname.size() + 1);
        std::memcpy(nb.data(), fname.data(), fname.size());
        ADSHANDLE h = 0;
        if (AdsOpenTable(abi_conn, nb.data(), nullptr,
                         ADS_CDX, 0, 0, 0, 0, &h) != 0) {
            return 0;
        }
        tbls_h[id] = h;
        return h;
    };

    // M12.18 — pack the current record (recno + deleted + per-field
    // value bytes) onto the tail of any nav-op ack so the client
    // populates RemoteTable's row cache in the same RTT as the nav
    // itself. Format:
    //   [u8 has_row]
    //   if has_row:
    //     [u32 recno][u8 deleted][u16 nfields][per-field: u32 len, bytes]
    // M12.21 — when called from Skip(step==1), the trailer is
    // followed by an extra lookahead block:
    //   [u16 lookahead_count]
    //   for i in 0..lookahead_count:
    //     [u32 recno][u8 deleted][u16 nfields][per-field: u32 len, bytes]
    // The server walks Skip(+1) lookahead_count times capturing
    // rows, then Skip(-N) back so the cursor lands at the same
    // recno the lone Skip(+1) would have produced. xbrowse PgDn
    // (20 sequential Skip(1) calls) collapses to 1 RTT for the
    // whole repaint.
    //
    // Resolves through cursor_tbls / engine tbls / promoted tbls_h
    // in the same priority order the existing handlers use.

    // Pack one record's bytes into `dst` at the current cursor.
    // Returns false on EoF / unread error so the caller can stop
    // walking lookahead.
    auto pack_one_row_engine = [&](std::vector<std::uint8_t>& dst,
                                    openads::engine::Table* tbl) {
        if (!tbl || tbl->eof() || tbl->bof() || tbl->recno() == 0) {
            return false;
        }
        auto write_u32_p = [&](std::uint32_t v) {
            dst.push_back(static_cast<std::uint8_t>( v        & 0xFFu));
            dst.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFFu));
            dst.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
            dst.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
        };
        auto write_u16_p = [&](std::uint16_t v) {
            dst.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
            dst.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
        };
        write_u32_p(tbl->recno());
        dst.push_back(tbl->is_deleted() ? 1 : 0);
        auto nf = static_cast<std::uint16_t>(tbl->field_count());
        write_u16_p(nf);
        for (std::uint16_t i = 0; i < nf; ++i) {
            auto v = tbl->read_field(i);
            std::string vv = v ? v.value().as_string : std::string();
            write_u32_p(static_cast<std::uint32_t>(vv.size()));
            dst.insert(dst.end(), vv.begin(), vv.end());
        }
        return true;
    };
    auto pack_one_row_abi = [&](std::vector<std::uint8_t>& dst,
                                 ADSHANDLE h_abi) {
        UNSIGNED16 atend = 0;
        AdsAtEOF(h_abi, &atend);
        if (atend) return false;
        auto write_u32_p = [&](std::uint32_t v) {
            dst.push_back(static_cast<std::uint8_t>( v        & 0xFFu));
            dst.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFFu));
            dst.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
            dst.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
        };
        auto write_u16_p = [&](std::uint16_t v) {
            dst.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
            dst.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
        };
        UNSIGNED32 rn = 0;
        AdsGetRecordNum(h_abi, 0, &rn);
        write_u32_p(rn);
        UNSIGNED16 del = 0;
        AdsIsRecordDeleted(h_abi, &del);
        dst.push_back(del != 0 ? 1 : 0);
        UNSIGNED16 nf = 0;
        AdsGetNumFields(h_abi, &nf);
        write_u16_p(nf);
        for (UNSIGNED16 i = 1; i <= nf; ++i) {
            UNSIGNED8  nm[64] = {0};
            UNSIGNED16 cap = sizeof(nm);
            AdsGetFieldName(h_abi, i, nm, &cap);
            std::vector<UNSIGNED8> nbuf(cap + 1, 0);
            std::memcpy(nbuf.data(), nm, cap);
            UNSIGNED16 ftype = 0;
            AdsGetFieldType(h_abi, nbuf.data(), &ftype);
            bool is_memo = (ftype == ADS_MEMO ||
                            ftype == ADS_BINARY ||
                            ftype == ADS_IMAGE);
            UNSIGNED32 vcap = 4096;
            if (is_memo) {
                UNSIGNED32 mlen = 0;
                if (AdsGetMemoLength(h_abi, nbuf.data(), &mlen) != 0) mlen = 0;
                vcap = mlen + 1;
            }
            std::vector<UNSIGNED8> vbuf(vcap, 0);
            if (AdsGetField(h_abi, nbuf.data(), vbuf.data(), &vcap, 0) != 0) {
                vcap = 0;
            }
            write_u32_p(vcap);
            if (vcap > 0) {
                dst.insert(dst.end(), vbuf.data(), vbuf.data() + vcap);
            }
        }
        return true;
    };

    auto pack_row_trailer = [&](Frame& reply, std::uint32_t id,
                                 std::uint16_t lookahead_n = 0) {
        auto write_u16_p = [&](std::uint16_t v,
                                std::vector<std::uint8_t>& dst) {
            dst.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
            dst.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
        };
        // Resolve the table once; remember which path applies for
        // both the current row and the lookahead block below.
        openads::engine::Table* eng_tbl = nullptr;
        ADSHANDLE               h_abi   = 0;
        if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
            h_abi = cit->second;
        } else if (auto eit = tbls.find(id); eit != tbls.end() && sess_conn) {
            eng_tbl = sess_conn->lookup_table(eit->second);
        } else if (auto hit = tbls_h.find(id); hit != tbls_h.end()) {
            h_abi = hit->second;
        }
        if (!eng_tbl && h_abi == 0) {
            reply.payload.push_back(0);
            return;
        }
        // Pack the current row.
        bool current_packed = false;
        if (eng_tbl) {
            if (eng_tbl->eof() || eng_tbl->bof() || eng_tbl->recno() == 0) {
                reply.payload.push_back(0);
            } else {
                reply.payload.push_back(1);
                std::vector<std::uint8_t> tmp;
                if (pack_one_row_engine(tmp, eng_tbl)) {
                    reply.payload.insert(reply.payload.end(),
                        tmp.begin(), tmp.end());
                    current_packed = true;
                }
            }
        } else {
            UNSIGNED16 atend = 0;
            AdsAtEOF(h_abi, &atend);
            if (atend) {
                reply.payload.push_back(0);
            } else {
                reply.payload.push_back(1);
                std::vector<std::uint8_t> tmp;
                if (pack_one_row_abi(tmp, h_abi)) {
                    reply.payload.insert(reply.payload.end(),
                        tmp.begin(), tmp.end());
                    current_packed = true;
                }
            }
        }
        // M12.21 lookahead block. Walk Skip(+1) up to lookahead_n
        // times capturing each row, then Skip(-N) back so the
        // cursor lands at the same spot the lone Skip(+1) the
        // caller actually issued would have produced.
        if (!current_packed || lookahead_n == 0) {
            // No lookahead either way — emit a count of 0 so the
            // wire format always carries the field. Old clients
            // (M12.18) ignore the extra bytes.
            write_u16_p(0, reply.payload);
            return;
        }
        std::vector<std::vector<std::uint8_t>> rows;
        rows.reserve(lookahead_n);
        std::uint16_t taken = 0;
        // Track cursor moves separately from rows packed: a Skip(+1)
        // that lands on EoF still moves the cursor, even though no
        // row gets packed. The restore step at the end has to undo
        // every cursor advance, packed-row or not, otherwise the
        // caller-visible cursor lands a row past where the lone
        // Skip(+1) it issued would have produced.
        int cursor_advance = 0;
        for (std::uint16_t i = 0; i < lookahead_n; ++i) {
            if (eng_tbl) {
                auto sk = eng_tbl->skip(1);
                if (!sk) break;
                ++cursor_advance;
                std::vector<std::uint8_t> row;
                if (!pack_one_row_engine(row, eng_tbl)) break;
                rows.push_back(std::move(row));
                ++taken;
            } else {
                if (AdsSkip(h_abi, 1) != 0) break;
                ++cursor_advance;
                UNSIGNED16 atend = 0;
                AdsAtEOF(h_abi, &atend);
                if (atend) break;
                std::vector<std::uint8_t> row;
                if (!pack_one_row_abi(row, h_abi)) break;
                rows.push_back(std::move(row));
                ++taken;
            }
        }
        if (cursor_advance > 0) {
            if (eng_tbl) {
                (void)eng_tbl->skip(-cursor_advance);
            } else {
                (void)AdsSkip(h_abi,
                    -static_cast<SIGNED32>(cursor_advance));
            }
        }
        write_u16_p(taken, reply.payload);
        for (auto& r : rows) {
            reply.payload.insert(reply.payload.end(), r.begin(), r.end());
        }
    };

    // M12.16 — re-position the engine cursor to match the ABI
    // cursor after an index op that moves it (Seek / SeekLast).
    // No-op when the table is a cursor or has no engine handle.
    auto sync_engine_cursor = [&](std::uint32_t id) {
        if (cursor_tbls.count(id)) return;
        auto eit = tbls.find(id);
        if (eit == tbls.end() || !sess_conn) return;
        auto* tbl = sess_conn->lookup_table(eit->second);
        if (!tbl) return;
        ADSHANDLE h = 0;
        if (auto hit = tbls_h.find(id); hit != tbls_h.end()) {
            h = hit->second;
        } else { return; }
        UNSIGNED32 rn = 0;
        AdsGetRecordNum(h, 0, &rn);
        if (rn == 0) return;
        (void)tbl->goto_record(rn);
    };

    // M12.10 — Error frame payload:
    //   [u32 LE ace_code][message bytes]
    // Server-side handlers fold either a literal ACE code or a code
    // pulled from a sub-result's util::Error into every Error frame
    // they emit so the client can surface the right `Ads*` code.
    auto err = [](const std::string& msg,
                  UNSIGNED32 code = openads::AE_INTERNAL_ERROR) {
        Frame f;
        f.opcode = Opcode::Error;
        f.payload.resize(4);
        f.payload[0] = static_cast<std::uint8_t>( code        & 0xFFu);
        f.payload[1] = static_cast<std::uint8_t>((code >>  8) & 0xFFu);
        f.payload[2] = static_cast<std::uint8_t>((code >> 16) & 0xFFu);
        f.payload[3] = static_cast<std::uint8_t>((code >> 24) & 0xFFu);
        f.payload.insert(f.payload.end(), msg.begin(), msg.end());
        return f;
    };

    while (true) {
        auto fr = read_frame(s);
        if (!fr) break;                       // connection closed
        this->touch_session(sid, true, false);
        const Frame& f = fr.value();
        {
            // M9.25 — inbound comm telemetry. +5 accounts for the
            // 4-byte length prefix + 1-byte opcode of the wire framing.
            auto& mgst = openads::mgmt::process_mg_stats();
            mgst.packets_in.fetch_add(1, std::memory_order_relaxed);
            mgst.bytes_in.fetch_add(f.payload.size() + 5,
                                    std::memory_order_relaxed);
        }
        Frame reply;
        switch (f.opcode) {
            case Opcode::Hello: {
                reply.opcode = Opcode::HelloAck;
                std::string v = "openads/0.3.2";
                reply.payload.assign(v.begin(), v.end());
                break;
            }
            case Opcode::Connect: {
                // M12.9 — Connect payload format:
                //   [u16 dlen][dir][u16 ulen][user][u16 plen][password]
                // All three lengths are required; user/password may be
                // empty when the server doesn't require auth.
                const auto& pl = f.payload;
                std::string dir, user, pw;
                std::size_t p = 0;
                auto readlen = [&](std::uint16_t& out)->bool {
                    if (p + 2 > pl.size()) return false;
                    out = static_cast<std::uint16_t>(
                        static_cast<std::uint16_t>(pl[p]) |
                        (static_cast<std::uint16_t>(pl[p+1]) << 8));
                    p += 2; return true;
                };
                auto readstr = [&](std::string& out, std::uint16_t n)->bool {
                    if (p + n > pl.size()) return false;
                    out.assign(reinterpret_cast<const char*>(pl.data() + p), n);
                    p += n; return true;
                };
                std::uint16_t dl=0, ul=0, pwl=0;
                if (!readlen(dl) || !readstr(dir, dl) ||
                    !readlen(ul) || !readstr(user, ul) ||
                    !readlen(pwl) || !readstr(pw, pwl)) {
                    reply = err("Connect: bad payload");
                    break;
                }
                // M12.21 option C — optional trailing [u32 LE caps].
                // Absent for pre-M12.21 clients (p == pl.size()).
                if (p + 4 <= pl.size()) {
                    std::uint32_t caps =
                        static_cast<std::uint32_t>(pl[p]) |
                        (static_cast<std::uint32_t>(pl[p + 1]) <<  8) |
                        (static_cast<std::uint32_t>(pl[p + 2]) << 16) |
                        (static_cast<std::uint32_t>(pl[p + 3]) << 24);
                    client_prefetch_ok =
                        (caps & openads::network::kCapPrefetchConsume) != 0;
                }
                if (require_auth()) {
                    auto cit = creds_.find(user);
                    if (cit == creds_.end() || cit->second != pw) {
                        reply = err("Connect: authentication failed",
                                    openads::AE_LOGIN_FAILED);
                        break;
                    }
                }
                // Resolve relative client paths under the server's data root.
                std::string resolved = dir;
                if (!data_dir_.empty()) {
                    namespace fs = std::filesystem;
                    fs::path cp(dir);
                    if (cp.is_relative())
                        resolved = (fs::path(data_dir_) / cp).string();
                }
                auto co = openads::session::Connection::open(resolved);
                if (!co) {
                    reply = err("Connect: connection open failed",
                                static_cast<UNSIGNED32>(co.error().code));
                    break;
                }
                sess_conn = std::make_unique<openads::session::Connection>(
                    std::move(co).value());
                this->set_session_user(sid, user, dir);
                reply.opcode = Opcode::ConnectAck;
                std::string ackmsg = "connected:" + dir;
                reply.payload.assign(ackmsg.begin(), ackmsg.end());
                break;
            }
            case Opcode::Disconnect: {
                cleanup();
                sock_close(s);
                return;
            }
            case Opcode::OpenTable: {
                if (!sess_conn) {
                    reply = err("OpenTable: not connected",
                                openads::AE_NO_CONNECTION);
                    break;
                }
                std::string rel(reinterpret_cast<const char*>(
                                    f.payload.data()),
                                f.payload.size());
                auto th = sess_conn->open_table(rel,
                    openads::engine::TableType::Cdx,
                    openads::engine::OpenMode::Shared);
                if (!th) {
                    reply = err("OpenTable: open failed",
                                static_cast<UNSIGNED32>(th.error().code));
                    break;
                }
                std::uint32_t id = next_id++;
                tbls.emplace(id, th.value());
                this->add_session_table(sid, +1);
                reply.opcode = Opcode::OpenTableAck;
                write_u32_le(id, reply.payload);
                break;
            }
            case Opcode::CloseTable: {
                if (f.payload.size() < 4) { reply = err("CloseTable: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                auto cit = cursor_tbls.find(id);
                if (cit != cursor_tbls.end()) {
                    (void)AdsCloseTable(cit->second);
                    cursor_tbls.erase(cit);
                    this->add_session_table(sid, -1);
                    reply.opcode = Opcode::CloseTableAck;
                    break;
                }
                auto it = tbls.find(id);
                if (it != tbls.end()) {
                    sess_conn->close_table(it->second);
                    tbls.erase(it);
                    this->add_session_table(sid, -1);
                }
                reply.opcode = Opcode::CloseTableAck;
                break;
            }
            case Opcode::GotoTop: {
                if (f.payload.size() < 4) { reply = err("GotoTop: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    (void)AdsGotoTop(cit->second);
                    reply.opcode = Opcode::GotoTopAck;
                    pack_row_trailer(reply, id);
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("GotoTop: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("GotoTop: lookup failed"); break; }
                (void)tbl->goto_top();
                reply.opcode = Opcode::GotoTopAck;
                pack_row_trailer(reply, id);
                break;
            }
            case Opcode::Skip: {
                if (f.payload.size() < 8) { reply = err("Skip: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                std::int32_t step = static_cast<std::int32_t>(
                    read_u32_le(f.payload.data() + 4));
                // M12.21 — sequential Skip(1) is the xbrowse PgDn
                // pattern; piggyback up to 19 lookahead rows so the
                // remaining cells in the repaint hit the client cache.
                // M12.21 option C — re-enabled. A forward Skip from a
                // prefetch-capable client piggybacks up to K lookahead
                // rows; the client serves them locally and folds the
                // consumed count back into the next wire step, so the
                // server cursor never desyncs (the bug that shelved
                // option B). Non-capable clients and non-forward steps
                // get no lookahead, preserving the old behavior.
                constexpr std::uint16_t kPrefetchLookahead = 64;
                std::uint16_t lookahead =
                    (client_prefetch_ok && step >= 1) ? kPrefetchLookahead : 0;
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    (void)AdsSkip(cit->second, step);
                    reply.opcode = Opcode::SkipAck;
                    pack_row_trailer(reply, id, lookahead);
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("Skip: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("Skip: lookup failed"); break; }
                (void)tbl->skip(step);
                reply.opcode = Opcode::SkipAck;
                pack_row_trailer(reply, id, lookahead);
                break;
            }
            case Opcode::GetField: {
                if (f.payload.size() < 5) { reply = err("GetField: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                std::string fname(reinterpret_cast<const char*>(
                                      f.payload.data() + 4),
                                  f.payload.size() - 4);
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    UNSIGNED8  fbuf[64] = {0};
                    UNSIGNED8  out [4096] = {0};
                    UNSIGNED32 cap = sizeof(out);
                    std::size_t n = std::min<std::size_t>(fname.size(),
                                                          sizeof(fbuf) - 1);
                    std::memcpy(fbuf, fname.data(), n);
                    fbuf[n] = 0;
                    UNSIGNED32 rrc = AdsGetField(cit->second, fbuf, out, &cap, 0);
                    if (rrc != 0) { reply = err("GetField: cursor read failed"); break; }
                    reply.opcode = Opcode::GetFieldAck;
                    reply.payload.assign(out, out + cap);
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("GetField: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("GetField: lookup failed"); break; }
                std::int32_t fi = tbl->field_index(fname);
                if (fi < 0) { reply = err("GetField: column not found"); break; }
                auto v = tbl->read_field(static_cast<std::uint16_t>(fi));
                if (!v) { reply = err("GetField: read failed"); break; }
                reply.opcode = Opcode::GetFieldAck;
                auto& sval = v.value().as_string;
                reply.payload.assign(sval.begin(), sval.end());
                break;
            }
            case Opcode::GetRecordCount: {
                if (f.payload.size() < 4) { reply = err("GetRecordCount: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    UNSIGNED32 rc = 0;
                    AdsGetRecordCount(cit->second, 0, &rc);
                    reply.opcode = Opcode::GetRecordCountAck;
                    write_u32_le(rc, reply.payload);
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("GetRecordCount: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("GetRecordCount: lookup failed"); break; }
                std::uint32_t rc = tbl->record_count();
                reply.opcode = Opcode::GetRecordCountAck;
                write_u32_le(rc, reply.payload);
                break;
            }
            case Opcode::AtEOF: {
                if (f.payload.size() < 4) { reply = err("AtEOF: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    UNSIGNED16 v = 0;
                    AdsAtEOF(cit->second, &v);
                    reply.opcode = Opcode::AtEOFAck;
                    reply.payload.push_back(v != 0 ? 1 : 0);
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("AtEOF: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("AtEOF: lookup failed"); break; }
                reply.opcode = Opcode::AtEOFAck;
                reply.payload.push_back(tbl->eof() ? 1 : 0);
                break;
            }
            // M12.14 — DescribeTable: serialize the schema in one
            // round-trip so rddads' adsOpen field-iteration loop
            // doesn't generate 5 × num_fields hops.
            case Opcode::DescribeTable: {
                if (f.payload.size() < 4) {
                    reply = err("DescribeTable: bad payload"); break;
                }
                std::uint32_t id = read_u32_le(f.payload.data());
                ADSHANDLE       cur_h  = 0;
                openads::engine::Table* tbl = nullptr;
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    cur_h = cit->second;
                } else {
                    auto it = tbls.find(id);
                    if (it == tbls.end() || !sess_conn) {
                        reply = err("DescribeTable: bad table id"); break;
                    }
                    tbl = sess_conn->lookup_table(it->second);
                    if (!tbl) {
                        reply = err("DescribeTable: lookup failed"); break;
                    }
                }
                reply.opcode = Opcode::DescribeTableAck;
                if (cur_h != 0) {
                    UNSIGNED16 nf = 0;
                    AdsGetNumFields(cur_h, &nf);
                    reply.payload.push_back(static_cast<std::uint8_t>(nf & 0xFFu));
                    reply.payload.push_back(static_cast<std::uint8_t>((nf >> 8) & 0xFFu));
                    for (UNSIGNED16 i = 1; i <= nf; ++i) {
                        UNSIGNED8  nm[64] = {0};
                        UNSIGNED16 cap = sizeof(nm);
                        AdsGetFieldName(cur_h, i, nm, &cap);
                        std::vector<UNSIGNED8> nbuf(cap + 1, 0);
                        std::memcpy(nbuf.data(), nm, cap);
                        UNSIGNED16 ftype = 0;
                        UNSIGNED32 flen  = 0;
                        UNSIGNED16 fdec  = 0;
                        AdsGetFieldType    (cur_h, nbuf.data(), &ftype);
                        AdsGetFieldLength  (cur_h, nbuf.data(), &flen);
                        AdsGetFieldDecimals(cur_h, nbuf.data(), &fdec);
                        reply.payload.push_back(static_cast<std::uint8_t>(cap));
                        reply.payload.insert(reply.payload.end(),
                            nm, nm + cap);
                        reply.payload.push_back(static_cast<std::uint8_t>( ftype       & 0xFFu));
                        reply.payload.push_back(static_cast<std::uint8_t>((ftype >> 8) & 0xFFu));
                        write_u32_le(flen, reply.payload);
                        reply.payload.push_back(static_cast<std::uint8_t>( fdec       & 0xFFu));
                        reply.payload.push_back(static_cast<std::uint8_t>((fdec >> 8) & 0xFFu));
                    }
                } else {
                    // Mirror the ABI map_field_type() table so the wire
                    // payload reports ADS_* type codes (4 = STRING, 2 =
                    // NUMERIC, 11 = INTEGER, …) regardless of which
                    // server-side branch we took.
                    auto map_type = [](openads::drivers::DbfFieldType t) -> std::uint16_t {
                        using T = openads::drivers::DbfFieldType;
                        switch (t) {
                            case T::Character:    return ADS_STRING;
                            case T::Numeric:
                            case T::Float:        return ADS_NUMERIC;
                            case T::Logical:      return ADS_LOGICAL;
                            case T::Date:
                            case T::AdtDate:      return ADS_DATE;
                            case T::DateTime:
                            case T::AdtTimestamp: return ADS_TIMESTAMP;
                            case T::Memo:         return ADS_MEMO;
                            case T::Integer:
                            case T::ShortInt:
                            case T::AutoInc:      return ADS_INTEGER;
                            case T::Currency:
                            case T::AdtMoney:     return ADS_MONEY;
                            case T::Double:       return ADS_DOUBLE;
                            case T::Varchar:
                            case T::CiCharacter:  return ADS_STRING;
                            case T::Varbinary:
                            case T::Binary:       return ADS_RAW;
                            case T::Time:         return ADS_TIME;
                            case T::Unknown:
                            default:              return ADS_FIELD_TYPE_UNKNOWN;
                        }
                    };
                    auto nf = static_cast<std::uint16_t>(tbl->field_count());
                    reply.payload.push_back(static_cast<std::uint8_t>(nf & 0xFFu));
                    reply.payload.push_back(static_cast<std::uint8_t>((nf >> 8) & 0xFFu));
                    for (std::uint16_t i = 0; i < nf; ++i) {
                        const auto& fd = tbl->field_descriptor(i);
                        std::uint8_t name_len =
                            static_cast<std::uint8_t>(fd.name.size() & 0xFFu);
                        std::uint16_t ftype = map_type(fd.type);
                        std::uint32_t flen = fd.length;
                        std::uint16_t fdec = fd.decimals;
                        reply.payload.push_back(name_len);
                        reply.payload.insert(reply.payload.end(),
                            fd.name.begin(),
                            fd.name.begin() + name_len);
                        reply.payload.push_back(static_cast<std::uint8_t>( ftype       & 0xFFu));
                        reply.payload.push_back(static_cast<std::uint8_t>((ftype >> 8) & 0xFFu));
                        write_u32_le(flen, reply.payload);
                        reply.payload.push_back(static_cast<std::uint8_t>( fdec       & 0xFFu));
                        reply.payload.push_back(static_cast<std::uint8_t>((fdec >> 8) & 0xFFu));
                    }
                }
                break;
            }
            case Opcode::AtBOF: {
                if (f.payload.size() < 4) { reply = err("AtBOF: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    UNSIGNED16 v = 0;
                    AdsAtBOF(cit->second, &v);
                    reply.opcode = Opcode::AtBOFAck;
                    reply.payload.push_back(v != 0 ? 1 : 0);
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("AtBOF: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("AtBOF: lookup failed"); break; }
                reply.opcode = Opcode::AtBOFAck;
                reply.payload.push_back(tbl->bof() ? 1 : 0);
                break;
            }
            case Opcode::GetRecordNum: {
                if (f.payload.size() < 4) { reply = err("GetRecordNum: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    UNSIGNED32 rn = 0;
                    AdsGetRecordNum(cit->second, 0, &rn);
                    reply.opcode = Opcode::GetRecordNumAck;
                    write_u32_le(rn, reply.payload);
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("GetRecordNum: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("GetRecordNum: lookup failed"); break; }
                reply.opcode = Opcode::GetRecordNumAck;
                write_u32_le(tbl->recno(), reply.payload);
                break;
            }
            case Opcode::IsRecordDeleted: {
                if (f.payload.size() < 4) { reply = err("IsRecordDeleted: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    UNSIGNED16 v = 0;
                    AdsIsRecordDeleted(cit->second, &v);
                    reply.opcode = Opcode::IsRecordDeletedAck;
                    reply.payload.push_back(v != 0 ? 1 : 0);
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("IsRecordDeleted: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("IsRecordDeleted: lookup failed"); break; }
                reply.opcode = Opcode::IsRecordDeletedAck;
                reply.payload.push_back(tbl->is_deleted() ? 1 : 0);
                break;
            }
            case Opcode::GotoBottom: {
                if (f.payload.size() < 4) { reply = err("GotoBottom: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    AdsGotoBottom(cit->second);
                    reply.opcode = Opcode::GotoBottomAck;
                    pack_row_trailer(reply, id);
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("GotoBottom: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("GotoBottom: lookup failed"); break; }
                auto rb = tbl->goto_bottom();
                if (!rb) {
                    reply = err("GotoBottom: " + rb.error().message);
                    break;
                }
                reply.opcode = Opcode::GotoBottomAck;
                pack_row_trailer(reply, id);
                break;
            }
            // M12.15 — info / lock / maintenance / AOF.
            //
            // All these handlers share the same shape:
            //   payload[0..3]  = table id (client-side)
            //   reply payload  = answer (or empty for void)
            // For the local-table branch (sess_conn-owned engine
            // Tables) the call lands on Table::* methods directly.
            // Cursor handles (from ExecuteSQL) route through the
            // matching ABI entry point, preserving the wire/ABI
            // symmetry.
            case Opcode::IsFound: {
                if (f.payload.size() < 4) { reply = err("IsFound: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    UNSIGNED16 v = 0;
                    AdsIsFound(cit->second, &v);
                    reply.opcode = Opcode::IsFoundAck;
                    reply.payload.push_back(v != 0 ? 1 : 0);
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("IsFound: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("IsFound: lookup failed"); break; }
                reply.opcode = Opcode::IsFoundAck;
                reply.payload.push_back(tbl->last_seek_found() ? 1 : 0);
                break;
            }
            case Opcode::RefreshRecord: {
                if (f.payload.size() < 4) { reply = err("RefreshRecord: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    AdsRefreshRecord(cit->second);
                    reply.opcode = Opcode::RefreshRecordAck;
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("RefreshRecord: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("RefreshRecord: lookup failed"); break; }
                // Force a re-load from disk by re-positioning to the
                // current recno.
                auto rb = tbl->goto_record(tbl->recno());
                if (!rb) { reply = err("RefreshRecord: " + rb.error().message); break; }
                reply.opcode = Opcode::RefreshRecordAck;
                break;
            }
            case Opcode::GetTableType: {
                if (f.payload.size() < 4) { reply = err("GetTableType: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    UNSIGNED16 v = 0;
                    AdsGetTableType(cit->second, &v);
                    reply.opcode = Opcode::GetTableTypeAck;
                    reply.payload.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
                    reply.payload.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("GetTableType: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("GetTableType: lookup failed"); break; }
                std::uint16_t v = ADS_CDX;
                std::filesystem::path p(tbl->path());
                std::string ext = p.extension().string();
                for (auto& c : ext) c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
                if      (ext == ".adt") v = ADS_ADT;
                reply.opcode = Opcode::GetTableTypeAck;
                reply.payload.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
                reply.payload.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
                break;
            }
            case Opcode::GetRecordLength: {
                if (f.payload.size() < 4) { reply = err("GetRecordLength: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    UNSIGNED32 v = 0;
                    AdsGetRecordLength(cit->second, &v);
                    reply.opcode = Opcode::GetRecordLengthAck;
                    write_u32_le(v, reply.payload);
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("GetRecordLength: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("GetRecordLength: lookup failed"); break; }
                std::uint32_t rl = tbl->driver()
                    ? tbl->driver()->record_length() : 0;
                reply.opcode = Opcode::GetRecordLengthAck;
                write_u32_le(rl, reply.payload);
                break;
            }
            case Opcode::GetNumIndexes: {
                if (f.payload.size() < 4) { reply = err("GetNumIndexes: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    UNSIGNED16 v = 0;
                    AdsGetNumIndexes(cit->second, &v);
                    reply.opcode = Opcode::GetNumIndexesAck;
                    reply.payload.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
                    reply.payload.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("GetNumIndexes: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("GetNumIndexes: lookup failed"); break; }
                std::uint16_t n = static_cast<std::uint16_t>(
                    tbl->all_indexes().size());
                reply.opcode = Opcode::GetNumIndexesAck;
                reply.payload.push_back(static_cast<std::uint8_t>( n       & 0xFFu));
                reply.payload.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFFu));
                break;
            }
            case Opcode::GetLastAutoinc: {
                if (f.payload.size() < 4) { reply = err("GetLastAutoinc: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                std::uint32_t v = 0;
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    AdsGetLastAutoinc(cit->second, &v);
                }
                // Local-table path: not exposed at engine level;
                // return 0. Same as the local AdsGetLastAutoinc
                // fallback when the column isn't autoinc.
                reply.opcode = Opcode::GetLastAutoincAck;
                write_u32_le(v, reply.payload);
                break;
            }
            case Opcode::GetLastTableUpdate: {
                if (f.payload.size() < 4) { reply = err("GetLastTableUpdate: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                std::uint32_t packed = 0;
                if (cursor_tbls.find(id) == cursor_tbls.end()) {
                    auto it = tbls.find(id);
                    if (it == tbls.end() || !sess_conn) {
                        reply = err("GetLastTableUpdate: bad table id"); break;
                    }
                    auto* tbl = sess_conn->lookup_table(it->second);
                    if (!tbl) { reply = err("GetLastTableUpdate: lookup failed"); break; }
                    if (tbl->driver()) {
                        std::uint8_t b[3] = {0, 0, 0};
                        auto got = tbl->driver()->file().read_at(1, b, sizeof(b));
                        if (got && got.value() >= sizeof(b)) {
                            packed = (static_cast<std::uint32_t>(1900 + b[0]) << 16) |
                                     (static_cast<std::uint32_t>(b[1]) << 8) |
                                      static_cast<std::uint32_t>(b[2]);
                        }
                    }
                }
                reply.opcode = Opcode::GetLastTableUpdateAck;
                write_u32_le(packed, reply.payload);
                break;
            }
            // Locking is currently no-op at the engine level for
            // our LocalServer fallback path; the cursor branch
            // routes through real ABI locks. Both ack with success
            // so rddads' shared-mode opens don't fail mid-flight.
            case Opcode::LockRecord:
            case Opcode::UnlockRecord: {
                if (f.payload.size() < 8) { reply = err("Lock: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                std::uint32_t rn = read_u32_le(f.payload.data() + 4);
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    if (f.opcode == Opcode::LockRecord) {
                        AdsLockRecord(cit->second, rn);
                    } else {
                        AdsUnlockRecord(cit->second, rn);
                    }
                }
                reply.opcode = (f.opcode == Opcode::LockRecord)
                    ? Opcode::LockRecordAck
                    : Opcode::UnlockRecordAck;
                break;
            }
            case Opcode::LockTable:
            case Opcode::UnlockTable: {
                if (f.payload.size() < 4) { reply = err("Lock: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    if (f.opcode == Opcode::LockTable) {
                        AdsLockTable(cit->second);
                    } else {
                        AdsUnlockTable(cit->second);
                    }
                }
                reply.opcode = (f.opcode == Opcode::LockTable)
                    ? Opcode::LockTableAck
                    : Opcode::UnlockTableAck;
                break;
            }
            case Opcode::PackTable:
            case Opcode::ZapTable:
            case Opcode::FlushFileBuffers:
            case Opcode::CloseAllIndexes: {
                if (f.payload.size() < 4) { reply = err("Maintenance: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                Opcode ack_op =
                      (f.opcode == Opcode::PackTable)        ? Opcode::PackTableAck
                    : (f.opcode == Opcode::ZapTable)         ? Opcode::ZapTableAck
                    : (f.opcode == Opcode::FlushFileBuffers) ? Opcode::FlushFileBuffersAck
                    :                                          Opcode::CloseAllIndexesAck;
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    if      (f.opcode == Opcode::PackTable)        AdsPackTable(cit->second);
                    else if (f.opcode == Opcode::ZapTable)         AdsZapTable(cit->second);
                    else if (f.opcode == Opcode::FlushFileBuffers) AdsFlushFileBuffers(cit->second);
                    else                                            AdsCloseAllIndexes(cit->second);
                    reply.opcode = ack_op;
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("Maintenance: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("Maintenance: lookup failed"); break; }
                util::Result<void> rb = util::Result<void>{};
                if      (f.opcode == Opcode::PackTable)        rb = tbl->pack();
                else if (f.opcode == Opcode::ZapTable)         rb = tbl->zap();
                else if (f.opcode == Opcode::FlushFileBuffers) rb = tbl->flush();
                else {
                    // CloseAllIndexes: drop both active order +
                    // every parked extra view in lockstep.
                    tbl->clear_order();
                    tbl->clear_extra_index_views();
                }
                if (!rb) { reply = err("Maintenance: " + rb.error().message); break; }
                reply.opcode = ack_op;
                break;
            }
            case Opcode::SetAOF: {
                if (f.payload.size() < 4) { reply = err("SetAOF: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                std::string cond(reinterpret_cast<const char*>(
                                     f.payload.data() + 4),
                                 f.payload.size() - 4);
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    std::vector<UNSIGNED8> b(cond.size() + 1);
                    std::memcpy(b.data(), cond.data(), cond.size());
                    UNSIGNED32 rrc = AdsSetAOF(cit->second, b.data(), 0);
                    if (rrc != 0) { reply = err("SetAOF: parse failed", rrc); break; }
                    reply.opcode = Opcode::SetAOFAck;
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("SetAOF: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("SetAOF: lookup failed"); break; }
                auto ast = openads::engine::aof::parse(cond);
                if (!ast) {
                    // Expression outside the optimisable AOF subset
                    // (e.g. Empty(NAME)) — not an error. Drop any
                    // prior AOF and ack; the client RDD filters.
                    tbl->clear_filter();
                    reply.opcode = Opcode::SetAOFAck;
                    break;
                }
                auto rep = openads::engine::aof::evaluate_optimised(*ast.value(), *tbl);
                if (!rep) { reply = err("SetAOF: " + rep.error().message); break; }
                tbl->install_aof_bitmap(std::move(rep.value().bm));
                int lvl = ADS_OPTIMIZED_NONE;
                switch (rep.value().level) {
                    case openads::engine::aof::OptLevel::None: lvl = ADS_OPTIMIZED_NONE; break;
                    case openads::engine::aof::OptLevel::Part: lvl = ADS_OPTIMIZED_PART; break;
                    case openads::engine::aof::OptLevel::Full: lvl = ADS_OPTIMIZED_FULL; break;
                }
                tbl->set_aof_opt_level(lvl);
                reply.opcode = Opcode::SetAOFAck;
                break;
            }
            case Opcode::ClearAOFRemote: {
                if (f.payload.size() < 4) { reply = err("ClearAOF: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    AdsClearAOF(cit->second);
                    reply.opcode = Opcode::ClearAOFRemoteAck;
                    break;
                }
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("ClearAOF: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("ClearAOF: lookup failed"); break; }
                tbl->clear_filter();
                reply.opcode = Opcode::ClearAOFRemoteAck;
                break;
            }
            case Opcode::GetAOFOptLevel: {
                if (f.payload.size() < 4) { reply = err("GetAOFOptLevel: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                std::uint16_t v = ADS_OPTIMIZED_NONE;
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    UNSIGNED16 lvl = 0; UNSIGNED16 buflen = 0;
                    AdsGetAOFOptLevel(cit->second, &lvl, nullptr, &buflen);
                    v = lvl;
                } else if (auto it = tbls.find(id); it != tbls.end() && sess_conn) {
                    if (auto* tbl = sess_conn->lookup_table(it->second)) {
                        if (tbl->aof_active()) {
                            v = static_cast<std::uint16_t>(tbl->aof_opt_level());
                        }
                    }
                }
                reply.opcode = Opcode::GetAOFOptLevelAck;
                reply.payload.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
                reply.payload.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
                break;
            }
            // M12.16 — remote index handle subsystem. All ops route
            // through the ABI handle on tbls_h (lazy-promoted via
            // ensure_abi_handle) so AdsOpenIndex / AdsSetOrder /
            // AdsSeek work end-to-end over the wire.
            case Opcode::OpenIndex: {
                if (f.payload.size() < 4) { reply = err("OpenIndex: bad payload"); break; }
                std::uint32_t tid = read_u32_le(f.payload.data());
                std::string path(reinterpret_cast<const char*>(
                                     f.payload.data() + 4),
                                 f.payload.size() - 4);
                ADSHANDLE ht = ensure_abi_handle(tid);
                if (ht == 0) { reply = err("OpenIndex: bad table id"); break; }
                std::vector<UNSIGNED8> pb(path.size() + 1);
                std::memcpy(pb.data(), path.data(), path.size());
                ADSHANDLE arr[64] = {0};
                UNSIGNED16 alen = 64;
                UNSIGNED32 rrc = AdsOpenIndex(ht, pb.data(), arr, &alen);
                if (rrc != 0) { reply = err("OpenIndex: " + path, rrc); break; }
                reply.opcode = Opcode::OpenIndexAck;
                reply.payload.push_back(static_cast<std::uint8_t>( alen       & 0xFFu));
                reply.payload.push_back(static_cast<std::uint8_t>((alen >> 8) & 0xFFu));
                for (std::uint16_t i = 0; i < alen; ++i) {
                    std::uint32_t iid = next_id++;
                    index_h[iid] = arr[i];
                    index_table[iid] = tid;
                    write_u32_le(iid, reply.payload);
                }
                break;
            }
            case Opcode::CloseIndex: {
                if (f.payload.size() < 4) { reply = err("CloseIndex: bad payload"); break; }
                std::uint32_t iid = read_u32_le(f.payload.data());
                auto iit = index_h.find(iid);
                if (iit != index_h.end()) {
                    AdsCloseIndex(iit->second);
                    index_h.erase(iit);
                }
                index_table.erase(iid);
                reply.opcode = Opcode::CloseIndexAck;
                break;
            }
            case Opcode::SetOrder: {
                if (f.payload.size() < 8) { reply = err("SetOrder: bad payload"); break; }
                std::uint32_t tid = read_u32_le(f.payload.data());
                std::uint32_t iid = read_u32_le(f.payload.data() + 4);
                ADSHANDLE ht = ensure_abi_handle(tid);
                if (ht == 0) { reply = err("SetOrder: bad table id"); break; }
                auto iit = index_h.find(iid);
                if (iit == index_h.end()) { reply = err("SetOrder: bad index id"); break; }
                UNSIGNED32 rrc = AdsSetIndexOrderByHandle(ht, iit->second);
                if (rrc != 0) { reply = err("SetOrder", rrc); break; }
                sync_engine_cursor(tid);
                reply.opcode = Opcode::SetOrderAck;
                break;
            }
            case Opcode::SetOrderByName: {
                if (f.payload.size() < 4) { reply = err("SetOrderByName: bad payload"); break; }
                std::uint32_t tid = read_u32_le(f.payload.data());
                std::string tag(reinterpret_cast<const char*>(
                                    f.payload.data() + 4),
                                f.payload.size() - 4);
                ADSHANDLE ht = ensure_abi_handle(tid);
                if (ht == 0) { reply = err("SetOrderByName: bad table id"); break; }
                std::vector<UNSIGNED8> tb(tag.size() + 1);
                std::memcpy(tb.data(), tag.data(), tag.size());
                UNSIGNED32 rrc = AdsSetIndexOrder(ht,
                    tag.empty() ? nullptr : tb.data());
                if (rrc != 0) { reply = err("SetOrderByName: " + tag, rrc); break; }
                sync_engine_cursor(tid);
                reply.opcode = Opcode::SetOrderByNameAck;
                break;
            }
            case Opcode::Seek:
            case Opcode::SeekLast: {
                // payload: u32 index_id, u8 soft, key bytes.
                if (f.payload.size() < 5) { reply = err("Seek: bad payload"); break; }
                std::uint32_t iid  = read_u32_le(f.payload.data());
                std::uint8_t  soft = f.payload[4];
                std::string key(reinterpret_cast<const char*>(
                                    f.payload.data() + 5),
                                f.payload.size() - 5);
                auto iit = index_h.find(iid);
                if (iit == index_h.end()) { reply = err("Seek: bad index id"); break; }
                std::vector<UNSIGNED8> kb(key.size() + 1);
                std::memcpy(kb.data(), key.data(), key.size());
                UNSIGNED16 found = 0;
                UNSIGNED32 rrc = (f.opcode == Opcode::SeekLast)
                    ? AdsSeekLast(iit->second, kb.data(),
                                  static_cast<UNSIGNED16>(key.size()),
                                  ADS_STRINGKEY,
                                  &found)
                    : AdsSeek    (iit->second, kb.data(),
                                  static_cast<UNSIGNED16>(key.size()),
                                  ADS_STRINGKEY,
                                  static_cast<UNSIGNED16>(soft),
                                  &found);
                if (rrc != 0) { reply = err("Seek", rrc); break; }
                // Read recno via the parent table's ABI handle; that
                // also lets sync_engine_cursor reflect the new
                // position on the engine cursor we keep alive
                // alongside.
                UNSIGNED32 rn = 0;
                std::uint32_t parent_tid = 0;
                if (auto tit = index_table.find(iid); tit != index_table.end()) {
                    parent_tid = tit->second;
                    if (ADSHANDLE ht = ensure_abi_handle(parent_tid); ht != 0) {
                        AdsGetRecordNum(ht, 0, &rn);
                    }
                }
                if (parent_tid != 0) sync_engine_cursor(parent_tid);
                reply.opcode = (f.opcode == Opcode::SeekLast)
                    ? Opcode::SeekLastAck : Opcode::SeekAck;
                reply.payload.push_back(static_cast<std::uint8_t>(found != 0 ? 1 : 0));
                write_u32_le(rn, reply.payload);
                break;
            }
            // CreateIndex / SkipUnique / SetScope / ClearScope —
            // remote bridges for the remaining index ops. CreateIndex
            // takes the full AdsCreateIndex61 input and returns a
            // single index id (multi-tag CDX additions are supported
            // via repeated calls). SkipUnique walks distinct keys via
            // the active order; SetScope / ClearScope manage top /
            // bottom range bounds on an existing hIndex.
            case Opcode::CreateIndex: {
                if (f.payload.size() < 4 + 4 + 2) {
                    reply = err("CreateIndex: bad payload"); break;
                }
                std::size_t pos = 0;
                std::uint32_t tid     = read_u32_le(f.payload.data() + pos); pos += 4;
                std::uint32_t options = read_u32_le(f.payload.data() + pos); pos += 4;
                std::uint16_t pgsize  = read_u16_le(f.payload.data() + pos); pos += 2;
                auto pop_str = [&](std::string& out) -> bool {
                    if (pos + 2 > f.payload.size()) return false;
                    std::uint16_t n = read_u16_le(f.payload.data() + pos);
                    pos += 2;
                    if (pos + n > f.payload.size()) return false;
                    out.assign(reinterpret_cast<const char*>(
                                   f.payload.data() + pos), n);
                    pos += n;
                    return true;
                };
                std::string path, tag, expr, cond, key_filter;
                if (!pop_str(path) || !pop_str(tag) || !pop_str(expr) ||
                    !pop_str(cond) || !pop_str(key_filter)) {
                    reply = err("CreateIndex: short payload"); break;
                }
                ADSHANDLE ht = ensure_abi_handle(tid);
                if (ht == 0) { reply = err("CreateIndex: bad table id"); break; }
                std::vector<UNSIGNED8> pb (path .size() + 1);  std::memcpy(pb .data(), path .data(), path .size());
                std::vector<UNSIGNED8> tb (tag  .size() + 1);  std::memcpy(tb .data(), tag  .data(), tag  .size());
                std::vector<UNSIGNED8> eb (expr .size() + 1);  std::memcpy(eb .data(), expr .data(), expr .size());
                std::vector<UNSIGNED8> cb (cond .size() + 1);  std::memcpy(cb .data(), cond .data(), cond .size());
                std::vector<UNSIGNED8> kfb(key_filter.size() + 1);
                std::memcpy(kfb.data(), key_filter.data(), key_filter.size());
                ADSHANDLE hidx = 0;
                UNSIGNED32 rrc = AdsCreateIndex61(
                    ht, pb.data(), tb.data(), eb.data(),
                    cond.empty() ? nullptr : cb.data(),
                    key_filter.empty() ? nullptr : kfb.data(),
                    options, pgsize, &hidx);
                if (rrc != 0) { reply = err("CreateIndex", rrc); break; }
                std::uint32_t iid = next_id++;
                index_h[iid]     = hidx;
                index_table[iid] = tid;
                reply.opcode = Opcode::CreateIndexAck;
                write_u32_le(iid, reply.payload);
                break;
            }
            case Opcode::SkipUnique: {
                if (f.payload.size() < 8) { reply = err("SkipUnique: bad payload"); break; }
                std::uint32_t iid = read_u32_le(f.payload.data());
                std::int32_t  dir = static_cast<std::int32_t>(
                    read_u32_le(f.payload.data() + 4));
                auto iit = index_h.find(iid);
                if (iit == index_h.end()) { reply = err("SkipUnique: bad index id"); break; }
                UNSIGNED32 rrc = AdsSkipUnique(iit->second, dir);
                if (rrc != 0) { reply = err("SkipUnique", rrc); break; }
                if (auto tit = index_table.find(iid); tit != index_table.end()) {
                    sync_engine_cursor(tit->second);
                }
                reply.opcode = Opcode::SkipUniqueAck;
                break;
            }
            case Opcode::SetScope: {
                // Payload: u32 index_id | u16 which | u16 data_type | bytes key.
                if (f.payload.size() < 8) { reply = err("SetScope: bad payload"); break; }
                std::uint32_t iid    = read_u32_le(f.payload.data());
                std::uint16_t which  = read_u16_le(f.payload.data() + 4);
                std::uint16_t dtype  = read_u16_le(f.payload.data() + 6);
                std::size_t   klen   = f.payload.size() - 8;
                auto iit = index_h.find(iid);
                if (iit == index_h.end()) { reply = err("SetScope: bad index id"); break; }
                std::vector<UNSIGNED8> kb(klen + 1);
                if (klen > 0) std::memcpy(kb.data(), f.payload.data() + 8, klen);
                UNSIGNED32 rrc = AdsSetScope(
                    iit->second, which, kb.data(),
                    static_cast<UNSIGNED16>(klen), dtype);
                if (rrc != 0) { reply = err("SetScope", rrc); break; }
                reply.opcode = Opcode::SetScopeAck;
                break;
            }
            case Opcode::ClearScope: {
                if (f.payload.size() < 6) { reply = err("ClearScope: bad payload"); break; }
                std::uint32_t iid   = read_u32_le(f.payload.data());
                std::uint16_t which = read_u16_le(f.payload.data() + 4);
                auto iit = index_h.find(iid);
                if (iit == index_h.end()) { reply = err("ClearScope: bad index id"); break; }
                UNSIGNED32 rrc = AdsClearScope(iit->second, which);
                if (rrc != 0) { reply = err("ClearScope", rrc); break; }
                reply.opcode = Opcode::ClearScopeAck;
                break;
            }
            // M12.17 — single-frame whole-record read. The server
            // walks every column once with AdsGetField against a
            // suitably-sized buffer (memo lengths queried up-front)
            // and packs each value into the ack so the client can
            // serve subsequent FieldGet calls from cache without
            // another RTT per cell.
            case Opcode::FetchCurrentRow: {
                if (f.payload.size() < 4) { reply = err("FetchCurrentRow: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                reply.opcode = Opcode::FetchCurrentRowAck;
                pack_row_trailer(reply, id);
                break;
            }
            // M12.7 — remote SQL exec. Lazy-creates a parallel ABI
            // connection on the server side; cursor handles returned
            // by AdsExecuteSQLDirect get wrapped in cursor_tbls so the
            // existing read-side ops can serve them through the same
            // wire opcodes.
            case Opcode::ExecuteSQL: {
                if (!sess_conn) { reply = err("ExecuteSQL: not connected"); break; }
                if (abi_conn == 0) {
                    if (!ensure_abi_conn()) {
                        reply = err("ExecuteSQL: AdsConnect60 failed");
                        break;
                    }
                    if (AdsCreateSQLStatement(abi_conn, &abi_stmt) != 0) {
                        reply = err("ExecuteSQL: AdsCreateSQLStatement failed");
                        break;
                    }
                }
                std::vector<UNSIGNED8> sqlbuf(f.payload.size() + 1);
                if (!f.payload.empty()) {
                    std::memcpy(sqlbuf.data(), f.payload.data(),
                                f.payload.size());
                }
                sqlbuf[f.payload.size()] = 0;
                ADSHANDLE hCur = 0;
                UNSIGNED32 rrc = AdsExecuteSQLDirect(abi_stmt,
                                                     sqlbuf.data(), &hCur);
                if (rrc != 0) {
                    reply = err("ExecuteSQL: server-side exec failed", rrc);
                    break;
                }
                std::uint32_t id = 0;
                if (hCur != 0) {
                    id = next_id++;
                    cursor_tbls.emplace(id, hCur);
                }
                reply.opcode = Opcode::ExecuteSQLAck;
                write_u32_le(id, reply.payload);
                break;
            }
            case Opcode::AppendBlank: {
                if (f.payload.size() < 4) { reply = err("AppendBlank: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("AppendBlank: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("AppendBlank: lookup failed"); break; }
                auto r = tbl->append_record();
                if (!r) { reply = err("AppendBlank: append_record failed"); break; }
                reply.opcode = Opcode::AppendBlankAck;
                break;
            }
            case Opcode::SetField: {
                // payload: [u32 tid][u16 name_len][name bytes][value bytes...]
                if (f.payload.size() < 6) { reply = err("SetField: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                std::uint16_t nlen = static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(f.payload[4]) |
                    (static_cast<std::uint16_t>(f.payload[5]) << 8));
                if (f.payload.size() < 6u + nlen) {
                    reply = err("SetField: truncated"); break;
                }
                std::string fname(reinterpret_cast<const char*>(
                                      f.payload.data() + 6),
                                  nlen);
                std::string val(reinterpret_cast<const char*>(
                                    f.payload.data() + 6 + nlen),
                                f.payload.size() - 6 - nlen);
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("SetField: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("SetField: lookup failed"); break; }
                std::int32_t fi = tbl->field_index(fname);
                if (fi < 0) { reply = err("SetField: column not found"); break; }
                const auto& fdesc =
                    tbl->field_descriptor(static_cast<std::uint16_t>(fi));
                util::Result<void> r;
                auto fi_u = static_cast<std::uint16_t>(fi);
                switch (fdesc.type) {
                    case drivers::DbfFieldType::Logical: {
                        bool lv = !val.empty() &&
                            (val[0] == '1' || val[0] == 'T' || val[0] == 't' ||
                             val[0] == 'Y' || val[0] == 'y');
                        r = tbl->set_field(fi_u, lv);
                        break;
                    }
                    case drivers::DbfFieldType::Integer:
                    case drivers::DbfFieldType::AutoInc:
                    case drivers::DbfFieldType::Double:
                    case drivers::DbfFieldType::ShortInt:
                    case drivers::DbfFieldType::Currency:
                    case drivers::DbfFieldType::AdtMoney:
                    case drivers::DbfFieldType::Time:
                    case drivers::DbfFieldType::Numeric:
                        try {
                            r = tbl->set_field(fi_u, std::stod(val));
                        } catch (...) {
                            r = tbl->set_field(fi_u, val);
                        }
                        break;
                    default:
                        r = tbl->set_field(fi_u, val);
                        break;
                }
                if (!r) { reply = err("SetField: write failed"); break; }
                reply.opcode = Opcode::SetFieldAck;
                break;
            }
            case Opcode::DeleteRecord: {
                if (f.payload.size() < 4) { reply = err("DeleteRecord: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("DeleteRecord: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("DeleteRecord: lookup failed"); break; }
                auto r = tbl->mark_deleted();
                if (!r) { reply = err("DeleteRecord: mark_deleted failed"); break; }
                reply.opcode = Opcode::DeleteRecordAck;
                break;
            }
            case Opcode::RecallRecord: {
                if (f.payload.size() < 4) { reply = err("RecallRecord: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("RecallRecord: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("RecallRecord: lookup failed"); break; }
                auto r = tbl->recall_deleted();
                if (!r) { reply = err("RecallRecord: recall_deleted failed"); break; }
                reply.opcode = Opcode::RecallRecordAck;
                break;
            }
            case Opcode::GotoRecord: {
                if (f.payload.size() < 8) { reply = err("GotoRecord: bad payload"); break; }
                std::uint32_t id    = read_u32_le(f.payload.data());
                std::uint32_t recno = read_u32_le(f.payload.data() + 4);
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("GotoRecord: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("GotoRecord: lookup failed"); break; }
                auto r = tbl->goto_record(recno);
                if (!r) { reply = err("GotoRecord: failed"); break; }
                reply.opcode = Opcode::GotoRecordAck;
                pack_row_trailer(reply, id);
                break;
            }
            case Opcode::FlushTable: {
                if (f.payload.size() < 4) { reply = err("FlushTable: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("FlushTable: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("FlushTable: lookup failed"); break; }
                auto r = tbl->flush();
                if (!r) { reply = err("FlushTable: flush failed"); break; }
                reply.opcode = Opcode::FlushTableAck;
                break;
            }
            case Opcode::Fetch: {
                // M12.11 — payload:
                //   [u32 tid][u32 max_rows][u8 ncols][u8 nlen][name]...
                // Reply:
                //   [u32 nrows][u8 ncols][per row, per col: u16 vlen][val]
                if (f.payload.size() < 9) { reply = err("Fetch: bad payload"); break; }
                std::uint32_t id      = read_u32_le(f.payload.data());
                std::uint32_t maxrows = read_u32_le(f.payload.data() + 4);
                std::uint8_t  ncols   = f.payload[8];
                std::vector<std::string> cols;
                cols.reserve(ncols);
                std::size_t p = 9;
                bool parse_ok = true;
                for (std::uint8_t c = 0; c < ncols && parse_ok; ++c) {
                    if (p + 1 > f.payload.size()) {
                        reply = err("Fetch: truncated col header");
                        parse_ok = false; break;
                    }
                    std::uint8_t nlen = f.payload[p++];
                    if (p + nlen > f.payload.size()) {
                        reply = err("Fetch: truncated col name");
                        parse_ok = false; break;
                    }
                    cols.emplace_back(
                        reinterpret_cast<const char*>(f.payload.data() + p),
                        nlen);
                    p += nlen;
                }
                if (!parse_ok) break;

                auto write_u16_le = [](std::vector<std::uint8_t>& out,
                                       std::uint16_t v) {
                    out.push_back(static_cast<std::uint8_t>( v        & 0xFFu));
                    out.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFFu));
                };

                reply.opcode = Opcode::FetchAck;
                std::vector<std::uint8_t> rowbuf;
                std::uint32_t nrows_out = 0;

                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    ADSHANDLE  hCur  = cit->second;
                    UNSIGNED16 atend = 0;
                    AdsAtEOF(hCur, &atend);
                    while (atend == 0 && nrows_out < maxrows) {
                        for (auto& cn : cols) {
                            UNSIGNED8  fbuf[64]  = {0};
                            UNSIGNED8  out [4096] = {0};
                            UNSIGNED32 cap = sizeof(out);
                            std::size_t n = std::min<std::size_t>(
                                cn.size(), sizeof(fbuf) - 1);
                            std::memcpy(fbuf, cn.data(), n);
                            fbuf[n] = 0;
                            UNSIGNED32 rrc = AdsGetField(hCur, fbuf,
                                                         out, &cap, 0);
                            if (rrc != 0) cap = 0;
                            write_u16_le(rowbuf,
                                static_cast<std::uint16_t>(cap));
                            rowbuf.insert(rowbuf.end(), out, out + cap);
                        }
                        ++nrows_out;
                        if (AdsSkip(hCur, 1) != 0) break;
                        AdsAtEOF(hCur, &atend);
                    }
                } else if (auto it = tbls.find(id);
                           it != tbls.end() && sess_conn) {
                    auto* tbl = sess_conn->lookup_table(it->second);
                    if (!tbl) { reply = err("Fetch: lookup failed"); break; }
                    while (!tbl->eof() && nrows_out < maxrows) {
                        for (auto& cn : cols) {
                            std::int32_t fi = tbl->field_index(cn);
                            std::string val;
                            if (fi >= 0) {
                                auto v = tbl->read_field(
                                    static_cast<std::uint16_t>(fi));
                                if (v) val = v.value().as_string;
                            }
                            write_u16_le(rowbuf,
                                static_cast<std::uint16_t>(val.size()));
                            rowbuf.insert(rowbuf.end(),
                                          val.begin(), val.end());
                        }
                        ++nrows_out;
                        auto sk = tbl->skip(1);
                        if (!sk) break;
                    }
                } else {
                    reply = err("Fetch: bad table id"); break;
                }

                write_u32_le(nrows_out, reply.payload);
                reply.payload.push_back(ncols);
                reply.payload.insert(reply.payload.end(),
                                     rowbuf.begin(), rowbuf.end());
                break;
            }
            case Opcode::Reindex: {
                if (f.payload.size() < 4) { reply = err("Reindex: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                auto it = tbls.find(id);
                if (it == tbls.end() || !sess_conn) {
                    reply = err("Reindex: bad table id"); break;
                }
                auto* tbl = sess_conn->lookup_table(it->second);
                if (!tbl) { reply = err("Reindex: lookup failed"); break; }
                auto r = tbl->reindex();
                if (!r) { reply = err("Reindex: reindex failed"); break; }
                reply.opcode = Opcode::ReindexAck;
                break;
            }
            case Opcode::MgConnect: {
                // Management handshake — no payload needed; reply with
                // an ack so the client can register its mgmt handle.
                reply.opcode = Opcode::MgConnectAck;
                std::string ok = "mg-ok";
                reply.payload.assign(ok.begin(), ok.end());
                break;
            }
            case Opcode::MgRequest: {
                std::string reqbuf(
                    reinterpret_cast<const char*>(f.payload.data()),
                    f.payload.size());
                auto req = decode_mg_request(reqbuf);
                if (!req) {
                    reply = err("bad mg request");
                    break;
                }
                switch (req.value().kind) {
                    case MgRequestKind::Snapshot: {
                        reply.opcode = Opcode::MgReplyAck;
                        std::string snap =
                            encode_mg_snapshot(build_mg_snapshot());
                        reply.payload.assign(snap.begin(), snap.end());
                        break;
                    }
                    case MgRequestKind::KillUser: {
                        // arg is the 1-based connection number; map it
                        // to the matching session id and kill it.
                        kill_session_by_conn_no(req.value().arg);
                        reply.opcode = Opcode::MgReplyAck;
                        break;
                    }
                    case MgRequestKind::ResetCommStats: {
                        openads::mgmt::process_mg_stats().reset_comm();
                        reply.opcode = Opcode::MgReplyAck;
                        break;
                    }
                    case MgRequestKind::DumpTables: {
                        reply.opcode = Opcode::MgReplyAck;
                        break;
                    }
                    default:
                        reply = err("unknown mg request kind");
                        break;
                }
                break;
            }
            default: {
                reply = err("unsupported opcode");
                break;
            }
        }
        if (auto wr = write_frame(s, reply); !wr) break;
        // M9.25 — outbound comm telemetry.
        openads::mgmt::process_mg_stats()
            .packets_out.fetch_add(1, std::memory_order_relaxed);
        this->touch_session(sid, false, true);
    }
    cleanup();
    sock_close(s);
}

} // namespace openads::network
