// NTX numeric-key interop: a numeric field indexed into an NTX bag must
// store its key in the native DBFNTX numeric form so a native reader's
// dbSeek(<number>) lands on the right record.
//
// The exact native form was decoded byte-for-byte against Harbour DBFNTX
// (INDEX ON <numfield> TO bag) — see native_ntx_key() below:
//   * magnitude is ZERO-padded to the field width: printf("%0*.*f", W, D, |v|)
//     e.g. ID N(8,0) 42 -> "00000042"; VAL N(12,2) 13.50 -> "000000013.50"
//   * a negative value has every byte complemented as (0x5c - byte), so
//     negatives sort before positives and the decimal point (0x2e, whose
//     0x5c-complement is itself) stays fixed.
//     e.g. VAL -3.25 -> magnitude "000000003.25" -> ",,,,,,,,).*'"
//
// These tests build the index through the ACE ABI (mirroring
// abi_create_index61_test.cpp) and assert:
//   (a) the on-disk leaf key bytes equal native_ntx_key(value, len, dec); and
//   (b) AdsSeek for a known numeric value finds the right record.

#include "doctest.h"
#include "openads/ace.h"
#include "drivers/ntx/ntx_index.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Build a DBF with two numeric fields: ID N(8,0) and VAL N(12,2).
// `rec_count` controls how many records are written (0 = empty table).
// Records (ID, VAL):  (42, 13.50), (7, 100.00), (1000, -3.25)
fs::path stage_numeric_dbf_n(const fs::path& dir, int rec_count) {
    fs::create_directories(dir);
    auto p = dir / "nums.dbf";
    std::error_code ec;
    fs::remove(p, ec);

    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };

    const std::uint16_t id_len  = 8;
    const std::uint16_t val_len = 12;
    const std::uint8_t  val_dec = 2;
    const std::uint16_t rec_len = 1 + id_len + val_len; // delete byte + 2 fields
    const std::uint16_t hdr_len = 32 + 32 + 32 + 1;     // header + 2 fields + 0x0D

    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = static_cast<std::uint8_t>(rec_count); // record count
    hdr[8]  = hdr_len & 0xFF; hdr[9]  = (hdr_len >> 8) & 0xFF;
    hdr[10] = rec_len & 0xFF; hdr[11] = (rec_len >> 8) & 0xFF;
    push(hdr.data(), hdr.size());

    std::array<std::uint8_t, 32> fid{};
    std::strncpy(reinterpret_cast<char*>(fid.data()), "ID", 11);
    fid[11] = 'N'; fid[16] = static_cast<std::uint8_t>(id_len); fid[17] = 0;
    push(fid.data(), fid.size());

    std::array<std::uint8_t, 32> fval{};
    std::strncpy(reinterpret_cast<char*>(fval.data()), "VAL", 11);
    fval[11] = 'N'; fval[16] = static_cast<std::uint8_t>(val_len); fval[17] = val_dec;
    push(fval.data(), fval.size());

    file.push_back(0x0D);

    auto right_just = [](const std::string& s, std::size_t w) {
        std::string out(w, ' ');
        std::size_t n = std::min(s.size(), w);
        for (std::size_t i = 0; i < n; ++i) out[w - n + i] = s[i];
        return out;
    };
    auto rec = [&](const char* id_str, const char* val_str) {
        file.push_back(' '); // not-deleted
        std::string a = right_just(id_str, id_len);
        std::string b = right_just(val_str, val_len);
        push(a.data(), a.size());
        push(b.data(), b.size());
    };
    if (rec_count >= 1) rec("42",   "13.50");
    if (rec_count >= 2) rec("7",    "100.00");
    if (rec_count >= 3) rec("1000", "-3.25");
    file.push_back(0x1A);

    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

fs::path stage_numeric_dbf(const fs::path& dir) {
    return stage_numeric_dbf_n(dir, 3);
}

// Native DBFNTX numeric key — verified byte-exact against Harbour DBFNTX.
// Magnitude zero-padded to the field width via printf("%0*.*f", W, D, |v|);
// for a negative value every byte is complemented as (0x5c - byte).
std::string native_ntx_key(double v, int width, int dec) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%0*.*f", width, dec, v < 0 ? -v : v);
    std::string out = buf;
    if (static_cast<int>(out.size()) > width)
        out = out.substr(out.size() - static_cast<std::size_t>(width));
    if (v < 0)
        for (char& c : out)
            c = static_cast<char>(0x5c - static_cast<unsigned char>(c));
    return out;
}

