// CDX character-key length: a character field indexed into a CDX bag must
// store its key at the FULL field width, not at the trimmed width of the
// first record's value. Deriving the key length from the trimmed first
// record truncates every later key that shares a prefix beyond that width
// (e.g. the first row is short but later rows are longer), which makes a
// native FoxPro/Clipper reader — and the index itself — collide distinct
// names and miss seeks.
//
// This mirrors the NTX numeric-key interop test: it builds the index through
// the ACE ABI and asserts (a) every on-disk leaf key is the field width and
// (b) a seek for a long, prefix-sharing name lands on the right record.

#include "doctest.h"
#include "openads/ace.h"
#include "drivers/cdx/cdx_index.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openads::drivers::IndexOpenMode;
using openads::drivers::SeekHit;
using openads::drivers::cdx::CdxIndex;

namespace {

constexpr std::uint16_t kNomeLen = 30;

// A 30-char space-padded NOME key, exactly as the field is stored on disk.
std::string nome_key(const std::string& name) {
    std::string k = name;
    k.resize(kNomeLen, ' ');
    return k;
}

// Build a DBF with a single character field NOME C(30). The first record is
// deliberately SHORT ("ANA") and two later records share a long prefix
// ("ANABELA ...") so a trimmed-first-record key length (3) would collide
// them; the field width (30) keeps them distinct.
fs::path stage_char_dbf(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "names.dbf";
    std::error_code ec;
    fs::remove(p, ec);

    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };

    const std::uint16_t rec_len = 1 + kNomeLen;   // delete byte + NOME
    const std::uint16_t hdr_len = 32 + 32 + 1;    // header + 1 field + 0x0D

    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 3;                                    // record count
    hdr[8]  = hdr_len & 0xFF; hdr[9]  = (hdr_len >> 8) & 0xFF;
    hdr[10] = rec_len & 0xFF; hdr[11] = (rec_len >> 8) & 0xFF;
    push(hdr.data(), hdr.size());

    std::array<std::uint8_t, 32> fnome{};
    std::strncpy(reinterpret_cast<char*>(fnome.data()), "NOME", 11);
    fnome[11] = 'C';
    fnome[16] = static_cast<std::uint8_t>(kNomeLen);
    fnome[17] = 0;
    push(fnome.data(), fnome.size());

    file.push_back(0x0D);

    auto rec = [&](const std::string& name) {
        file.push_back(' ');                       // not-deleted
        std::string v = nome_key(name);
        push(v.data(), v.size());
    };
    rec("ANA");               // record 1 — short, trims to 3 chars
    rec("ANABELA CARDOSO");   // record 2 — shares the "ANA" prefix
    rec("ANABELA FERREIRA");  // record 3 — shares "ANABELA " prefix with rec 2
    file.push_back(0x1A);

    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("CDX char key: width comes from the field, not the trimmed first record") {
    auto dir = fs::temp_directory_path() / "openads_cdx_char_keylen";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_char_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 name[16] = "names";
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 idxfile[16] = "names.cdx";
    UNSIGNED8 idxname[16] = "NOME_IDX";
    UNSIGNED8 expr[16]    = "NOME";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, idxname, expr,
                             nullptr, nullptr, 0, 512, &hIdx) == 0);
    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    auto bag = (dir / "names.cdx").string();
    REQUIRE(fs::exists(bag));

    CdxIndex ix;
    REQUIRE(ix.open_named(bag, IndexOpenMode::Shared, "NOME_IDX").has_value());

    // Walk every key in order; each must be the full field width (30) so the
    // prefix-sharing names stay distinct and sorted.
    std::vector<std::string> keys;
    auto s = ix.seek_first();
    REQUIRE(s.has_value());
    while (s.value().positioned) {
        keys.push_back(ix.current_key());
        s = ix.next();
        REQUIRE(s.has_value());
    }
    REQUIRE(keys.size() == 3);
    for (const auto& k : keys) {
        CHECK(k.size() == kNomeLen);              // not truncated to 3
    }
    // Ascending order: "ANA" < "ANABELA CARDOSO" < "ANABELA FERREIRA".
    CHECK(keys[0] == nome_key("ANA"));
    CHECK(keys[1] == nome_key("ANABELA CARDOSO"));
    CHECK(keys[2] == nome_key("ANABELA FERREIRA"));

    // A seek for the longest, prefix-sharing name must land on record 3, not
    // collide onto an earlier truncated key.
    auto seek = ix.seek_key(nome_key("ANABELA FERREIRA"), false);
    REQUIRE(seek.has_value());
    CHECK(seek.value().hit == SeekHit::Exact);
    CHECK(seek.value().recno == 3u);

    fs::remove_all(dir, ec);
}
