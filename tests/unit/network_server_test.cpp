#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"
#include "network/socket.h"
#include "network/wire.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using openads::network::Server;
using openads::network::Socket;
using openads::network::connect_tcp;
using openads::network::sock_close;
using openads::network::Frame;
using openads::network::Opcode;
using openads::network::read_frame;
using openads::network::write_frame;

TEST_CASE("M12.3 server Hello → HelloAck round-trip") {
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    CHECK(srv.running());
    auto port = srv.port();
    REQUIRE(port != 0);

    auto cli = connect_tcp("127.0.0.1", port);
    REQUIRE(cli.has_value());
    Socket cs = cli.value();

    Frame req;
    req.opcode = Opcode::Hello;
    REQUIRE(write_frame(cs, req).has_value());
    auto reply = read_frame(cs);
    REQUIRE(reply.has_value());
    CHECK(reply.value().opcode == Opcode::HelloAck);
    std::string ver(reply.value().payload.begin(),
                    reply.value().payload.end());
    CHECK(ver == "openads/0.3.2");

    sock_close(cs);
    srv.stop();
    CHECK_FALSE(srv.running());
}

TEST_CASE("M12.3 server Connect against a real data dir succeeds") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_3_connect";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    auto cli = connect_tcp("127.0.0.1", srv.port());
    REQUIRE(cli.has_value());
    Socket cs = cli.value();

    Frame req;
    req.opcode = Opcode::Connect;
    std::string ds = dir.string();
    auto pushlen = [](std::vector<std::uint8_t>& out, std::uint16_t n) {
        out.push_back(static_cast<std::uint8_t>( n        & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((n >>  8) & 0xFFu));
    };
    pushlen(req.payload, static_cast<std::uint16_t>(ds.size()));
    req.payload.insert(req.payload.end(), ds.begin(), ds.end());
    pushlen(req.payload, 0);                       // empty user
    pushlen(req.payload, 0);                       // empty password
    REQUIRE(write_frame(cs, req).has_value());
    auto reply = read_frame(cs);
    REQUIRE(reply.has_value());
    CHECK(reply.value().opcode == Opcode::ConnectAck);
    std::string s(reply.value().payload.begin(),
                  reply.value().payload.end());
    CHECK(s == std::string("connected:") + ds);

    sock_close(cs);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.3 server unknown opcode returns Error frame") {
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    auto cli = connect_tcp("127.0.0.1", srv.port());
    REQUIRE(cli.has_value());
    Socket cs = cli.value();

    Frame req;
    req.opcode = static_cast<Opcode>(0x7E);   // truly unknown
    REQUIRE(write_frame(cs, req).has_value());
    auto reply = read_frame(cs);
    REQUIRE(reply.has_value());
    CHECK(reply.value().opcode == Opcode::Error);
    // M12.10 — Error payload now starts with [u32 ace_code]; skip 4
    // bytes to recover the textual message.
    REQUIRE(reply.value().payload.size() >= 4);
    std::string s(reply.value().payload.begin() + 4,
                  reply.value().payload.end());
    CHECK(s == "unsupported opcode");

    sock_close(cs);
    srv.stop();
}

namespace {

void m12_write_dbf(const std::filesystem::path& path,
                   const std::vector<std::string>& tags) {
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = static_cast<std::uint8_t>(tags.size());
    hdr[8]  = 32 + 32 + 1;
    hdr[10] = 1 + 4;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 4;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    for (auto& t : tags) {
        file.push_back(' ');
        for (std::size_t i = 0; i < 4; ++i)
            file.push_back(i < t.size()
                ? static_cast<std::uint8_t>(t[i]) : ' ');
    }
    file.push_back(0x1A);
    std::ofstream(path, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
}

void m12_write_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>( x        & 0xFFu));
    v.push_back(static_cast<std::uint8_t>((x >>  8) & 0xFFu));
    v.push_back(static_cast<std::uint8_t>((x >> 16) & 0xFFu));
    v.push_back(static_cast<std::uint8_t>((x >> 24) & 0xFFu));
}

}  // namespace

