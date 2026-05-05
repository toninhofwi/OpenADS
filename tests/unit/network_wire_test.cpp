#include "doctest.h"
#include "network/wire.h"

#include <cstdint>
#include <string>
#include <vector>

using openads::network::Frame;
using openads::network::Opcode;
using openads::network::encode_frame;
using openads::network::decode_frame;

TEST_CASE("M12.1 wire frame encode/decode round-trip") {
    Frame in;
    in.opcode = Opcode::ExecuteSQL;
    std::string sql = "SELECT * FROM t";
    in.payload.assign(sql.begin(), sql.end());

    auto enc = encode_frame(in);
    REQUIRE(enc.has_value());
    auto& bytes = enc.value();
    REQUIRE(bytes.size() == 5 + sql.size());
    CHECK(bytes[0] == 0x00);
    CHECK(bytes[1] == 0x00);
    CHECK(bytes[2] == 0x00);
    CHECK(bytes[3] == static_cast<std::uint8_t>(sql.size()));
    CHECK(bytes[4] == static_cast<std::uint8_t>(Opcode::ExecuteSQL));

    std::size_t consumed = 0;
    auto dec = decode_frame(bytes.data(), bytes.size(), &consumed);
    REQUIRE(dec.has_value());
    CHECK(consumed == bytes.size());
    CHECK(dec.value().opcode == Opcode::ExecuteSQL);
    CHECK(std::string(dec.value().payload.begin(),
                      dec.value().payload.end()) == sql);
}

TEST_CASE("M12.1 wire decode rejects truncated buffer") {
    std::vector<std::uint8_t> partial = {0x00, 0x00, 0x00, 0x10, 0x30,
                                          'a', 'b', 'c'};
    auto dec = decode_frame(partial.data(), partial.size());
    CHECK_FALSE(dec.has_value());
}

TEST_CASE("M12.1 wire decode rejects header-only buffer < 5 bytes") {
    std::vector<std::uint8_t> tiny = {0x00, 0x00};
    auto dec = decode_frame(tiny.data(), tiny.size());
    CHECK_FALSE(dec.has_value());
}

TEST_CASE("M12.1 wire encode + decode an empty-payload frame") {
    Frame f;
    f.opcode = Opcode::Disconnect;
    auto enc = encode_frame(f);
    REQUIRE(enc.has_value());
    REQUIRE(enc.value().size() == 5);
    auto dec = decode_frame(enc.value().data(), enc.value().size());
    REQUIRE(dec.has_value());
    CHECK(dec.value().opcode == Opcode::Disconnect);
    CHECK(dec.value().payload.empty());
}
