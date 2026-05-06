#include "network/server.h"

#include "engine/table.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "session/connection.h"

#include <cstring>
#include <memory>
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

void Server::add_credential(const std::string& user,
                            const std::string& password) {
    creds_[user] = password;
}

bool Server::require_auth() const noexcept { return !creds_.empty(); }

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
    for (auto& t : sessions_) {
        if (t.joinable()) t.join();
    }
    sessions_.clear();
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
        std::lock_guard<std::mutex> lk(sessions_mu_);
        sessions_.emplace_back([this, s]() mutable {
            this->session_loop(s);
        });
    }
}

namespace {

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
    ADSHANDLE                                                   abi_conn = 0;
    ADSHANDLE                                                   abi_stmt = 0;
    std::uint32_t next_id = 1;

    auto cleanup = [&]() {
        for (auto& [id, h] : cursor_tbls) {
            (void)AdsCloseTable(h);
        }
        cursor_tbls.clear();
        if (abi_stmt != 0) {
            (void)AdsCloseSQLStatement(abi_stmt);
            abi_stmt = 0;
        }
        if (abi_conn != 0) {
            (void)AdsDisconnect(abi_conn);
            abi_conn = 0;
        }
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
                if (require_auth()) {
                    auto cit = creds_.find(user);
                    if (cit == creds_.end() || cit->second != pw) {
                        reply = err("Connect: authentication failed",
                                    openads::AE_LOGIN_FAILED);
                        break;
                    }
                }
                auto co = openads::session::Connection::open(dir);
                if (!co) {
                    reply = err("Connect: connection open failed",
                                static_cast<UNSIGNED32>(co.error().code));
                    break;
                }
                sess_conn = std::make_unique<openads::session::Connection>(
                    std::move(co).value());
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
                    reply.opcode = Opcode::CloseTableAck;
                    break;
                }
                auto it = tbls.find(id);
                if (it != tbls.end()) {
                    sess_conn->close_table(it->second);
                    tbls.erase(it);
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
                break;
            }
            case Opcode::Skip: {
                if (f.payload.size() < 8) { reply = err("Skip: bad payload"); break; }
                std::uint32_t id = read_u32_le(f.payload.data());
                std::int32_t step = static_cast<std::int32_t>(
                    read_u32_le(f.payload.data() + 4));
                if (auto cit = cursor_tbls.find(id); cit != cursor_tbls.end()) {
                    (void)AdsSkip(cit->second, step);
                    reply.opcode = Opcode::SkipAck;
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
            // M12.7 — remote SQL exec. Lazy-creates a parallel ABI
            // connection on the server side; cursor handles returned
            // by AdsExecuteSQLDirect get wrapped in cursor_tbls so the
            // existing read-side ops can serve them through the same
            // wire opcodes.
            case Opcode::ExecuteSQL: {
                if (!sess_conn) { reply = err("ExecuteSQL: not connected"); break; }
                if (abi_conn == 0) {
                    const std::string& dir = sess_conn->data_dir();
                    std::vector<UNSIGNED8> srvbuf(dir.size() + 1);
                    std::memcpy(srvbuf.data(), dir.c_str(), dir.size() + 1);
                    if (AdsConnect60(srvbuf.data(), ADS_LOCAL_SERVER,
                                     nullptr, nullptr, 0, &abi_conn) != 0) {
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
                auto r = tbl->set_field(static_cast<std::uint16_t>(fi), val);
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
            default: {
                reply = err("unsupported opcode");
                break;
            }
        }
        if (auto wr = write_frame(s, reply); !wr) break;
    }
    cleanup();
    sock_close(s);
}

} // namespace openads::network