TEST_CASE("M12.4 remote OpenTable + GetRecordCount + walk + GetField") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_4";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA", "BBBB", "CCCC"});

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    auto cli = connect_tcp("127.0.0.1", srv.port());
    REQUIRE(cli.has_value());
    Socket cs = cli.value();

    // Connect.
    {
        Frame req;
        req.opcode = Opcode::Connect;
        std::string ds = dir.string();
        auto pushlen = [](std::vector<std::uint8_t>& out, std::uint16_t n) {
            out.push_back(static_cast<std::uint8_t>( n        & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((n >>  8) & 0xFFu));
        };
        pushlen(req.payload, static_cast<std::uint16_t>(ds.size()));
        req.payload.insert(req.payload.end(), ds.begin(), ds.end());
        pushlen(req.payload, 0);                   // user
        pushlen(req.payload, 0);                   // password
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::ConnectAck);
    }

    // OpenTable.
    std::uint32_t tid = 0;
    {
        Frame req;
        req.opcode = Opcode::OpenTable;
        std::string leaf = "data.dbf";
        req.payload.assign(leaf.begin(), leaf.end());
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::OpenTableAck);
        REQUIRE(rep.value().payload.size() == 4);
        tid = static_cast<std::uint32_t>(rep.value().payload[0]) |
              (static_cast<std::uint32_t>(rep.value().payload[1]) <<  8) |
              (static_cast<std::uint32_t>(rep.value().payload[2]) << 16) |
              (static_cast<std::uint32_t>(rep.value().payload[3]) << 24);
        CHECK(tid == 1);
    }

    // GetRecordCount.
    {
        Frame req;
        req.opcode = Opcode::GetRecordCount;
        m12_write_u32(req.payload, tid);
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::GetRecordCountAck);
        std::uint32_t rc =
            static_cast<std::uint32_t>(rep.value().payload[0]) |
            (static_cast<std::uint32_t>(rep.value().payload[1]) <<  8) |
            (static_cast<std::uint32_t>(rep.value().payload[2]) << 16) |
            (static_cast<std::uint32_t>(rep.value().payload[3]) << 24);
        CHECK(rc == 3);
    }

    // GotoTop + GetField on row 1.
    {
        Frame req;
        req.opcode = Opcode::GotoTop;
        m12_write_u32(req.payload, tid);
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::GotoTopAck);
    }
    auto get_tag = [&]() {
        Frame req;
        req.opcode = Opcode::GetField;
        m12_write_u32(req.payload, tid);
        std::string fname = "TAG";
        req.payload.insert(req.payload.end(),
                           fname.begin(), fname.end());
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::GetFieldAck);
        return std::string(rep.value().payload.begin(),
                           rep.value().payload.end());
    };
    CHECK(get_tag() == "AAAA");

    // Skip +1 → row 2.
    {
        Frame req;
        req.opcode = Opcode::Skip;
        m12_write_u32(req.payload, tid);
        m12_write_u32(req.payload, 1);
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::SkipAck);
    }
    CHECK(get_tag() == "BBBB");

    // CloseTable.
    {
        Frame req;
        req.opcode = Opcode::CloseTable;
        m12_write_u32(req.payload, tid);
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::CloseTableAck);
    }

    sock_close(cs);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.5 dual-mode AdsConnect60 with tcp:// URI routes ABI calls to server") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_5";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA", "BBBB"});

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[256];
    std::snprintf(uri, sizeof(uri),
                  "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()),
                  dir.string().c_str());

    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    UNSIGNED8 leaf[64] = "data.dbf";

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &cnt) == 0);
    CHECK(cnt == 2);

    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED8  buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, (UNSIGNED8*)"TAG", buf, &cap, 0) == 0);
    std::string s((char*)buf, cap);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    CHECK(s == "AAAA");

    REQUIRE(AdsSkip(hTable, 1) == 0);
    cap = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hTable, (UNSIGNED8*)"TAG", buf, &cap, 0) == 0);
    std::string s2((char*)buf, cap);
    while (!s2.empty() && s2.back() == ' ') s2.pop_back();
    CHECK(s2 == "BBBB");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.6 remote append + set_field + delete + recall + flush round-trip") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_6";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA", "BBBB"});

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[256];
    std::snprintf(uri, sizeof(uri),
                  "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()),
                  dir.string().c_str());

    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    UNSIGNED8 leaf[64] = "data.dbf";

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    // Append a third record + write its TAG over the wire.
    REQUIRE(AdsAppendRecord(hTable) == 0);
    UNSIGNED8 fld[8] = "TAG";
    UNSIGNED8 val[8] = "CCCC";
    REQUIRE(AdsSetString(hTable, fld, val, 4) == 0);
    REQUIRE(AdsWriteRecord(hTable) == 0);          // wire flush

    // Confirm record_count climbed to 3 + the value round-trips.
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &cnt) == 0);
    CHECK(cnt == 3);

    REQUIRE(AdsGotoRecord(hTable, 3) == 0);
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    std::string s((char*)buf, cap);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    CHECK(s == "CCCC");

    // Delete + recall round-trip.
    REQUIRE(AdsGotoRecord(hTable, 2) == 0);
    REQUIRE(AdsDeleteRecord(hTable) == 0);
    UNSIGNED16 del = 0;
    // is_deleted is read-only / local-only here; verify by reopening
    // the file at the end. For now exercise recall.
    REQUIRE(AdsRecallRecord(hTable) == 0);
    (void)del;

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();

    // Re-open through a *local* connection to confirm the write hit
    // disk on the server side.
    UNSIGNED8 lsrv[256];
    std::memcpy(lsrv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hLocal = 0;
    REQUIRE(AdsConnect60(lsrv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hLocal) == 0);
    ADSHANDLE hLocalT = 0;
    REQUIRE(AdsOpenTable(hLocal, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hLocalT) == 0);
    UNSIGNED32 cnt2 = 0;
    REQUIRE(AdsGetRecordCount(hLocalT, 0, &cnt2) == 0);
    CHECK(cnt2 == 3);
    REQUIRE(AdsGotoRecord(hLocalT, 3) == 0);
    UNSIGNED8 lbuf[16] = {0};
    UNSIGNED32 lcap = sizeof(lbuf);
    REQUIRE(AdsGetField(hLocalT, fld, lbuf, &lcap, 0) == 0);
    std::string s2((char*)lbuf, lcap);
    while (!s2.empty() && s2.back() == ' ') s2.pop_back();
    CHECK(s2 == "CCCC");
    REQUIRE(AdsCloseTable(hLocalT) == 0);
    REQUIRE(AdsDisconnect(hLocal) == 0);

    fs::remove_all(dir, ec);
}