// Read every leaf key from an NTX file by walking the driver in key order
// and collecting current_key(). Returns keys in ascending B+tree order.
std::vector<std::string> read_ntx_keys(const std::string& ntx_path) {
    std::vector<std::string> keys;
    openads::drivers::ntx::NtxIndex idx;
    auto o = idx.open(ntx_path, openads::drivers::IndexOpenMode::ReadOnly);
    if (!o) return keys;
    auto so = idx.seek_first();
    if (!so) return keys;
    while (so.value().positioned) {
        keys.push_back(idx.current_key());
        so = idx.next();
        if (!so) break;
    }
    return keys;
}

} // namespace

TEST_CASE("NTX numeric key: on-disk bytes equal native_ntx_key — N(8,0)") {
    auto dir = fs::temp_directory_path() / "openads_ntx_num_id";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_numeric_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 name[16] = "nums";
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_NTX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 idxfile[16] = "id.ntx";  // explicit .ntx bag
    UNSIGNED8 idxname[16] = "ID";
    UNSIGNED8 expr[16]    = "ID";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, idxname, expr,
                             nullptr, nullptr, 0, 512, &hIdx) == 0);
    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    auto ntx = (dir / "id.ntx").string();
    REQUIRE(fs::exists(ntx));
    auto keys = read_ntx_keys(ntx);
    REQUIRE(keys.size() == 3);

    // Native form for ID N(8,0): zero-padded width 8, no decimals.
    std::string k7    = native_ntx_key(7.0,    8, 0); // "00000007"
    std::string k42   = native_ntx_key(42.0,   8, 0); // "00000042"
    std::string k1000 = native_ntx_key(1000.0, 8, 0); // "00001000"

    // Every stored key must be exactly 8 bytes and equal the native form.
    for (const auto& k : keys) {
        CHECK(k.size() == 8);
    }
    // Ascending order: 7, 42, 1000.
    CHECK(keys[0] == k7);
    CHECK(keys[1] == k42);
    CHECK(keys[2] == k1000);

    fs::remove_all(dir, ec);
}

TEST_CASE("NTX numeric key: on-disk bytes equal native_ntx_key — N(12,2)") {
    auto dir = fs::temp_directory_path() / "openads_ntx_num_val";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_numeric_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 name[16] = "nums";
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_NTX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 idxfile[16] = "val.ntx";
    UNSIGNED8 idxname[16] = "VAL";
    UNSIGNED8 expr[16]    = "VAL";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, idxname, expr,
                             nullptr, nullptr, 0, 512, &hIdx) == 0);
    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    auto ntx = (dir / "val.ntx").string();
    REQUIRE(fs::exists(ntx));
    auto keys = read_ntx_keys(ntx);
    REQUIRE(keys.size() == 3);

    // VAL N(12,2): zero-padded width 12, 2 decimals. Negative is byte-
    // complemented, so -3.25 -> ",,,,,,,,).*'" sorts before the positives.
    std::string kn = native_ntx_key(-3.25,  12, 2); // ",,,,,,,,).*'"
    std::string ka = native_ntx_key(13.50,  12, 2); // "000000013.50"
    std::string kb = native_ntx_key(100.00, 12, 2); // "000000100.00"

    for (const auto& k : keys) {
        CHECK(k.size() == 12);
    }
    // Ascending order: -3.25, 13.50, 100.00.
    CHECK(keys[0] == kn);
    CHECK(keys[1] == ka);
    CHECK(keys[2] == kb);

    fs::remove_all(dir, ec);
}

TEST_CASE("NTX numeric key: AdsSeek(<number>) finds the record") {
    auto dir = fs::temp_directory_path() / "openads_ntx_num_seek";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_numeric_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 name[16] = "nums";
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_NTX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 idxfile[16] = "id.ntx";
    UNSIGNED8 idxname[16] = "ID";
    UNSIGNED8 expr[16]    = "ID";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, idxname, expr,
                             nullptr, nullptr, 0, 512, &hIdx) == 0);

    // Native rddads dbSeek(<number>) passes the value as raw IEEE-754
    // double bytes (u16KeyLen == sizeof(double)); the engine reformats it
    // to the stored ASCII key. Seek 42 -> record 1.
    double dv = 42.0;
    UNSIGNED8 key[16];
    std::memcpy(key, &dv, sizeof(double));
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hIdx, key, static_cast<UNSIGNED16>(sizeof(double)),
                    0, 0, &found) == 0);
    CHECK(found == 1);
    UNSIGNED32 recno = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 1); // ID 42 is record 1

    // Seek 1000 -> record 3.
    dv = 1000.0;
    std::memcpy(key, &dv, sizeof(double));
    found = 0;
    REQUIRE(AdsSeek(hIdx, key, static_cast<UNSIGNED16>(sizeof(double)),
                    0, 0, &found) == 0);
    CHECK(found == 1);
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 3);

    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// Empty-table case: an index built on an empty numeric table cannot probe
