#include "network/client.h"

#include <cstring>

namespace openads::network {

namespace {

inline void write_u32_le(std::uint32_t v,
                         std::vector<std::uint8_t>& out) {
    out.push_back(static_cast<std::uint8_t>( v        & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

inline std::uint32_t read_u32_le(const std::uint8_t* p) {
    return  static_cast<std::uint32_t>(p[0])        |
           (static_cast<std::uint32_t>(p[1]) <<  8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

} // namespace

bool parse_tcp_uri(const std::string& uri,
                   std::string& host,
                   std::uint16_t& port,
                   std::string& data_dir) {
    const std::string kPrefix = "tcp://";
    if (uri.size() < kPrefix.size() ||
        uri.compare(0, kPrefix.size(), kPrefix) != 0) {
        return false;
    }
    std::size_t after = kPrefix.size();
    std::size_t slash = uri.find('/', after);
    std::string hostport = uri.substr(after,
        slash == std::string::npos ? std::string::npos : slash - after);
    std::size_t colon = hostport.find(':');
    if (colon == std::string::npos) return false;
    host = hostport.substr(0, colon);
    port = static_cast<std::uint16_t>(
        std::strtoul(hostport.substr(colon + 1).c_str(), nullptr, 10));
    data_dir = (slash == std::string::npos) ? std::string()
                                            : uri.substr(slash + 1);
    return true;
}

util::Result<Frame> RemoteConnection::request(const Frame& f) {
    std::lock_guard<std::mutex> lk(mu_);
    if (auto r = write_frame(sock_, f); !r) return r.error();
    return read_frame(sock_);
}

util::Result<void> RemoteConnection::connect(const std::string& host,
                                              std::uint16_t port,
                                              const std::string& data_dir) {
    auto s = connect_tcp(host, port);
    if (!s) return s.error();
    sock_ = s.value();
    Frame req;
    req.opcode = Opcode::Connect;
    req.payload.assign(data_dir.begin(), data_dir.end());
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::ConnectAck) {
        return util::Error{5000, 0, "Connect: server returned non-ack",
                           data_dir};
    }
    return {};
}

void RemoteConnection::disconnect() noexcept {
    if (!sock_.valid()) return;
    Frame req;
    req.opcode = Opcode::Disconnect;
    (void)write_frame(sock_, req);
    sock_close(sock_);
}

util::Result<std::uint32_t>
RemoteConnection::open_table(const std::string& rel) {
    Frame req;
    req.opcode = Opcode::OpenTable;
    req.payload.assign(rel.begin(), rel.end());
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::OpenTableAck) {
        return util::Error{5000, 0, "OpenTable: server error",
                           std::string(rep.value().payload.begin(),
                                       rep.value().payload.end())};
    }
    if (rep.value().payload.size() < 4) {
        return util::Error{5000, 0,
            "OpenTable: ack payload too short", ""};
    }
    return read_u32_le(rep.value().payload.data());
}

util::Result<void> RemoteConnection::close_table(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::CloseTable;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::CloseTableAck) {
        return util::Error{5000, 0, "CloseTable: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::goto_top(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::GotoTop;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GotoTopAck) {
        return util::Error{5000, 0, "GotoTop: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::skip(std::uint32_t id,
                                           std::int32_t step) {
    Frame req;
    req.opcode = Opcode::Skip;
    write_u32_le(id, req.payload);
    write_u32_le(static_cast<std::uint32_t>(step), req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::SkipAck) {
        return util::Error{5000, 0, "Skip: server error", ""};
    }
    return {};
}

util::Result<std::string>
RemoteConnection::get_field(std::uint32_t id,
                             const std::string& field_name) {
    Frame req;
    req.opcode = Opcode::GetField;
    write_u32_le(id, req.payload);
    req.payload.insert(req.payload.end(),
                       field_name.begin(), field_name.end());
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetFieldAck) {
        return util::Error{5000, 0, "GetField: server error",
                           field_name};
    }
    return std::string(rep.value().payload.begin(),
                       rep.value().payload.end());
}

util::Result<std::uint32_t>
RemoteConnection::record_count(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::GetRecordCount;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetRecordCountAck ||
        rep.value().payload.size() < 4) {
        return util::Error{5000, 0,
            "GetRecordCount: server error", ""};
    }
    return read_u32_le(rep.value().payload.data());
}

util::Result<bool> RemoteConnection::at_eof(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::AtEOF;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::AtEOFAck ||
        rep.value().payload.empty()) {
        return util::Error{5000, 0, "AtEOF: server error", ""};
    }
    return rep.value().payload[0] != 0;
}

util::Result<void> RemoteConnection::append_blank(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::AppendBlank;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::AppendBlankAck) {
        return util::Error{5000, 0, "AppendBlank: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::set_field(std::uint32_t id,
                                                const std::string& field_name,
                                                const std::string& value) {
    if (field_name.size() > 0xFFFFu) {
        return util::Error{5000, 0,
            "SetField: field name too long", field_name};
    }
    Frame req;
    req.opcode = Opcode::SetField;
    write_u32_le(id, req.payload);
    auto nlen = static_cast<std::uint16_t>(field_name.size());
    req.payload.push_back(static_cast<std::uint8_t>( nlen        & 0xFFu));
    req.payload.push_back(static_cast<std::uint8_t>((nlen >>  8) & 0xFFu));
    req.payload.insert(req.payload.end(),
                       field_name.begin(), field_name.end());
    req.payload.insert(req.payload.end(), value.begin(), value.end());
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::SetFieldAck) {
        return util::Error{5000, 0, "SetField: server error",
                           field_name};
    }
    return {};
}

util::Result<void> RemoteConnection::delete_record(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::DeleteRecord;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DeleteRecordAck) {
        return util::Error{5000, 0, "DeleteRecord: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::recall_record(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::RecallRecord;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::RecallRecordAck) {
        return util::Error{5000, 0, "RecallRecord: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::goto_record(std::uint32_t id,
                                                  std::uint32_t recno) {
    Frame req;
    req.opcode = Opcode::GotoRecord;
    write_u32_le(id, req.payload);
    write_u32_le(recno, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GotoRecordAck) {
        return util::Error{5000, 0, "GotoRecord: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::flush_table(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::FlushTable;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FlushTableAck) {
        return util::Error{5000, 0, "FlushTable: server error", ""};
    }
    return {};
}

} // namespace openads::network