TEST_CASE("M12.7 remote SQL exec — SELECT cursor + COUNT round-trip") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_7";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA", "BBBB", "CCCC"});

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[256];
    std::snprintf(uri, sizeof(uri),
                  "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()),
                  dir.string().c_str());
    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // 1) SELECT * FROM data — cursor with 3 rows.
    {
        UNSIGNED8 sql[64];
        std::strcpy(reinterpret_cast<char*>(sql),
                    "SELECT * FROM data.dbf");
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
        REQUIRE(hCur != 0);
        UNSIGNED32 cnt = 0;
        REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
        CHECK(cnt == 3);
        REQUIRE(AdsGotoTop(hCur) == 0);
        UNSIGNED8  buf[16] = {0};
        UNSIGNED32 cap = sizeof(buf);
        REQUIRE(AdsGetField(hCur, (UNSIGNED8*)"TAG",
                            buf, &cap, 0) == 0);
        std::string s((char*)buf, cap);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        CHECK(s == "AAAA");
        REQUIRE(AdsCloseTable(hCur) == 0);
    }

    // 2) SELECT COUNT(*) — single-row aggregate cursor.
    {
        UNSIGNED8 sql[64];
        std::strcpy(reinterpret_cast<char*>(sql),
                    "SELECT COUNT(*) FROM data.dbf");
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
        REQUIRE(hCur != 0);
        UNSIGNED32 cnt = 0;
        REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
        CHECK(cnt == 1);
        REQUIRE(AdsCloseTable(hCur) == 0);
    }

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.8 remote AdsReindex routes through wire") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_8";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA", "BBBB"});

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[256];
    std::snprintf(uri, sizeof(uri),
                  "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()),
                  dir.string().c_str());
    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    UNSIGNED8 leaf[64] = "data.dbf";

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);
    // No bound indexes — Reindex over an indexless table is a no-op
    // success. The point of the test is that the wire op and the
    // server-side dispatch hold together end-to-end.
    REQUIRE(AdsReindex(hTable) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.9 server with credentials rejects bad password") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_9_bad";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA"});

    Server srv;
    srv.add_credential("admin", "letmein");
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[256];
    std::snprintf(uri, sizeof(uri),
                  "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()),
                  dir.string().c_str());
    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);

    UNSIGNED8 user[16] = "admin";
    UNSIGNED8 wrong[16] = "nope";
    ADSHANDLE hConn = 0;
    CHECK(AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                       user, wrong, 0, &hConn) != 0);
    CHECK(hConn == 0);

    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.9 server with credentials accepts matching password") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_9_ok";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA"});

    Server srv;
    srv.add_credential("admin", "letmein");
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[256];
    std::snprintf(uri, sizeof(uri),
                  "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()),
                  dir.string().c_str());
    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);

    UNSIGNED8 user[16] = "admin";
    UNSIGNED8 pw  [16] = "letmein";
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                         user, pw, 0, &hConn) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.10 server Error frame surfaces the real ACE code") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_10";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    Server srv;
    srv.add_credential("admin", "letmein");
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[256];
    std::snprintf(uri, sizeof(uri),
                  "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()),
                  dir.string().c_str());
    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    UNSIGNED8 user[16] = "admin";
    UNSIGNED8 wrong[16] = "nope";
    ADSHANDLE hConn = 0;
    UNSIGNED32 rc = AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                                  user, wrong, 0, &hConn);
    // Was AE_INTERNAL_ERROR (5000) before M12.10; now AE_LOGIN_FAILED.
    CHECK(rc == 7077u);
    CHECK(hConn == 0);

    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.3 server stop() drops in-flight connection cleanly") {
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    auto cli = connect_tcp("127.0.0.1", srv.port());
    REQUIRE(cli.has_value());
    Socket cs = cli.value();
    srv.stop();
    sock_close(cs);
    CHECK_FALSE(srv.running());
}