// a key width from a record, so it must take the width from the FIELD
// descriptor (N(8,0) -> width 8). If it falls back to a 32-char default,
// every key appended afterwards is the wrong width -> a native dbSeek misses.
TEST_CASE("NTX numeric key: width comes from field descriptor on empty table") {
    auto dir = fs::temp_directory_path() / "openads_ntx_num_empty";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_numeric_dbf_n(dir, 0); // empty table, ID N(8,0), VAL N(12,2)

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 name[16] = "nums";
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_NTX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 idxfile[16] = "id.ntx";
    UNSIGNED8 idxname[16] = "ID";
    UNSIGNED8 expr[16]    = "ID";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, idxname, expr,
                             nullptr, nullptr, 0, 512, &hIdx) == 0);

    // Append a record with ID = 42 through the ABI; the index must store
    // the key at the field width (8), not the 32-char default.
    REQUIRE(AdsAppendRecord(hTable) == 0);
    UNSIGNED8 idfld[8] = "ID";
    double dv = 42.0;
    REQUIRE(AdsSetDouble(hTable, idfld, dv) == 0);
    REQUIRE(AdsWriteRecord(hTable) == 0);

    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    auto ntx = (dir / "id.ntx").string();
    REQUIRE(fs::exists(ntx));
    auto keys = read_ntx_keys(ntx);
    REQUIRE(keys.size() == 1);
    // Native form: native_ntx_key(42,8,0) = "00000042", exactly 8 bytes.
    CHECK(keys[0].size() == 8);
    CHECK(keys[0] == native_ntx_key(42.0, 8, 0));

    fs::remove_all(dir, ec);
}

// Reopen-and-append: the creating session pins the numeric format via
// set_numeric_format, but a later session that reopens the bag through
// AdsOpenIndex must restore the numeric mark — otherwise the appended key
// reverts to space-padded text and a native dbSeek misses it. Uses VAL
// N(12,2) with a NEGATIVE value to also exercise the byte-complement on the
// reopen path.
TEST_CASE("NTX numeric key: reopened index appends in native form") {
    auto dir = fs::temp_directory_path() / "openads_ntx_num_reopen";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_numeric_dbf(dir); // 3 records

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    // Session 1: create the VAL index, then close everything.
    {
        ADSHANDLE hTable = 0;
        UNSIGNED8 name[16] = "nums";
        REQUIRE(AdsOpenTable(hConn, name, name, ADS_NTX, 1, 1, 0, 1, &hTable) == 0);
        UNSIGNED8 idxfile[16] = "val.ntx";
        UNSIGNED8 idxname[16] = "VAL";
        UNSIGNED8 expr[16]    = "VAL";
        ADSHANDLE hIdx = 0;
        REQUIRE(AdsCreateIndex61(hTable, idxfile, idxname, expr,
                                 nullptr, nullptr, 0, 512, &hIdx) == 0);
        REQUIRE(AdsCloseIndex(hIdx) == 0);
        REQUIRE(AdsCloseTable(hTable) == 0);
    }

    // Session 2: reopen the bag via AdsOpenIndex and append a NEGATIVE value.
    {
        ADSHANDLE hTable = 0;
        UNSIGNED8 name[16] = "nums";
        REQUIRE(AdsOpenTable(hConn, name, name, ADS_NTX, 1, 1, 0, 1, &hTable) == 0);
        UNSIGNED8 idxfile[16] = "val.ntx";
        ADSHANDLE ah[8]; UNSIGNED16 alen = 8;
        REQUIRE(AdsOpenIndex(hTable, idxfile, ah, &alen) == 0);
        REQUIRE(AdsAppendRecord(hTable) == 0);
        UNSIGNED8 vfld[8] = "VAL";
        REQUIRE(AdsSetDouble(hTable, vfld, -99.99) == 0);
        REQUIRE(AdsWriteRecord(hTable) == 0);
        REQUIRE(AdsCloseAllIndexes(hTable) == 0);
        REQUIRE(AdsCloseTable(hTable) == 0);
    }
    REQUIRE(AdsDisconnect(hConn) == 0);

    auto ntx = (dir / "val.ntx").string();
    REQUIRE(fs::exists(ntx));
    auto keys = read_ntx_keys(ntx);
    REQUIRE(keys.size() == 4);
    // The appended -99.99 must be stored in the native complemented form and
    // sort first (most negative). Every key stays 12 bytes.
    for (const auto& k : keys) {
        CHECK(k.size() == 12);
    }
    CHECK(keys[0] == native_ntx_key(-99.99, 12, 2));

    fs::remove_all(dir, ec);
}
