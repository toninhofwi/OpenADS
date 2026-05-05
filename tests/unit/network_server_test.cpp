#include "doctest.h"
#include "network/server.h"
#include "network/socket.h"
#include "network/wire.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

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

TEST_CASE("M12.3 server Connect echoes data_dir tag") {
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    auto cli = connect_tcp("127.0.0.1", srv.port());
    REQUIRE(cli.has_value());
    Socket cs = cli.value();

    Frame req;
    req.opcode = Opcode::Connect;
    std::string dir = "/data/foo";
    req.payload.assign(dir.begin(), dir.end());
    REQUIRE(write_frame(cs, req).has_value());
    auto reply = read_frame(cs);
    REQUIRE(reply.has_value());
    CHECK(reply.value().opcode == Opcode::ConnectAck);
    std::string s(reply.value().payload.begin(),
                  reply.value().payload.end());
    CHECK(s == std::string("connected:") + dir);

    sock_close(cs);
    srv.stop();
}

TEST_CASE("M12.3 server unsupported opcode returns Error frame") {
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    auto cli = connect_tcp("127.0.0.1", srv.port());
    REQUIRE(cli.has_value());
    Socket cs = cli.value();

    Frame req;
    req.opcode = Opcode::OpenTable;     // not yet implemented in M12.3
    REQUIRE(write_frame(cs, req).has_value());
    auto reply = read_frame(cs);
    REQUIRE(reply.has_value());
    CHECK(reply.value().opcode == Opcode::Error);
    std::string s(reply.value().payload.begin(),
                  reply.value().payload.end());
    CHECK(s == "unsupported opcode");

    sock_close(cs);
    srv.stop();
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
