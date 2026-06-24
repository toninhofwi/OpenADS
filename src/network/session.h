#pragma once

#include "network/server.h"
#include "network/socket.h"
#include "network/wire.h"
#include "openads/ace.h"
#include "session/connection.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace openads::engine { class Table; }

namespace openads::network {

// Result of dispatching one wire frame. `reply == std::nullopt` means
// "send nothing back" (used by Disconnect); `close_session` asks the
// driving loop to stop reading and tear the connection down.
struct DispatchResult {
    std::optional<Frame> reply;          // std::nullopt => send nothing
    bool                 close_session = false;
};

// SLICE 3a — all per-connection state and the opcode dispatch switch
// extracted verbatim out of Server::session_loop. The existing
// thread-per-connection loop constructs a Session per accepted socket
// and drives it through dispatch(). Zero behavior change.
class Session {
public:
    Session(Server& srv, Socket s);      // computes peer addr, register_session, install_session_socket
    ~Session();                          // cleanup(); srv_->erase_session_socket(sid_); srv_->unregister_session(sid_);
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    DispatchResult dispatch(const Frame& f);
    std::uint64_t  id() const noexcept { return sid_; }

private:
    Server*       srv_;
    Socket        s_;
    std::uint64_t sid_;

    // M12.4 — per-session state. Connection is opened by the
    // Connect frame; OpenTable allocates a session-scoped 32-bit
    // table id keyed into engine handles.
    std::unique_ptr<openads::session::Connection> sess_conn_;
    std::unordered_map<std::uint32_t, openads::session::Handle> tbls_;
    std::unordered_map<std::uint32_t, ADSHANDLE>                cursor_tbls_;
    // M12.16 — lazy-promoted ABI handle parallel to tbls_.
    std::unordered_map<std::uint32_t, ADSHANDLE>                tbls_h_;
    // M12.16 — index_h_[index_id] holds the ABI hIndex.
    std::unordered_map<std::uint32_t, ADSHANDLE>                index_h_;
    // M12.16 — reverse map index_id -> table_id.
    std::unordered_map<std::uint32_t, std::uint32_t>            index_table_;
    ADSHANDLE     abi_conn_ = 0;
    ADSHANDLE     abi_stmt_ = 0;
    std::uint32_t next_id_ = 1;
    // M12.21 option C — set from the Connect payload's capability word.
    bool          client_prefetch_ok_ = false;

    // Moved helpers (were [&] lambdas in session_loop) -> private
    // methods; bodies unchanged except member renames.
    void      cleanup();
    bool      ensure_abi_conn();
    ADSHANDLE ensure_abi_handle(std::uint32_t id);
    bool      pack_one_row_engine(std::vector<std::uint8_t>& dst,
                                  openads::engine::Table* tbl);
    bool      pack_one_row_abi(std::vector<std::uint8_t>& dst,
                               ADSHANDLE h_abi);
    void      pack_row_trailer(Frame& reply, std::uint32_t id,
                               std::uint16_t lookahead_n = 0);
    void      sync_engine_cursor(std::uint32_t id);
};

} // namespace openads::network
