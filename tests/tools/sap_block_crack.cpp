// sap_block_crack.cpp
// Reverse-engineering tool for the 16-byte encrypted DB: built-in group block
// in SAP binary .add User records.
//
// Strategy:
//   1. Read User records from .add binary (extract OID, plen, prop273, etc.)
//   2. Connect via SAP DLL to get ground-truth DB: membership
//   3. Locate the "10 00" length marker in each user's post-token section
//      and extract the 16-byte cipher block that follows
//   4. For each user, try DES-ECB and 3DES-ECB decryption with a broad
//      set of candidate keys derived from: username, OID, prop273 bytes,
//      database path, and known plaintext passwords
//   5. For each decryption, test whether the result matches any plausible
//      plaintext pattern for the user's known membership bits
//   6. Report any hit; also do XOR pair analysis and dump structured data
//
// Plaintext hypothesis:
//   The 16 bytes encode two 8-byte DES blocks.
//   Block 0: "which DB: groups this user BELONGS TO"
//   Block 1: "which DB: groups this user can GRANT" (WITH_GRANT)
//   Encoding (little-endian bitmask in first 4 bytes, rest zeros/padding):
//     DB:Admin  = bit 0 (0x01)
//     DB:Backup = bit 1 (0x02)
//     DB:Debug  = bit 2 (0x04)
//   OR: a fixed plaintext, OR a plaintext derived from a constant + bitmask.
//   We test many encodings.
//
// Build (MSVC x64, from tests/tools/):
//   set MSVC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
//   set SDK=C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0
//   set SDKLIB=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0
//   set INCLUDE=%MSVC%\include;%SDK%\ucrt;%SDK%\um;%SDK%\shared
//   set LIB=%MSVC%\lib\x64;%SDKLIB%\ucrt\x64;%SDKLIB%\um\x64
//   "%MSVC%\bin\Hostx64\x64\cl.exe" sap_block_crack.cpp /Dx64 /EHsc /I f:\Ads11 ^
//     /Fe:sap_block_crack.exe /link f:\Ads11\ace64.lib Bcrypt.lib

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "ace.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "Bcrypt.lib")

// ---------------------------------------------------------------------------
// Binary helpers
// ---------------------------------------------------------------------------
static uint32_t le32(const uint8_t* b, size_t o) {
    return (uint32_t)b[o]|((uint32_t)b[o+1]<<8)|((uint32_t)b[o+2]<<16)|((uint32_t)b[o+3]<<24);
}
static uint16_t le16(const uint8_t* b, size_t o) {
    return (uint16_t)b[o]|((uint16_t)b[o+1]<<8);
}
static std::string trim(const uint8_t* b, size_t o, size_t len) {
    std::string s((const char*)b+o, len);
    auto n = s.find('\0'); if (n != std::string::npos) s.resize(n);
    while (!s.empty() && (uint8_t)s.back() <= 0x20) s.pop_back();
    return s;
}
static void printhex(const char* label, const uint8_t* b, size_t n) {
    printf("  %s: ", label);
    for (size_t i = 0; i < n; ++i) printf("%02X ", b[i]);
    printf("\n");
}

// ---------------------------------------------------------------------------
// BCrypt DES helpers
// ---------------------------------------------------------------------------
static bool bcrypt_des_ecb(const uint8_t key8[8], const uint8_t* ct, size_t ct_len,
                            uint8_t* pt, bool decrypt)
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;

    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_DES_ALGORITHM, nullptr, 0)))
        return false;
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
        (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0);

    struct KBlob {
        BCRYPT_KEY_DATA_BLOB_HEADER h;
        uint8_t key[8];
    } kb;
    kb.h.dwMagic   = BCRYPT_KEY_DATA_BLOB_MAGIC;
    kb.h.dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
    kb.h.cbKeyData = 8;
    memcpy(kb.key, key8, 8);

    bool ok = BCRYPT_SUCCESS(BCryptImportKey(hAlg, nullptr, BCRYPT_KEY_DATA_BLOB,
                                             &hKey, nullptr, 0,
                                             (PUCHAR)&kb, sizeof(kb), 0));
    if (ok) {
        ULONG result = 0;
        if (decrypt)
            ok = BCRYPT_SUCCESS(BCryptDecrypt(hKey, (PUCHAR)ct, (ULONG)ct_len,
                                              nullptr, nullptr, 0,
                                              (PUCHAR)pt, (ULONG)ct_len, &result, 0));
        else
            ok = BCRYPT_SUCCESS(BCryptEncrypt(hKey, (PUCHAR)ct, (ULONG)ct_len,
                                              nullptr, nullptr, 0,
                                              (PUCHAR)pt, (ULONG)ct_len, &result, 0));
        BCryptDestroyKey(hKey);
    }
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}

static bool des_ecb_dec(const uint8_t k[8], const uint8_t ct[8], uint8_t pt[8]) {
    return bcrypt_des_ecb(k, ct, 8, pt, true);
}
static bool des_ecb_enc(const uint8_t k[8], const uint8_t pt[8], uint8_t ct[8]) {
    return bcrypt_des_ecb(k, pt, 8, ct, false);
}

// 3DES-ECB with 16-byte key (k1=k[0..7], k2=k[8..15], k3=k[0..7])
static bool tdes_ecb_dec(const uint8_t key16[16], const uint8_t* ct, size_t ct_len, uint8_t* pt) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;

    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_3DES_112_ALGORITHM, nullptr, 0)))
        return false;
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
        (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0);

    struct KBlob {
        BCRYPT_KEY_DATA_BLOB_HEADER h;
        uint8_t key[16];
    } kb;
    kb.h.dwMagic   = BCRYPT_KEY_DATA_BLOB_MAGIC;
    kb.h.dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
    kb.h.cbKeyData = 16;
    memcpy(kb.key, key16, 16);

    bool ok = BCRYPT_SUCCESS(BCryptImportKey(hAlg, nullptr, BCRYPT_KEY_DATA_BLOB,
                                             &hKey, nullptr, 0,
                                             (PUCHAR)&kb, sizeof(kb), 0));
    if (ok) {
        ULONG result = 0;
        ok = BCRYPT_SUCCESS(BCryptDecrypt(hKey, (PUCHAR)ct, (ULONG)ct_len,
                                          nullptr, nullptr, 0,
                                          (PUCHAR)pt, (ULONG)ct_len, &result, 0));
        BCryptDestroyKey(hKey);
    }
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}

// DES-CBC: decrypt 16 bytes using 8-byte key and 8-byte IV
static bool des_cbc_dec(const uint8_t k[8], const uint8_t iv[8],
                         const uint8_t ct[16], uint8_t pt[16]) {
    // CBC: block[0] = DES_ECB_dec(ct[0]) XOR iv
    //       block[1] = DES_ECB_dec(ct[1]) XOR ct[0]
    uint8_t tmp0[8], tmp1[8];
    if (!des_ecb_dec(k, ct+0, tmp0)) return false;
    if (!des_ecb_dec(k, ct+8, tmp1)) return false;
    for (int i = 0; i < 8; ++i) { pt[i]   = tmp0[i] ^ iv[i]; }
    for (int i = 0; i < 8; ++i) { pt[8+i] = tmp1[i] ^ ct[i]; }
    return true;
}

// ---------------------------------------------------------------------------
// Key generation helpers
// ---------------------------------------------------------------------------
using Key8  = std::array<uint8_t,8>;
using Key16 = std::array<uint8_t,16>;

// Pad a string to N bytes with pad_byte; truncate if longer
static Key8 pad8(const std::string& s, uint8_t pad = 0) {
    Key8 k{}; k.fill(pad);
    size_t n = std::min(s.size(), (size_t)8);
    for (size_t i = 0; i < n; ++i) k[i] = (uint8_t)s[i];
    return k;
}
static Key16 pad16(const std::string& s, uint8_t pad = 0) {
    Key16 k{}; k.fill(pad);
    size_t n = std::min(s.size(), (size_t)16);
    for (size_t i = 0; i < n; ++i) k[i] = (uint8_t)s[i];
    return k;
}
static Key8 from_oid_le(uint32_t oid) {
    Key8 k{};
    k[0]=(uint8_t)oid;       k[1]=(uint8_t)(oid>>8);
    k[2]=(uint8_t)(oid>>16); k[3]=(uint8_t)(oid>>24);
    k[4]=k[0]; k[5]=k[1]; k[6]=k[2]; k[7]=k[3];
    return k;
}
static Key8 from_oid_be(uint32_t oid) {
    Key8 k{};
    k[0]=(uint8_t)(oid>>24); k[1]=(uint8_t)(oid>>16);
    k[2]=(uint8_t)(oid>>8);  k[3]=(uint8_t)oid;
    k[4]=k[0]; k[5]=k[1]; k[6]=k[2]; k[7]=k[3];
    return k;
}

// ---------------------------------------------------------------------------
// User data structure
// ---------------------------------------------------------------------------
struct User {
    std::string name;
    uint32_t oid   = 0;
    uint32_t info1 = 0;
    uint32_t info2 = 0;
    uint16_t plen  = 0xFFFF;
    std::vector<uint8_t> prop273; // 273 bytes
    bool has_cipher = false;
    uint8_t cipher[16]{};
    // post-token section start offset within prop273
    size_t post_off = 0;
    // SAP ground truth
    bool db_admin  = false;
    bool db_backup = false;
    bool db_debug  = false;
    uint8_t bits() const {
        return (uint8_t)((db_admin?1:0)|(db_backup?2:0)|(db_debug?4:0));
    }
    const char* bits_str() const {
        static char buf[32];
        int n = 0;
        if (db_admin)  { strcpy_s(buf+n,32-n,"Admin"); n+=5; }
        if (db_backup) { if(n){buf[n++]='+';} strcpy_s(buf+n,32-n,"Backup"); n+=6; }
        if (db_debug)  { if(n){buf[n++]='+';} strcpy_s(buf+n,32-n,"Debug"); n+=5; }
        if (!n) strcpy_s(buf,32,"none");
        return buf;
    }
};

// ---------------------------------------------------------------------------
// Binary .add reader
// ---------------------------------------------------------------------------
static std::vector<User> read_add_binary(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { printf("Cannot open %s\n", path); return {}; }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), {});
    if (buf.size() < 0x28) { printf("File too short\n"); return {}; }

    const uint8_t* B = buf.data();
    uint32_t hdr_len = le32(B, 0x20);
    uint32_t rec_len = le32(B, 0x24);
    if (hdr_len < 0x28 || rec_len < 100 || hdr_len >= buf.size()) {
        printf("Bad header\n"); return {};
    }

    size_t total = (buf.size() - hdr_len) / rec_len;
    std::vector<User> users;

    for (size_t i = 0; i < total; ++i) {
        size_t base = hdr_len + i * rec_len;
        if (base + rec_len > buf.size()) break;
        if (B[base] != 0x04) continue;
        if (trim(B, base+13, 10) != "User") continue;

        std::string name = trim(B, base+23, 200);
        if (name.empty()) continue;

        User u;
        u.name  = name;
        u.oid   = le32(B, base+5);
        u.info1 = le32(B, base+507);
        u.info2 = le32(B, base+511);
        u.plen  = le16(B, base+223);

        size_t ps = base + 225;
        size_t pe = std::min(ps + 273, buf.size());
        u.prop273.assign(B+ps, B+pe);
        u.prop273.resize(273, 0xFF);

        // Find cipher block in post-token section
        if (u.plen != 0xFFFFu && u.plen < 273) {
            uint16_t cf = le16(u.prop273.data(), u.plen);
            uint32_t ntok = (cf == 0xFFFFu || cf == 0) ? 0 : (uint32_t)cf / 4u;
            u.post_off = u.plen + 2 + ntok * 4;
            // Scan post-token for "10 00" followed by 16 non-trivially-all-FF bytes
            const uint8_t* P = u.prop273.data();
            for (size_t j = u.post_off; j + 18 <= 273; ++j) {
                if (P[j] == 0x10 && P[j+1] == 0x00) {
                    // Check there are 16 bytes that look like cipher (at least some non-FF)
                    bool all_ff = true;
                    for (int k = 0; k < 16; ++k)
                        if (P[j+2+k] != 0xFF) { all_ff = false; break; }
                    if (!all_ff) {
                        u.has_cipher = true;
                        memcpy(u.cipher, P+j+2, 16);
                        break;
                    }
                }
            }
        }
        users.push_back(std::move(u));
    }
    return users;
}

// ---------------------------------------------------------------------------
// Database record extraction — reads the 273-byte property area of the
// "Database" record from the .add binary.  Returns true on success.
// ---------------------------------------------------------------------------
static bool read_db_record(const char* path, uint8_t out_prop273[273], uint16_t* out_plen = nullptr) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), {});
    if (buf.size() < 0x28) return false;

    const uint8_t* B = buf.data();
    uint32_t hdr_len = le32(B, 0x20);
    uint32_t rec_len = le32(B, 0x24);
    if (hdr_len >= buf.size() || rec_len < 100) return false;

    size_t total = (buf.size() - hdr_len) / rec_len;
    for (size_t i = 0; i < total; ++i) {
        size_t base = hdr_len + i * rec_len;
        if (base + rec_len > buf.size()) break;
        if (B[base] != 0x04) continue;
        if (trim(B, base+13, 10) != "Database") continue;

        size_t ps = base + 225;
        size_t actual = std::min<size_t>(273, buf.size() - ps);
        memcpy(out_prop273, B+ps, actual);
        if (actual < 273) memset(out_prop273+actual, 0xFF, 273-actual);

        uint16_t plen = le16(B, base+223);
        if (out_plen) *out_plen = plen;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// SAP membership query
// ---------------------------------------------------------------------------
static void query_sap(const char* path, const char* pw, std::vector<User>& users) {
    ADSHANDLE hConn = 0;
    UNSIGNED32 rc = AdsConnect60((UNSIGNED8*)path, ADS_LOCAL_SERVER,
                                  (UNSIGNED8*)"adssys", (UNSIGNED8*)pw, 0, &hConn);
    if (rc != 0) {
        printf("  SAP connect failed (%u) — membership will be unknown\n", rc);
        return;
    }
    for (auto& u : users) {
        unsigned char buf[4096] = {}; UNSIGNED16 len = sizeof(buf);
        if (AdsDDGetUserProperty(hConn, (UNSIGNED8*)u.name.c_str(),
                                 ADS_DD_USER_GROUP_MEMBERSHIP, buf, &len) == 0 && len > 0) {
            std::string s((char*)buf, len);
            // semicolon-separated group list
            for (size_t p = 0, q; p < s.size(); p = q+1) {
                q = s.find(';', p); if (q == std::string::npos) q = s.size();
                std::string g = s.substr(p, q-p);
                while (!g.empty() && (uint8_t)g.back() <= 0x20) g.pop_back();
                if (g == "DB:Admin")  u.db_admin  = true;
                if (g == "DB:Backup") u.db_backup = true;
                if (g == "DB:Debug")  u.db_debug  = true;
            }
        }
    }
    AdsDisconnect(hConn);
}

// ---------------------------------------------------------------------------
// Plaintext checker
// ---------------------------------------------------------------------------
// Returns true if the 16-byte block looks like a valid plaintext for
// the given expected membership bits.
static bool check_pt(const uint8_t pt[16], uint8_t expected_bits,
                      const char** pat_name) {
    // Pattern A: bits in byte 0, rest zeros
    if (pt[0] == expected_bits) {
        bool rest_zero = true;
        for (int i = 1; i < 16; ++i) if (pt[i] != 0) { rest_zero = false; break; }
        if (rest_zero) { *pat_name = "A:bits[0],rest=0"; return true; }
    }
    // Pattern B: bits in byte 0, remaining bytes identical (repeated)
    if (pt[0] == expected_bits) {
        // check all same value in bytes 1-15
        bool all_same = true;
        for (int i = 2; i < 16; ++i) if (pt[i] != pt[1]) { all_same = false; break; }
        if (all_same && pt[1] != expected_bits) {
            *pat_name = "B:bits[0],rest=const"; return true;
        }
    }
    // Pattern C: bits in byte 3 (BE-like), rest zeros
    if (pt[3] == expected_bits) {
        bool ok = (pt[0]==0 && pt[1]==0 && pt[2]==0);
        for (int i = 4; i < 16 && ok; ++i) if (pt[i] != 0) ok = false;
        if (ok) { *pat_name = "C:bits[3],rest=0(BE)"; return true; }
    }
    // Pattern D: bits in byte 4, rest zeros
    if (pt[4] == expected_bits) {
        bool ok = (pt[0]==0&&pt[1]==0&&pt[2]==0&&pt[3]==0);
        for (int i = 5; i < 16 && ok; ++i) if (pt[i] != 0) ok = false;
        if (ok) { *pat_name = "D:bits[4],rest=0"; return true; }
    }
    // Pattern E: bits in byte 7, rest zeros
    if (pt[7] == expected_bits) {
        bool ok = true;
        for (int i = 0; i < 7 && ok; ++i) if (pt[i] != 0) ok = false;
        for (int i = 8; i < 16 && ok; ++i) if (pt[i] != 0) ok = false;
        if (ok) { *pat_name = "E:bits[7],rest=0"; return true; }
    }
    // Pattern F: inverted bits in byte 0 (~bits), rest 0xFF
    if (pt[0] == (uint8_t)~expected_bits) {
        bool ok = true;
        for (int i = 1; i < 16 && ok; ++i) if (pt[i] != 0xFF) ok = false;
        if (ok) { *pat_name = "F:~bits[0],rest=FF"; return true; }
    }
    // Pattern G: for block0/block1 independently: block0 has bits, block1 has bits too
    if (pt[0] == expected_bits && pt[8] == expected_bits) {
        bool ok = true;
        for (int i = 1; i < 8 && ok; ++i) if (pt[i] != 0) ok = false;
        for (int i = 9; i < 16 && ok; ++i) if (pt[i] != 0) ok = false;
        if (ok) { *pat_name = "G:bits[0+8],rest=0"; return true; }
    }
    // Pattern H: any byte position equals bits, everything else is zero
    for (int pos = 0; pos < 16; ++pos) {
        if (pt[pos] == expected_bits && expected_bits != 0) {
            bool ok = true;
            for (int i = 0; i < 16 && ok; ++i)
                if (i != pos && pt[i] != 0) ok = false;
            if (ok) {
                static char pbuf[32];
                sprintf_s(pbuf, "H:bits[%d],rest=0", pos);
                *pat_name = pbuf;
                return true;
            }
        }
    }
    // Pattern I: low entropy (at least 12 of 16 bytes are the same value)
    {
        uint8_t counts[256] = {};
        for (int i = 0; i < 16; ++i) counts[pt[i]]++;
        uint8_t mx = *std::max_element(counts, counts+256);
        if (mx >= 12) { *pat_name = "I:low-entropy"; return true; }
    }
    // Pattern J: two halves identical
    if (memcmp(pt, pt+8, 8) == 0) {
        *pat_name = "J:halves-equal"; return true;
    }
    // Pattern K: recognizable constant (all same byte)
    {
        bool all_same = true;
        for (int i = 1; i < 16; ++i) if (pt[i] != pt[0]) { all_same = false; break; }
        if (all_same) { *pat_name = "K:constant-byte"; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Key generation for a user
// ---------------------------------------------------------------------------
struct KeyEntry {
    std::string label;
    Key8 k;
};
struct Key16Entry {
    std::string label;
    Key16 k;
};

static std::vector<KeyEntry> make_keys(const User& u, const char* db_pw) {
    std::vector<KeyEntry> keys;
    auto add = [&](std::string lbl, Key8 k) { keys.push_back({std::move(lbl), k}); };

    // Username-based keys
    add("user-name[0..7] NUL-pad",  pad8(u.name, 0x00));
    add("user-name[0..7] SPC-pad",  pad8(u.name, 0x20));
    add("user-name[0..7] 0xFF-pad", pad8(u.name, 0xFF));
    {
        std::string rev = u.name; std::reverse(rev.begin(), rev.end());
        add("user-name-reversed NUL-pad", pad8(rev, 0x00));
    }
    // Right-aligned (last 8 chars)
    if (u.name.size() > 8) {
        std::string tail = u.name.substr(u.name.size()-8);
        add("user-name[-8..] NUL-pad", pad8(tail, 0x00));
    }
    // Uppercase name
    {
        std::string up = u.name;
        for (auto& c : up) c = (char)toupper((unsigned char)c);
        add("user-name-upper NUL-pad", pad8(up, 0x00));
        add("user-name-upper SPC-pad", pad8(up, 0x20));
    }

    // OID-based keys
    add("OID LE×2",         from_oid_le(u.oid));
    add("OID BE×2",         from_oid_be(u.oid));
    {
        Key8 k{};
        k[0]=(uint8_t)u.oid; k[1]=(uint8_t)(u.oid>>8);
        k[2]=(uint8_t)(u.oid>>16); k[3]=(uint8_t)(u.oid>>24);
        add("OID LE + 0x00", k);
    }
    {
        Key8 k{};
        k[0]=(uint8_t)(u.oid>>24); k[1]=(uint8_t)(u.oid>>16);
        k[2]=(uint8_t)(u.oid>>8);  k[3]=(uint8_t)u.oid;
        add("OID BE + 0x00", k);
    }
    // OID XOR username[0..3]
    {
        Key8 k = from_oid_le(u.oid);
        for (int i = 0; i < 4 && i < (int)u.name.size(); ++i)
            k[i] ^= (uint8_t)u.name[i];
        add("OID-LE XOR username[0..3]", k);
    }

    // prop273 bytes as key
    if (u.prop273.size() >= 8) {
        Key8 k;
        memcpy(k.data(), u.prop273.data(), 8);
        add("prop273[0..7]", k);
    }
    if (u.prop273.size() >= 16) {
        Key8 k;
        memcpy(k.data(), u.prop273.data()+8, 8);
        add("prop273[8..15]", k);
    }
    if (u.plen >= 8 && u.plen < 273) {
        // Last 8 bytes of password hash (just before cf field)
        Key8 k;
        memcpy(k.data(), u.prop273.data() + u.plen - 8, 8);
        add("prop273[plen-8..plen-1]", k);
    }
    if (u.plen >= 16 && u.plen < 273) {
        // Middle of hash
        Key8 k;
        memcpy(k.data(), u.prop273.data() + (u.plen/2 - 4), 8);
        add("prop273[plen/2-4..+7]", k);
    }

    // info1/info2 based
    {
        Key8 k{};
        k[0]=(uint8_t)u.info1; k[1]=(uint8_t)(u.info1>>8);
        k[2]=(uint8_t)(u.info1>>16); k[3]=(uint8_t)(u.info1>>24);
        k[4]=(uint8_t)u.info2; k[5]=(uint8_t)(u.info2>>8);
        k[6]=(uint8_t)(u.info2>>16); k[7]=(uint8_t)(u.info2>>24);
        add("info1|info2 LE", k);
    }

    // DB password
    add("DB-pw NUL-pad", pad8(std::string(db_pw), 0x00));
    add("DB-pw SPC-pad", pad8(std::string(db_pw), 0x20));
    // DB password XOR username
    {
        Key8 k = pad8(std::string(db_pw), 0x00);
        for (int i = 0; i < 8 && i < (int)u.name.size(); ++i)
            k[i] ^= (uint8_t)u.name[i];
        add("DB-pw XOR username", k);
    }
    // DB password XOR OID
    {
        Key8 k = pad8(std::string(db_pw), 0x00);
        k[0]^=(uint8_t)u.oid;       k[1]^=(uint8_t)(u.oid>>8);
        k[2]^=(uint8_t)(u.oid>>16); k[3]^=(uint8_t)(u.oid>>24);
        add("DB-pw XOR OID-LE", k);
    }

    // Fixed well-known strings
    add("\"adssys  \"", pad8("adssys", 0x20));
    add("\"adssys\\0\"", pad8("adssys", 0x00));
    add("\"ADSSYS  \"", pad8("ADSSYS", 0x20));
    // All-zeros, all-ones
    { Key8 k; k.fill(0x00); add("all-zeros", k); }
    { Key8 k; k.fill(0xFF); add("all-FF", k); }
    // First 8 of cipher itself (degenerate check)
    if (u.has_cipher) {
        Key8 k; memcpy(k.data(), u.cipher, 8);
        add("cipher[0..7] as key (self)", k);
    }

    return keys;
}

static std::vector<Key16Entry> make_keys16(const User& u, const char* db_pw) {
    std::vector<Key16Entry> keys16;
    auto add = [&](std::string lbl, Key16 k) { keys16.push_back({std::move(lbl), k}); };

    add("username NUL-pad to 16", pad16(u.name, 0x00));
    add("username SPC-pad to 16", pad16(u.name, 0x20));
    if (u.prop273.size() >= 16) {
        Key16 k; memcpy(k.data(), u.prop273.data(), 16);
        add("prop273[0..15]", k);
    }
    if (u.plen >= 16 && u.plen < 273) {
        Key16 k; memcpy(k.data(), u.prop273.data() + u.plen - 16, 16);
        add("prop273[plen-16..plen-1]", k);
    }
    // OID ×4
    { Key16 k;
      for (int i = 0; i < 4; ++i) {
          k[i*4+0]=(uint8_t)u.oid; k[i*4+1]=(uint8_t)(u.oid>>8);
          k[i*4+2]=(uint8_t)(u.oid>>16); k[i*4+3]=(uint8_t)(u.oid>>24);
      }
      add("OID-LE ×4 (16 bytes)", k);
    }
    // DB-pw padded to 16
    add("DB-pw NUL-pad to 16", pad16(std::string(db_pw), 0x00));
    // DB-pw XOR username, 16 bytes
    {
        Key16 k = pad16(std::string(db_pw), 0x00);
        for (int i = 0; i < 16 && i < (int)u.name.size(); ++i) k[i] ^= (uint8_t)u.name[i];
        add("DB-pw XOR username (16)", k);
    }

    return keys16;
}

// ---------------------------------------------------------------------------
// XOR pair analysis
// ---------------------------------------------------------------------------
static void xor_analysis(const std::vector<User>& users) {
    printf("\n=== XOR pair analysis (users with cipher, same DB) ===\n");
    printf("%-16s ^ %-16s = XOR-of-ciphers  (bits_A=%d bits_B=%d)\n",
           "UserA", "UserB", 0, 0);
    printf("%s\n", std::string(80, '-').c_str());

    for (size_t a = 0; a < users.size(); ++a) {
        if (!users[a].has_cipher) continue;
        for (size_t b = a+1; b < users.size(); ++b) {
            if (!users[b].has_cipher) continue;
            const auto& ua = users[a];
            const auto& ub = users[b];
            uint8_t x[16];
            for (int i = 0; i < 16; ++i) x[i] = ua.cipher[i] ^ ub.cipher[i];
            printf("%-16s ^ %-16s [bits:%X ^ %X] XOR: ",
                   ua.name.c_str(), ub.name.c_str(), ua.bits(), ub.bits());
            for (int i = 0; i < 16; ++i) printf("%02X ", x[i]);
            printf("\n");
        }
    }
}

// ---------------------------------------------------------------------------
// Main cracking loop
// ---------------------------------------------------------------------------
static void prop273_analysis(const std::vector<User>& users);  // forward decl

static int crack_db(const char* path, const char* pw) {
    printf("\n");
    printf("========================================\n");
    printf("DB: %s  (pw=%s)\n", path, pw);
    printf("========================================\n");

    auto users = read_add_binary(path);
    if (users.empty()) { printf("No users found.\n"); return 1; }
    printf("Binary read: %zu user records.\n", users.size());

    query_sap(path, pw, users);

    // Summary table
    printf("\n--- Cipher summary ---\n");
    printf("%-20s OID       Mbr  HasCipher  Cipher (16 bytes)\n", "Username");
    printf("%s\n", std::string(90, '-').c_str());
    for (const auto& u : users) {
        printf("%-20s %08X  %4s  %s  ",
               u.name.c_str(), u.oid, u.bits_str(),
               u.has_cipher ? "YES" : " no");
        if (u.has_cipher)
            for (int i = 0; i < 16; ++i) printf("%02X ", u.cipher[i]);
        else
            printf("(no block)");
        printf("\n");
    }

    // XOR pair analysis
    xor_analysis(users);

    // DES cracking
    printf("\n--- DES-ECB / 3DES-ECB key search ---\n");
    bool any_hit = false;
    for (const auto& u : users) {
        if (!u.has_cipher) continue;

        auto keys   = make_keys(u, pw);
        auto keys16 = make_keys16(u, pw);
        uint8_t expected = u.bits();

        // DES-ECB
        for (const auto& ke : keys) {
            uint8_t pt[16];
            // Try decrypting full 16 bytes as two ECB blocks
            uint8_t pt0[8], pt1[8];
            if (!des_ecb_dec(ke.k.data(), u.cipher+0, pt0)) continue;
            if (!des_ecb_dec(ke.k.data(), u.cipher+8, pt1)) continue;
            memcpy(pt, pt0, 8); memcpy(pt+8, pt1, 8);
            const char* pat = nullptr;
            if (check_pt(pt, expected, &pat)) {
                printf("HIT  user=%-16s key=%-36s algo=DES-ECB-2x8  pat=%s\n",
                       u.name.c_str(), ke.label.c_str(), pat);
                printhex("  key  ", ke.k.data(), 8);
                printhex("  plain", pt, 16);
                any_hit = true;
            }
        }

        // DES-ECB treating all 16 bytes as ONE block — not standard DES but
        // we check if block 0 alone reveals structure
        for (const auto& ke : keys) {
            uint8_t pt0[8];
            if (!des_ecb_dec(ke.k.data(), u.cipher, pt0)) continue;
            // check just first half
            const char* pat = nullptr;
            uint8_t pt16[16] = {};
            memcpy(pt16, pt0, 8);
            if (check_pt(pt16, expected, &pat)) {
                printf("HIT  user=%-16s key=%-36s algo=DES-ECB-block0  pat=%s\n",
                       u.name.c_str(), ke.label.c_str(), pat);
                printhex("  key  ", ke.k.data(), 8);
                printhex("  plain", pt0, 8);
                any_hit = true;
            }
        }

        // DES-CBC (IV=all zeros)
        for (const auto& ke : keys) {
            uint8_t pt[16];
            const uint8_t iv[8] = {};
            if (!des_cbc_dec(ke.k.data(), iv, u.cipher, pt)) continue;
            const char* pat = nullptr;
            if (check_pt(pt, expected, &pat)) {
                printf("HIT  user=%-16s key=%-36s algo=DES-CBC-IV0  pat=%s\n",
                       u.name.c_str(), ke.label.c_str(), pat);
                printhex("  key  ", ke.k.data(), 8);
                printhex("  plain", pt, 16);
                any_hit = true;
            }
        }
        // DES-CBC with IV = cipher[0..7] (internal chaining variant)
        for (const auto& ke : keys) {
            uint8_t pt[16];
            uint8_t pt0[8];
            if (!des_ecb_dec(ke.k.data(), u.cipher+8, pt0)) continue;
            // block1 XOR with cipher[0..7]
            uint8_t pt1[8];
            for (int i = 0; i < 8; ++i) pt1[i] = pt0[i] ^ u.cipher[i];
            memcpy(pt, u.cipher, 8); memcpy(pt+8, pt1, 8); // block0 = pt0 placeholder
            // check just second block
            const char* pat = nullptr;
            uint8_t tmp[16] = {};
            memcpy(tmp, pt1, 8);
            if (check_pt(tmp, expected, &pat)) {
                printf("HIT  user=%-16s key=%-36s algo=DES-CBC-IV=ct0(block1)  pat=%s\n",
                       u.name.c_str(), ke.label.c_str(), pat);
                any_hit = true;
            }
        }

        // 3DES-ECB (16-byte key)
        for (const auto& ke : keys16) {
            uint8_t pt[16];
            if (!tdes_ecb_dec(ke.k.data(), u.cipher, 16, pt)) continue;
            const char* pat = nullptr;
            if (check_pt(pt, expected, &pat)) {
                printf("HIT  user=%-16s key=%-36s algo=3DES-ECB  pat=%s\n",
                       u.name.c_str(), ke.label.c_str(), pat);
                printhex("  key  ", ke.k.data(), 16);
                printhex("  plain", pt, 16);
                any_hit = true;
            }
        }

        // Simple XOR stream: key = repeating bytes from various sources
        // If cipher = plaintext XOR repeating(key_byte), then
        // plaintext = cipher XOR repeating(key_byte)
        // Test with every possible single byte as key
        {
            for (int kb = 0; kb < 256; ++kb) {
                uint8_t pt[16];
                for (int i = 0; i < 16; ++i) pt[i] = u.cipher[i] ^ (uint8_t)kb;
                const char* pat = nullptr;
                if (check_pt(pt, expected, &pat)) {
                    printf("HIT  user=%-16s key=0x%02X (single-byte XOR)  pat=%s\n",
                           u.name.c_str(), kb, pat);
                    printhex("  plain", pt, 16);
                    any_hit = true;
                }
            }
        }
    }

    if (!any_hit)
        printf("No DES key match found for any key candidate or plaintext pattern.\n");

    prop273_analysis(users);
    return 0;
}

// ---------------------------------------------------------------------------
// Header dump (look for embedded key material in .add file header)
// ---------------------------------------------------------------------------
static void dump_header(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return;
    std::vector<uint8_t> buf(2200);
    f.read((char*)buf.data(), 2200);
    size_t n = (size_t)f.gcount();
    if (n < 0x28) return;

    uint32_t hdr_len = le32(buf.data(), 0x20);
    uint32_t rec_len = le32(buf.data(), 0x24);
    printf("Header: hdr_len=%u  rec_len=%u\n", hdr_len, rec_len);

    // Look for non-trivial 8-byte windows in the header that could be DES keys
    // A DES key candidate is "interesting" if it has high bit variety (not all same)
    printf("Interesting 8-byte windows in header (non-trivial bytes, offsets 0x28..0x%X):\n",
           (unsigned)std::min(n, (size_t)hdr_len));
    for (size_t off = 0x28; off + 8 <= std::min(n, (size_t)hdr_len); ++off) {
        const uint8_t* p = buf.data() + off;
        // count distinct bytes
        std::set<uint8_t> distinct(p, p+8);
        uint8_t lo = *std::min_element(p, p+8);
        uint8_t hi = *std::max_element(p, p+8);
        if (distinct.size() >= 4 && lo != 0x00 && lo != 0xFF && hi != 0x00) {
            printf("  [0x%04zX]: ", off);
            for (int i = 0; i < 8; ++i) printf("%02X ", p[i]);
            printf("(distinct=%zu range=%02X..%02X)\n",
                   distinct.size(), lo, hi);
        }
    }
}

// ---------------------------------------------------------------------------
// Cross-DB analysis: compare ciphers for same username across databases
// ---------------------------------------------------------------------------
static void cross_db_analysis(const std::vector<std::pair<std::string, std::vector<User>>>& dbs) {
    printf("\n=== Cross-DB analysis (same username, same membership, different ciphers) ===\n");
    // Collect all user names seen across DBs
    std::set<std::string> all_names;
    for (const auto& db : dbs)
        for (const auto& u : db.second)
            if (u.has_cipher) all_names.insert(u.name);

    for (const auto& name : all_names) {
        std::vector<std::pair<std::string, const User*>> found;
        for (const auto& db : dbs)
            for (const auto& u : db.second)
                if (u.name == name && u.has_cipher)
                    found.push_back({db.first, &u});
        if (found.size() < 2) continue;
        printf("User \"%s\":\n", name.c_str());
        for (auto& [db, pu] : found) {
            printf("  [%s] OID=%08X  bits=%X  cipher: ", db.c_str(), pu->oid, pu->bits());
            for (int i = 0; i < 16; ++i) printf("%02X ", pu->cipher[i]);
            printf("\n");
        }
        // XOR across databases
        for (size_t a = 0; a < found.size(); ++a)
            for (size_t b = a+1; b < found.size(); ++b) {
                uint8_t x[16];
                for (int i = 0; i < 16; ++i)
                    x[i] = found[a].second->cipher[i] ^ found[b].second->cipher[i];
                printf("  XOR [%s ^ %s]: ", found[a].first.c_str(), found[b].first.c_str());
                for (int i = 0; i < 16; ++i) printf("%02X ", x[i]);
                printf("\n");
            }
    }
}

// ---------------------------------------------------------------------------
// Hash utilities for prop273 analysis
// ---------------------------------------------------------------------------
static uint32_t crc32_compute(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}
static uint32_t fnv32(const uint8_t* data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; ++i) h = (h ^ data[i]) * 16777619u;
    return h;
}
static uint32_t djb2(const uint8_t* data, size_t len) {
    uint32_t h = 5381;
    for (size_t i = 0; i < len; ++i) h = h * 33 + data[i];
    return h;
}
static uint32_t sdbm(const uint8_t* data, size_t len) {
    uint32_t h = 0;
    for (size_t i = 0; i < len; ++i) h = data[i] + h * 65599u;
    return h;
}
static uint32_t murmur_fmix32(uint32_t h) {
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}
static uint32_t lcg_hash(uint32_t v) {
    // Knuth multiplicative hash
    return v * 2654435761u;
}

// ---------------------------------------------------------------------------
// Prop273 / hash analysis
// ---------------------------------------------------------------------------
static void prop273_analysis(const std::vector<User>& users) {
    printf("\n=== prop273 / OID hash analysis ===\n");

    for (const auto& u : users) {
        if (!u.has_cipher) continue;
        if (u.prop273.size() < 273) continue;

        // What we know:
        //   cipher[4..7]   = f(OID) — constant per OID, independent of membership
        //   cipher[8..11]  = keystream(OID) XOR membership_delta(bits)
        //   membership_delta: Admin=(00,00,00,20) Backup=(01,00,00,00) Debug=(06,00,01,00)
        uint8_t c47[4]; memcpy(c47, u.cipher+4, 4);
        uint8_t c811[4]; memcpy(c811, u.cipher+8, 4);

        // Derive keystream[8-11] from known membership
        uint8_t ks[4]; memcpy(ks, c811, 4);
        if (u.db_admin)  ks[3] ^= 0x20;
        if (u.db_backup) ks[0] ^= 0x01;
        if (u.db_debug) { ks[0] ^= 0x06; ks[2] ^= 0x01; }

        printf("\nUser %-16s OID=%08X bits=%X plen=%u\n",
               u.name.c_str(), u.oid, u.bits(), u.plen);
        printf("  cipher[4-7]:    "); for (int i=0;i<4;i++) printf("%02X ",c47[i]); printf("\n");
        printf("  cipher[8-11]:   "); for (int i=0;i<4;i++) printf("%02X ",c811[i]); printf("\n");
        printf("  keystream[8-11]:"); for (int i=0;i<4;i++) printf("%02X ",ks[i]); printf("\n");

        // Hypothesis A: bytes 4-7 appear somewhere in prop273
        {
            bool found47 = false;
            for (size_t j = 0; j + 4 <= 273; ++j) {
                if (memcmp(u.prop273.data()+j, c47, 4) == 0) {
                    printf("  ** MATCH: cipher[4-7] found at prop273[%zu]\n", j);
                    found47 = true;
                }
            }
            if (!found47) printf("  cipher[4-7] NOT found in prop273\n");
        }
        // Hypothesis B: keystream[8-11] appears somewhere in prop273
        {
            bool foundks = false;
            for (size_t j = 0; j + 4 <= 273; ++j) {
                if (memcmp(u.prop273.data()+j, ks, 4) == 0) {
                    printf("  ** MATCH: keystream[8-11] found at prop273[%zu]\n", j);
                    foundks = true;
                }
            }
            if (!foundks) printf("  keystream[8-11] NOT found in prop273\n");
        }

        // Hypothesis C: bytes 4-11 = 8 bytes at some fixed offset in prop273
        {
            bool found8 = false;
            for (size_t j = 0; j + 8 <= 273; ++j) {
                if (memcmp(u.prop273.data()+j, u.cipher+4, 4) == 0 &&
                    memcmp(u.prop273.data()+j+4, ks, 4) == 0) {
                    printf("  ** MATCH: full bytes[4-11] at prop273[%zu]\n", j);
                    found8 = true;
                }
            }
            if (!found8) printf("  bytes[4-11] as 8-block NOT found in prop273\n");
        }

        // Hypothesis D: bytes 4-7 = hash(OID)
        // Try multiple hash functions of the OID (both LE and BE byte representations)
        uint8_t oid_le[4] = {(uint8_t)u.oid,(uint8_t)(u.oid>>8),(uint8_t)(u.oid>>16),(uint8_t)(u.oid>>24)};
        uint8_t oid_be[4] = {(uint8_t)(u.oid>>24),(uint8_t)(u.oid>>16),(uint8_t)(u.oid>>8),(uint8_t)u.oid};
        uint8_t oid8_le[8] = {}; memcpy(oid8_le, oid_le, 4);
        uint8_t oid8_be[8] = {}; memcpy(oid8_be, oid_be, 4);

        struct HashCandidate {
            const char* name;
            uint32_t val;
        };
        std::vector<HashCandidate> hashes = {
            {"crc32(oid_le)",    crc32_compute(oid_le,4)},
            {"crc32(oid_be)",    crc32_compute(oid_be,4)},
            {"crc32(oid8_le)",   crc32_compute(oid8_le,8)},
            {"fnv32(oid_le)",    fnv32(oid_le,4)},
            {"fnv32(oid_be)",    fnv32(oid_be,4)},
            {"djb2(oid_le)",     djb2(oid_le,4)},
            {"djb2(oid_be)",     djb2(oid_be,4)},
            {"sdbm(oid_le)",     sdbm(oid_le,4)},
            {"sdbm(oid_be)",     sdbm(oid_be,4)},
            {"murmur_fmix(oid)", murmur_fmix32(u.oid)},
            {"lcg(oid)",         lcg_hash(u.oid)},
            {"lcg(oid) rot16",   (lcg_hash(u.oid)>>16)|(lcg_hash(u.oid)<<16)},
        };

        // Convert c47 to LE uint32
        uint32_t target47_le = (uint32_t)c47[0]|((uint32_t)c47[1]<<8)|((uint32_t)c47[2]<<16)|((uint32_t)c47[3]<<24);
        uint32_t target47_be = (uint32_t)c47[3]|((uint32_t)c47[2]<<8)|((uint32_t)c47[1]<<16)|((uint32_t)c47[0]<<24);

        bool any_hash_match = false;
        for (const auto& h : hashes) {
            if (h.val == target47_le || h.val == target47_be) {
                printf("  ** HASH MATCH: %s = %08X matches cipher[4-7] (LE=%08X BE=%08X)\n",
                       h.name, h.val, target47_le, target47_be);
                any_hash_match = true;
            }
        }
        if (!any_hash_match) {
            printf("  No hash match for cipher[4-7] (target LE=%08X)\n", target47_le);
        }

        // Dump the FULL post-token section to see what precedes the cipher
        if (u.plen != 0xFFFFu && u.plen < 273) {
            // Recompute post_off correctly with integer-division ntok
            uint16_t cf2 = le16(u.prop273.data(), u.plen);
            uint32_t ntok2 = (cf2 == 0xFFFFu || cf2 == 0) ? 0 : cf2 / 4u;
            size_t post = u.plen + 2 + ntok2 * 4;
            printf("  plen=%u  cf=%u  ntok=%u  post_off=%zu\n", u.plen, cf2, ntok2, post);
            // Print full post-token section (up to cipher end or 273)
            size_t cipher_end = u.post_off + 2 + 16;  // approximate
            size_t dump_end = std::min((size_t)273, cipher_end + 4);
            printf("  post-token bytes [%zu..%zu]:", post, dump_end);
            for (size_t j = post; j < dump_end && j < u.prop273.size(); ++j)
                printf(" %02X", u.prop273[j]);
            printf("\n");
            // Also show first 16 bytes of prop273 (password hash start)
            printf("  prop273[0..15] (hash start): ");
            for (size_t j = 0; j < 16 && j < u.prop273.size(); ++j)
                printf(" %02X", u.prop273[j]);
            printf("\n");
            // And last 8 bytes of password hash
            if (u.plen >= 8) {
                printf("  prop273[%u..%u] (hash end): ", u.plen-8, u.plen-1);
                for (size_t j = u.plen-8; j < (size_t)u.plen; ++j)
                    printf(" %02X", u.prop273[j]);
                printf("\n");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Known (OID, f||g) pair for cipher analysis
// ---------------------------------------------------------------------------
struct KnownPair {
    uint32_t oid;
    uint8_t fg[8]; // cipher[4..7] || keystream[8..11] with membership bits stripped
};

// ---------------------------------------------------------------------------
// BCrypt hash helper — compute hash of arbitrary data, return in out[0..out_len-1]
// ---------------------------------------------------------------------------
static bool bcrypt_hash(const wchar_t* alg_name, const uint8_t* data, size_t data_len,
                         uint8_t* out, size_t out_len) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, alg_name, nullptr, 0)))
        return false;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    DWORD obj_sz = 0, hash_sz = 0, res = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&obj_sz, sizeof(DWORD), &res, 0);
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH,   (PUCHAR)&hash_sz, sizeof(DWORD), &res, 0);
    std::vector<uint8_t> obj(obj_sz);
    bool ok = BCRYPT_SUCCESS(BCryptCreateHash(hAlg, &hHash, obj.data(), obj_sz, nullptr, 0, 0));
    if (ok) ok = BCRYPT_SUCCESS(BCryptHashData(hHash, (PUCHAR)data, (ULONG)data_len, 0));
    std::vector<uint8_t> hash(hash_sz, 0);
    if (ok) ok = BCRYPT_SUCCESS(BCryptFinishHash(hHash, hash.data(), hash_sz, 0));
    if (hHash) BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    if (ok) { size_t cp = std::min(out_len, (size_t)hash_sz); memcpy(out, hash.data(), cp); }
    return ok;
}

// ---------------------------------------------------------------------------
// Create fresh test database and populate with N users to collect consecutive
// (OID → f, g) pairs for differential analysis.
// All new users have bits=0, so cipher[4-7]=f(OID) and cipher[8-11]=g(OID).
// ---------------------------------------------------------------------------
static std::vector<KnownPair> collect_consecutive_pairs(int n_users,
                                                          const char* base_add,
                                                          const char* pw = "") {
    // Connect directly to the live test database, add users, read back, then delete them.
    ADSHANDLE hDBC = 0;
    // Must use REMOTE server so it writes cipher blocks (LOCAL server skips them)
    UNSIGNED32 rc = AdsConnect60((UNSIGNED8*)base_add, ADS_REMOTE_SERVER,
                                  (UNSIGNED8*)"adssys", (UNSIGNED8*)pw, 0, &hDBC);
    if (rc != 0) {
        printf("  REMOTE connect to %s failed: %u — cipher blocks won't be written by local server\n", base_add, rc);
        // Fallback: try local anyway (pairs will be empty but at least diagnose)
        rc = AdsConnect60((UNSIGNED8*)base_add, ADS_LOCAL_SERVER,
                           (UNSIGNED8*)"adssys", (UNSIGNED8*)pw, 0, &hDBC);
        if (rc != 0) { printf("  LOCAL connect also failed: %u\n", rc); return {}; }
    }

    printf("  Creating %d test users in %s...\n", n_users, base_add);
    int created = 0;
    for (int i = 1; i <= n_users; i++) {
        char uname[32]; sprintf(uname, "crack_%04d", i);
        UNSIGNED32 crc = AdsDDCreateUser(hDBC, nullptr, (UNSIGNED8*)uname, (UNSIGNED8*)"x", nullptr);
        if (crc == 0) created++;
        else if (i == 1) printf("  AdsDDCreateUser failed: %u\n", crc);
    }
    printf("  Created %d users.\n", created);
    AdsDisconnect(hDBC);

    // Read binary — only the new crack_ users matter
    auto users = read_add_binary(base_add);
    printf("  read_add_binary found %zu records total.\n", users.size());
    int crack_found = 0;
    for (const auto& u : users) {
        bool is_crack = u.name.size() >= 6 && u.name.substr(0,6) == "crack_";
        if (is_crack) { crack_found++; if (crack_found <= 3) printf("    crack user: '%s' oid=%04X has_cipher=%d\n", u.name.c_str(), u.oid, (int)u.has_cipher); }
    }
    printf("  Found %d crack_ users (%d with cipher).\n", crack_found,
           (int)std::count_if(users.begin(), users.end(), [](const User& u){ return u.has_cipher && u.name.size()>=6 && u.name.substr(0,6)=="crack_"; }));

    std::vector<KnownPair> out;
    for (const auto& u : users) {
        if (!u.has_cipher) continue;
        if (u.name.size() < 6 || u.name.substr(0,6) != "crack_") continue;
        KnownPair p;
        p.oid = u.oid;
        memcpy(p.fg,   u.cipher + 4, 4);
        memcpy(p.fg+4, u.cipher + 8, 4);
        out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [](const KnownPair& a, const KnownPair& b){ return a.oid < b.oid; });
    printf("  Collected %zu new pairs (OID range 0x%04X..0x%04X)\n",
           out.size(), out.empty()?0:out.front().oid, out.empty()?0:out.back().oid);

    // Cleanup: delete the crack_ users so the test db stays clean
    if (!out.empty()) {
        ADSHANDLE hDBC2 = 0;
        if (AdsConnect60((UNSIGNED8*)base_add, ADS_REMOTE_SERVER,
                         (UNSIGNED8*)"adssys", (UNSIGNED8*)pw, 0, &hDBC2) == 0) {
            int deleted = 0;
            for (const auto& u : users) {
                if (u.name.size() >= 6 && u.name.substr(0,6) == "crack_") {
                    if (AdsDDDeleteUser(hDBC2, (UNSIGNED8*)u.name.c_str()) == 0)
                        deleted++;
                }
            }
            AdsDisconnect(hDBC2);
            printf("  Cleaned up %d test users.\n", deleted);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Xorshift variants applied to OID
// ---------------------------------------------------------------------------
static void test_xorshift(const std::vector<KnownPair>& pairs) {
    printf("\n=== Xorshift / LFSR tests ===\n");
    bool found = false;

    // xorshift32 once: f = result, g = xorshift32(f)
    auto xs32 = [](uint32_t x, int a, int b, int c) -> uint32_t {
        x ^= x << a; x ^= x >> b; x ^= x << c; return x;
    };
    // common xorshift32 parameter sets
    int params[][3] = {{13,17,5},{13,7,17},{17,13,5},{5,17,13},{7,13,17},
                       {13,17,15},{15,17,13},{11,19,7},{7,19,11},{9,14,6}};
    for (auto& p : params) {
        bool all_f = true, all_fg = true;
        for (const auto& kp : pairs) {
            uint32_t f = xs32(kp.oid, p[0], p[1], p[2]);
            if (memcmp(&f, kp.fg, 4) != 0) all_f = false;
            uint32_t g = xs32(f, p[0], p[1], p[2]);
            uint8_t fg[8]; memcpy(fg, &f, 4); memcpy(fg+4, &g, 4);
            if (memcmp(fg, kp.fg, 8) != 0) all_fg = false;
        }
        if (all_f)  { printf("  ** MATCH: xorshift32(%d,%d,%d) → f!\n", p[0],p[1],p[2]); found=true; }
        if (all_fg) { printf("  ** MATCH: xorshift32(%d,%d,%d) → f, xorshift32(f) → g!\n", p[0],p[1],p[2]); found=true; }
    }
    // xorshift64: seed=oid, one step → f=hi32, g=lo32
    auto xs64 = [](uint64_t x, int a, int b, int c) -> uint64_t {
        x ^= x << a; x ^= x >> b; x ^= x << c; return x;
    };
    int params64[][3] = {{13,7,17},{17,31,8},{21,35,4},{20,41,5},{12,25,27}};
    for (auto& p : params64) {
        bool all_hi = true, all_lo = true, all_fg = true;
        for (const auto& kp : pairs) {
            uint64_t h = xs64((uint64_t)kp.oid, p[0], p[1], p[2]);
            uint32_t hi = (uint32_t)(h >> 32), lo = (uint32_t)h;
            if (memcmp(&hi, kp.fg, 4) != 0) all_hi = false;
            if (memcmp(&lo, kp.fg, 4) != 0) all_lo = false;
            uint8_t fg_hl[8]; memcpy(fg_hl, &hi, 4); memcpy(fg_hl+4, &lo, 4);
            uint8_t fg_lh[8]; memcpy(fg_lh, &lo, 4); memcpy(fg_lh+4, &hi, 4);
            if (memcmp(fg_hl, kp.fg, 8) != 0 && memcmp(fg_lh, kp.fg, 8) != 0) all_fg = false;
        }
        if (all_fg) { printf("  ** MATCH: xorshift64(%d,%d,%d) hi/lo → fg!\n", p[0],p[1],p[2]); found=true; }
    }
    // Also test: f = mix(oid), g = mix(oid ^ C) for some constant C
    // Try C being byte patterns
    uint32_t C_vals[] = {0x12345678, 0xDEADBEEF, 0xCAFEBABE, 0x9E3779B9,
                         0x6C62272E, 0x9B04BC6B, 0xA0761D65, 0xE7037ED1};
    // For common mixing: test f = mix(oid), g = mix(oid+1) or mix(oid^CONST)
    auto wang = [](uint32_t a) -> uint32_t {
        a = (a^0x61C88647) + (a<<3); a ^= a>>11; a += a<<15; return a;
    };
    for (uint32_t C : C_vals) {
        bool all_match = true;
        for (const auto& kp : pairs) {
            uint32_t f = wang(kp.oid);
            uint32_t g = wang(kp.oid ^ C);
            uint8_t fg[8]; memcpy(fg, &f, 4); memcpy(fg+4, &g, 4);
            if (memcmp(fg, kp.fg, 8) != 0) { all_match = false; break; }
        }
        if (all_match) { printf("  ** MATCH: wang(oid)||wang(oid^0x%08X) → fg!\n", C); found=true; }
    }
    if (!found) printf("  No xorshift/LFSR match.\n");
}

// ---------------------------------------------------------------------------
// Scan DLL for custom 256-byte S-boxes (bijective permutations)
// If found, try using them in a simple substitution cipher for OID→fg
// ---------------------------------------------------------------------------
static void scan_dll_sbox(const char* dll_path, const std::vector<KnownPair>& pairs) {
    printf("\n=== DLL S-box scan (bijective 256-byte permutations): %s ===\n", dll_path);
    std::ifstream f(dll_path, std::ios::binary);
    if (!f) { printf("  Cannot open\n"); return; }
    std::vector<uint8_t> dll((std::istreambuf_iterator<char>(f)), {});

    std::vector<size_t> sbox_offsets;
    for (size_t i = 0; i + 256 <= dll.size(); ++i) {
        bool seen[256] = {};
        bool ok = true;
        for (int j = 0; j < 256; ++j) {
            uint8_t v = dll[i+j];
            if (seen[v]) { ok = false; break; }
            seen[v] = true;
        }
        if (ok) sbox_offsets.push_back(i);
    }
    printf("  Found %zu bijective 256-byte S-boxes.\n", sbox_offsets.size());
    if (sbox_offsets.empty()) return;

    // For each S-box, try simple substitution: treat OID bytes through S-box
    // f = S[b0] | S[b1]<<8 | S[b2]<<16 | S[b3]<<24 (and similar combos)
    int match_count = 0;
    for (size_t off : sbox_offsets) {
        const uint8_t* S = dll.data() + off;
        // Check one test pair first (fast filter)
        const auto& kp0 = pairs[0];
        uint8_t b[4];
        b[0] = S[kp0.oid & 0xFF];
        b[1] = S[(kp0.oid >> 8) & 0xFF];
        b[2] = S[(kp0.oid >> 16) & 0xFF];
        b[3] = S[(kp0.oid >> 24) & 0xFF];
        if (memcmp(b, kp0.fg, 4) != 0) continue; // quick filter on f
        // Full check
        bool all_match = true;
        for (const auto& kp : pairs) {
            b[0] = S[kp.oid & 0xFF];
            b[1] = S[(kp.oid >> 8) & 0xFF];
            b[2] = S[(kp.oid >> 16) & 0xFF];
            b[3] = S[(kp.oid >> 24) & 0xFF];
            if (memcmp(b, kp.fg, 4) != 0) { all_match = false; break; }
        }
        if (all_match) {
            printf("  ** S-box at offset 0x%zX matches f for all pairs!\n", off);
            match_count++;
        }
    }
    if (!match_count) printf("  No S-box match found.\n");
}

// Print differential analysis of consecutive OID→f pairs
static void differential_analysis(const std::vector<KnownPair>& pairs) {
    printf("\n=== Differential analysis (consecutive OID pairs) ===\n");
    if (pairs.size() < 4) { printf("  (need >= 4 pairs)\n"); return; }

    // Find consecutive OID sequences
    std::map<uint32_t, const KnownPair*> by_oid;
    for (const auto& p : pairs) by_oid[p.oid] = &p;

    printf("  Consecutive XOR differences in f:\n");
    int printed = 0;
    for (auto it = by_oid.begin(); it != by_oid.end() && printed < 20; ++it) {
        auto next = by_oid.find(it->first + 1);
        if (next == by_oid.end()) continue;
        uint32_t f0 = *(uint32_t*)it->second->fg;
        uint32_t f1 = *(uint32_t*)next->second->fg;
        uint32_t g0 = *(uint32_t*)(it->second->fg+4);
        uint32_t g1 = *(uint32_t*)(next->second->fg+4);
        printf("    OID=%04X->%04X  fXOR=%08X  gXOR=%08X  fDIFF=%08X  gDIFF=%08X\n",
               it->first, next->first,
               f0^f1, g0^g1,
               (uint32_t)(f1-f0), (uint32_t)(g1-g0));
        ++printed;
    }

    // Check if XOR diff is constant (indicating XOR-linear cipher)
    std::vector<uint32_t> fxors, gxors;
    for (auto it = by_oid.begin(); it != by_oid.end(); ++it) {
        auto next = by_oid.find(it->first + 1);
        if (next == by_oid.end()) continue;
        fxors.push_back(*(uint32_t*)it->second->fg ^ *(uint32_t*)next->second->fg);
        gxors.push_back(*(uint32_t*)(it->second->fg+4) ^ *(uint32_t*)(next->second->fg+4));
    }
    if (!fxors.empty()) {
        bool fconst = std::all_of(fxors.begin(), fxors.end(), [&](uint32_t v){ return v==fxors[0]; });
        bool gconst = std::all_of(gxors.begin(), gxors.end(), [&](uint32_t v){ return v==gxors[0]; });
        if (fconst) printf("  *** f XOR diff is CONSTANT 0x%08X (linear cipher!)\n", fxors[0]);
        if (gconst) printf("  *** g XOR diff is CONSTANT 0x%08X (linear cipher!)\n", gxors[0]);
        // Check ADD diff constant
        std::vector<uint32_t> fadds, gadds;
        for (auto it = by_oid.begin(); it != by_oid.end(); ++it) {
            auto next = by_oid.find(it->first + 1);
            if (next == by_oid.end()) continue;
            fadds.push_back(*(uint32_t*)next->second->fg - *(uint32_t*)it->second->fg);
            gadds.push_back(*(uint32_t*)(next->second->fg+4) - *(uint32_t*)(it->second->fg+4));
        }
        bool faddconst = std::all_of(fadds.begin(), fadds.end(), [&](uint32_t v){ return v==fadds[0]; });
        bool gaddconst = std::all_of(gadds.begin(), gadds.end(), [&](uint32_t v){ return v==gadds[0]; });
        if (faddconst) printf("  *** f ADD diff is CONSTANT 0x%08X (affine cipher!)\n", fadds[0]);
        if (gaddconst) printf("  *** g ADD diff is CONSTANT 0x%08X (affine cipher!)\n", gadds[0]);
    }
}

// ---------------------------------------------------------------------------
// Test keyless hashes (MD5, SHA1, SHA256) of OID bytes against known f||g
// ---------------------------------------------------------------------------
static void keyless_hash_test(const std::vector<KnownPair>& pairs) {
    printf("\n=== Keyless hash tests (no key — MD5/SHA1/SHA256 of OID) ===\n");

    struct AlgInfo { const wchar_t* name; const char* label; size_t sz; };
    const AlgInfo algs[] = {
        {BCRYPT_MD5_ALGORITHM,    "MD5",    16},
        {BCRYPT_SHA1_ALGORITHM,   "SHA1",   20},
        {BCRYPT_SHA256_ALGORITHM, "SHA256", 32},
    };

    bool any_match = false;
    for (const auto& ai : algs) {
        // For each pair, compute hash of OID in various encodings and check all windows
        bool all_match_le4 = true;  // hash(oid_le_4bytes)[fixed_offset..+7] = fg for ALL pairs?
        // We just print individual matches (false positive rate is near zero with 22 pairs)
        for (const auto& p : pairs) {
            uint8_t oid_le4[4] = {(uint8_t)p.oid,(uint8_t)(p.oid>>8),(uint8_t)(p.oid>>16),(uint8_t)(p.oid>>24)};
            uint8_t oid_be4[4] = {(uint8_t)(p.oid>>24),(uint8_t)(p.oid>>16),(uint8_t)(p.oid>>8),(uint8_t)p.oid};
            uint8_t oid_le8[8] = {}; memcpy(oid_le8, oid_le4, 4);
            uint8_t oid_le16[16] = {}; memcpy(oid_le16, oid_le4, 4);

            uint8_t h[32] = {};
            // try oid_le 4 bytes
            if (bcrypt_hash(ai.name, oid_le4, 4, h, ai.sz)) {
                for (size_t off = 0; off + 8 <= ai.sz; ++off) {
                    if (memcmp(h+off, p.fg, 8) == 0) {
                        printf("  ** MATCH: %s(oid_le4)[%zu] = f||g for OID=%04X!\n",ai.label,off,p.oid);
                        any_match = true;
                    }
                }
            }
            // try oid_be 4 bytes
            if (bcrypt_hash(ai.name, oid_be4, 4, h, ai.sz)) {
                for (size_t off = 0; off + 8 <= ai.sz; ++off)
                    if (memcmp(h+off, p.fg, 8) == 0) {
                        printf("  ** MATCH: %s(oid_be4)[%zu] = f||g for OID=%04X!\n",ai.label,off,p.oid);
                        any_match = true;
                    }
            }
            // try oid padded to 8 bytes (LE)
            if (bcrypt_hash(ai.name, oid_le8, 8, h, ai.sz)) {
                for (size_t off = 0; off + 8 <= ai.sz; ++off)
                    if (memcmp(h+off, p.fg, 8) == 0) {
                        printf("  ** MATCH: %s(oid_le8)[%zu] = f||g for OID=%04X!\n",ai.label,off,p.oid);
                        any_match = true;
                    }
            }
            // try oid padded to 16 bytes (LE)
            if (bcrypt_hash(ai.name, oid_le16, 16, h, ai.sz)) {
                for (size_t off = 0; off + 8 <= ai.sz; ++off)
                    if (memcmp(h+off, p.fg, 8) == 0) {
                        printf("  ** MATCH: %s(oid_le16)[%zu] = f||g for OID=%04X!\n",ai.label,off,p.oid);
                        any_match = true;
                    }
            }
        }
    }
    if (!any_match) printf("  No keyless hash match found.\n");
}

// ---------------------------------------------------------------------------
// Scan a binary file for an AES-128 key (16-byte windows in DLL).
// Uses shared BCRYPT_ALG_HANDLE for speed.
// ---------------------------------------------------------------------------
static void scan_dll_for_aes_key(const char* dll_path, const std::vector<KnownPair>& pairs) {
    printf("\n=== DLL AES-128 key scan: %s ===\n", dll_path);

    std::ifstream f(dll_path, std::ios::binary);
    if (!f) { printf("  Cannot open DLL\n"); return; }
    std::vector<uint8_t> dll((std::istreambuf_iterator<char>(f)), {});
    printf("  DLL size: %zu bytes, scanning %zu 16-byte windows...\n",
           dll.size(), dll.size() >= 16 ? dll.size()-15 : 0);

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
        printf("  BCryptOpenAlgorithmProvider(AES) failed\n"); return;
    }
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
        (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
    DWORD key_obj_sz = 0, res = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&key_obj_sz, sizeof(DWORD), &res, 0);
    std::vector<uint8_t> key_obj(key_obj_sz);

    // Build key blob template
    struct AESKBlob { BCRYPT_KEY_DATA_BLOB_HEADER h; uint8_t key[16]; } kb;
    kb.h.dwMagic   = BCRYPT_KEY_DATA_BLOB_MAGIC;
    kb.h.dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
    kb.h.cbKeyData = 16;

    // Plaintext hypotheses for how OID → 16-byte AES block
    const KnownPair& pp = pairs[0];
    uint8_t o_le[4] = {(uint8_t)pp.oid,(uint8_t)(pp.oid>>8),(uint8_t)(pp.oid>>16),(uint8_t)(pp.oid>>24)};
    uint8_t o_be[4] = {(uint8_t)(pp.oid>>24),(uint8_t)(pp.oid>>16),(uint8_t)(pp.oid>>8),(uint8_t)pp.oid};

    struct PtHyp16 { const char* name; uint8_t pt[16]; };
    std::vector<PtHyp16> hyps;
    // H1: oid_le at pos 0, rest 0
    { PtHyp16 h; h.name="oid_le@0"; memset(h.pt,0,16); memcpy(h.pt,o_le,4); hyps.push_back(h); }
    // H2: oid_be at pos 0, rest 0
    { PtHyp16 h; h.name="oid_be@0"; memset(h.pt,0,16); memcpy(h.pt,o_be,4); hyps.push_back(h); }
    // H3: oid_le at pos 4, rest 0
    { PtHyp16 h; h.name="oid_le@4"; memset(h.pt,0,16); memcpy(h.pt+4,o_le,4); hyps.push_back(h); }
    // H4: oid_le at pos 8, rest 0
    { PtHyp16 h; h.name="oid_le@8"; memset(h.pt,0,16); memcpy(h.pt+8,o_le,4); hyps.push_back(h); }
    // H5: oid_le at pos 12, rest 0
    { PtHyp16 h; h.name="oid_le@12"; memset(h.pt,0,16); memcpy(h.pt+12,o_le,4); hyps.push_back(h); }
    // H6: oid_le repeated 4x
    { PtHyp16 h; h.name="oid_le×4"; for(int i=0;i<4;i++) memcpy(h.pt+i*4,o_le,4); hyps.push_back(h); }

    uint32_t hits = 0;
    for (size_t off = 0; off + 16 <= dll.size(); ++off) {
        const uint8_t* key_bytes = dll.data() + off;
        memcpy(kb.key, key_bytes, 16);

        // Check if all bytes same (trivial key — skip)
        bool trivial = true;
        for (int i = 1; i < 16; ++i) if (key_bytes[i] != key_bytes[0]) { trivial = false; break; }
        if (trivial) continue;

        for (auto& hyp : hyps) {
            BCRYPT_KEY_HANDLE hKey = nullptr;
            if (!BCRYPT_SUCCESS(BCryptImportKey(hAlg, nullptr, BCRYPT_KEY_DATA_BLOB,
                                                &hKey, key_obj.data(), key_obj_sz,
                                                (PUCHAR)&kb, sizeof(kb), 0))) continue;

            uint8_t ct[16];
            ULONG rlen = 0;
            bool enc_ok = BCRYPT_SUCCESS(BCryptEncrypt(hKey, (PUCHAR)hyp.pt, 16, nullptr,
                                                        nullptr, 0, ct, 16, &rlen, 0));
            BCryptDestroyKey(hKey);
            if (!enc_ok) continue;

            // Check output[4..11] = f||g for first pair
            if (memcmp(ct+4, pp.fg, 8) != 0) continue;

            // Verify all pairs
            bool all_ok = true;
            for (size_t pi = 1; pi < pairs.size() && all_ok; ++pi) {
                const auto& vp = pairs[pi];
                uint8_t v_le[4] = {(uint8_t)vp.oid,(uint8_t)(vp.oid>>8),(uint8_t)(vp.oid>>16),(uint8_t)(vp.oid>>24)};
                uint8_t v_be[4] = {(uint8_t)(vp.oid>>24),(uint8_t)(vp.oid>>16),(uint8_t)(vp.oid>>8),(uint8_t)vp.oid};
                uint8_t vpt[16] = {};
                if      (strcmp(hyp.name,"oid_le@0")==0)  memcpy(vpt,    v_le,4);
                else if (strcmp(hyp.name,"oid_be@0")==0)  memcpy(vpt,    v_be,4);
                else if (strcmp(hyp.name,"oid_le@4")==0)  memcpy(vpt+4,  v_le,4);
                else if (strcmp(hyp.name,"oid_le@8")==0)  memcpy(vpt+8,  v_le,4);
                else if (strcmp(hyp.name,"oid_le@12")==0) memcpy(vpt+12, v_le,4);
                else if (strcmp(hyp.name,"oid_le×4")==0)  { for(int i=0;i<4;i++) memcpy(vpt+i*4,v_le,4); }
                else continue;

                BCRYPT_KEY_HANDLE hKey2 = nullptr;
                memcpy(kb.key, key_bytes, 16);
                if (!BCRYPT_SUCCESS(BCryptImportKey(hAlg, nullptr, BCRYPT_KEY_DATA_BLOB,
                                                    &hKey2, key_obj.data(), key_obj_sz,
                                                    (PUCHAR)&kb, sizeof(kb), 0))) { all_ok=false; break; }
                uint8_t vct[16]; ULONG vlen=0;
                bool vok = BCRYPT_SUCCESS(BCryptEncrypt(hKey2, vpt, 16, nullptr, nullptr, 0, vct, 16, &vlen, 0));
                BCryptDestroyKey(hKey2);
                if (!vok || memcmp(vct+4, vp.fg, 8) != 0) all_ok = false;
            }
            if (all_ok) {
                printf("  *** AES KEY FOUND! offset=0x%08zX hyp=%s\n", off, hyp.name);
                printf("      key: ");
                for (int i = 0; i < 16; ++i) printf("%02X ", key_bytes[i]);
                printf("\n");
                ++hits;
            }
        }
    }
    BCryptCloseAlgorithmProvider(hAlg, 0);
    if (hits == 0) printf("  No AES-128 key found.\n");
}

// ---------------------------------------------------------------------------
// Scan a binary file for a DES key that encrypts plaintext → ciphertext.
// Tries multiple plaintext hypotheses for encoding a 4-byte OID into 8 bytes.
// Verifies candidate key against ALL verification pairs.
// ---------------------------------------------------------------------------
static void scan_dll_for_des_key(const char* dll_path,
                                   const std::vector<KnownPair>& pairs) {
    printf("\n=== DLL DES-key scan: %s ===\n", dll_path);
    printf("  Pairs to match:\n");
    for (const auto& p : pairs)
        printf("    OID=%04X  fg=%02X%02X%02X%02X %02X%02X%02X%02X\n",
               p.oid, p.fg[0],p.fg[1],p.fg[2],p.fg[3],p.fg[4],p.fg[5],p.fg[6],p.fg[7]);

    std::ifstream f(dll_path, std::ios::binary);
    if (!f) { printf("  Cannot open DLL\n"); return; }
    std::vector<uint8_t> dll((std::istreambuf_iterator<char>(f)), {});
    printf("  DLL size: %zu bytes, scanning %zu 8-byte windows...\n",
           dll.size(), dll.size() > 8 ? dll.size()-7 : 0);

    // Known-plaintext hypotheses for how OID → 8-byte DES block
    // Each pair is (primary pair index, plaintext format)
    // We use the first pair for fast pre-filter, then verify all
    const KnownPair& pp = pairs[0];
    uint8_t oid0_le[4] = {(uint8_t)pp.oid,(uint8_t)(pp.oid>>8),(uint8_t)(pp.oid>>16),(uint8_t)(pp.oid>>24)};
    uint8_t oid0_be[4] = {(uint8_t)(pp.oid>>24),(uint8_t)(pp.oid>>16),(uint8_t)(pp.oid>>8),(uint8_t)pp.oid};

    struct PtHyp { const char* name; uint8_t pt[8]; };
    std::vector<PtHyp> hyps;
    // H1: oid_le || 00 00 00 00
    { PtHyp h; h.name="oid_le||zeros"; memcpy(h.pt,oid0_le,4); memset(h.pt+4,0,4); hyps.push_back(h); }
    // H2: oid_be || 00 00 00 00
    { PtHyp h; h.name="oid_be||zeros"; memcpy(h.pt,oid0_be,4); memset(h.pt+4,0,4); hyps.push_back(h); }
    // H3: oid_le || oid_le
    { PtHyp h; h.name="oid_le||oid_le"; memcpy(h.pt,oid0_le,4); memcpy(h.pt+4,oid0_le,4); hyps.push_back(h); }
    // H4: 00 00 00 00 || oid_le
    { PtHyp h; h.name="zeros||oid_le"; memset(h.pt,0,4); memcpy(h.pt+4,oid0_le,4); hyps.push_back(h); }
    // H5: oid_le (low 2 bytes) in positions 0,1, rest zero
    { PtHyp h; h.name="oid_le16||zeros"; h.pt[0]=oid0_le[0];h.pt[1]=oid0_le[1];memset(h.pt+2,0,6); hyps.push_back(h); }
    // H6: 00 00 00 00 || oid_be
    { PtHyp h; h.name="zeros||oid_be"; memset(h.pt,0,4); memcpy(h.pt+4,oid0_be,4); hyps.push_back(h); }

    uint32_t hits = 0;
    for (size_t off = 0; off + 8 <= dll.size(); ++off) {
        const uint8_t* key = dll.data() + off;
        // Quick DES parity check: a typical DES key has varied bytes (skip trivial)
        bool trivial = true;
        for (int i = 1; i < 8; ++i) if (key[i] != key[0]) { trivial = false; break; }
        if (trivial) continue;

        for (auto& hyp : hyps) {
            uint8_t ct[8];
            if (!des_ecb_enc(key, hyp.pt, ct)) continue;
            if (memcmp(ct, pp.fg, 8) != 0) continue;

            // Pre-filter matched! Now verify all pairs
            bool all_ok = true;
            for (size_t pi = 1; pi < pairs.size() && all_ok; ++pi) {
                const auto& vp = pairs[pi];
                uint8_t vpt[8];
                // Build plaintext for this pair using same hypothesis format
                uint8_t v_le[4] = {(uint8_t)vp.oid,(uint8_t)(vp.oid>>8),(uint8_t)(vp.oid>>16),(uint8_t)(vp.oid>>24)};
                uint8_t v_be[4] = {(uint8_t)(vp.oid>>24),(uint8_t)(vp.oid>>16),(uint8_t)(vp.oid>>8),(uint8_t)vp.oid};
                if (strcmp(hyp.name,"oid_le||zeros")==0)  { memcpy(vpt,v_le,4); memset(vpt+4,0,4); }
                else if (strcmp(hyp.name,"oid_be||zeros")==0) { memcpy(vpt,v_be,4); memset(vpt+4,0,4); }
                else if (strcmp(hyp.name,"oid_le||oid_le")==0) { memcpy(vpt,v_le,4); memcpy(vpt+4,v_le,4); }
                else if (strcmp(hyp.name,"zeros||oid_le")==0)  { memset(vpt,0,4); memcpy(vpt+4,v_le,4); }
                else if (strcmp(hyp.name,"oid_le16||zeros")==0) { vpt[0]=v_le[0];vpt[1]=v_le[1];memset(vpt+2,0,6); }
                else if (strcmp(hyp.name,"zeros||oid_be")==0)  { memset(vpt,0,4); memcpy(vpt+4,v_be,4); }
                else continue;

                uint8_t vct[8];
                if (!des_ecb_enc(key, vpt, vct)) { all_ok = false; break; }
                if (memcmp(vct, vp.fg, 8) != 0) all_ok = false;
            }

            if (all_ok) {
                printf("  *** KEY FOUND! offset=0x%08zX hyp=%s\n", off, hyp.name);
                printf("      key: ");
                for (int i = 0; i < 8; ++i) printf("%02X ", key[i]);
                printf("\n");
                ++hits;
            }
        }

        // Also try DECRYPTION (ct=pp.fg → pt should equal plaintext hypothesis)
        for (auto& hyp : hyps) {
            uint8_t pt_out[8];
            if (!des_ecb_dec(key, pp.fg, pt_out)) continue;
            if (memcmp(pt_out, hyp.pt, 8) != 0) continue;

            // Verify all pairs with decrypt
            bool all_ok = true;
            for (size_t pi = 1; pi < pairs.size() && all_ok; ++pi) {
                const auto& vp = pairs[pi];
                uint8_t v_le[4] = {(uint8_t)vp.oid,(uint8_t)(vp.oid>>8),(uint8_t)(vp.oid>>16),(uint8_t)(vp.oid>>24)};
                uint8_t v_be[4] = {(uint8_t)(vp.oid>>24),(uint8_t)(vp.oid>>16),(uint8_t)(vp.oid>>8),(uint8_t)vp.oid};
                uint8_t vpt[8];
                if (strcmp(hyp.name,"oid_le||zeros")==0)  { memcpy(vpt,v_le,4); memset(vpt+4,0,4); }
                else if (strcmp(hyp.name,"oid_be||zeros")==0) { memcpy(vpt,v_be,4); memset(vpt+4,0,4); }
                else if (strcmp(hyp.name,"oid_le||oid_le")==0) { memcpy(vpt,v_le,4); memcpy(vpt+4,v_le,4); }
                else if (strcmp(hyp.name,"zeros||oid_le")==0)  { memset(vpt,0,4); memcpy(vpt+4,v_le,4); }
                else if (strcmp(hyp.name,"oid_le16||zeros")==0) { vpt[0]=v_le[0];vpt[1]=v_le[1];memset(vpt+2,0,6); }
                else if (strcmp(hyp.name,"zeros||oid_be")==0)  { memset(vpt,0,4); memcpy(vpt+4,v_be,4); }
                else continue;

                uint8_t vpt_out[8];
                if (!des_ecb_dec(key, vp.fg, vpt_out)) { all_ok = false; break; }
                if (memcmp(vpt_out, vpt, 8) != 0) all_ok = false;
            }
            if (all_ok) {
                printf("  *** KEY FOUND (decrypt)! offset=0x%08zX hyp=%s\n", off, hyp.name);
                printf("      key: ");
                for (int i = 0; i < 8; ++i) printf("%02X ", key[i]);
                printf("\n");
                ++hits;
            }
        }
    }
    if (hits == 0)
        printf("  No DES key found with tested plaintext hypotheses.\n");
    else
        printf("  %u key(s) found total.\n", hits);
}

// ---------------------------------------------------------------------------
// Keystream / database-key hypothesis analysis
//
// Tests whether:
//   cipher[4-7]  = db_key_bytes[j..j+3] XOR oid_le  (j unknown)
//   keystream    = db_key_bytes[k..k+3] XOR oid_le   (k unknown)
//
// Membership delta encoding (from cross-DB XOR analysis):
//   p_admin  = (00 00 00 20)  byte[11] bit5
//   p_backup = (01 00 00 00)  byte[8]  bit0
//   p_debug  = (06 00 01 00)  byte[8] bits1-2 + byte[10] bit0
// ---------------------------------------------------------------------------
static const uint8_t DELTA_ADMIN[4]  = {0x00, 0x00, 0x00, 0x20};
static const uint8_t DELTA_BACKUP[4] = {0x01, 0x00, 0x00, 0x00};
static const uint8_t DELTA_DEBUG[4]  = {0x06, 0x00, 0x01, 0x00};

static void xor4(const uint8_t* a, const uint8_t* b, uint8_t* out) {
    for (int i = 0; i < 4; ++i) out[i] = a[i] ^ b[i];
}

static void keystream_analysis(const std::vector<User>& users,
                                const char* db_label,
                                const uint8_t db_prop273[273]) {
    printf("\n=== Keystream/DB-key analysis: %s ===\n", db_label);

    // Print per-user table: OID, cipher[4-7], keystream[8-11], c47^oid_le, ks^oid_le
    printf("  %-22s %8s bits  c[4-7]           c[8-11]          ks[8-11]         c47^oid_le       ks^oid_le\n",
           "User", "OID");

    for (const auto& u : users) {
        if (!u.has_cipher) continue;

        // derive keystream by stripping membership bits
        uint8_t ks[4]; memcpy(ks, u.cipher+8, 4);
        if (u.db_admin)  xor4(ks, DELTA_ADMIN,  ks);
        if (u.db_backup) xor4(ks, DELTA_BACKUP, ks);
        if (u.db_debug)  xor4(ks, DELTA_DEBUG,  ks);

        uint8_t oid_le[4] = {(uint8_t)u.oid,(uint8_t)(u.oid>>8),(uint8_t)(u.oid>>16),(uint8_t)(u.oid>>24)};
        uint8_t c47_xoid[4]; xor4(u.cipher+4, oid_le, c47_xoid);
        uint8_t ks_xoid[4];  xor4(ks, oid_le, ks_xoid);

        printf("  %-22s %08X  %X  %02X%02X%02X%02X  %02X%02X%02X%02X  %02X%02X%02X%02X  %02X%02X%02X%02X  %02X%02X%02X%02X\n",
               u.name.c_str(), u.oid, u.bits(),
               u.cipher[4],u.cipher[5],u.cipher[6],u.cipher[7],
               u.cipher[8],u.cipher[9],u.cipher[10],u.cipher[11],
               ks[0],ks[1],ks[2],ks[3],
               c47_xoid[0],c47_xoid[1],c47_xoid[2],c47_xoid[3],
               ks_xoid[0],ks_xoid[1],ks_xoid[2],ks_xoid[3]);
    }

    // Test: is (cipher[4-7] XOR oid_le) constant across all users in this DB?
    // If yes → cipher[4-7] = db_const_A XOR oid_le
    {
        uint8_t ref[4] = {}; bool has_ref = false; bool all_same = true;
        for (const auto& u : users) {
            if (!u.has_cipher) continue;
            uint8_t oid_le[4] = {(uint8_t)u.oid,(uint8_t)(u.oid>>8),(uint8_t)(u.oid>>16),(uint8_t)(u.oid>>24)};
            uint8_t v[4]; xor4(u.cipher+4, oid_le, v);
            if (!has_ref) { memcpy(ref, v, 4); has_ref = true; }
            else if (memcmp(ref, v, 4) != 0) { all_same = false; }
        }
        if (has_ref) {
            if (all_same)
                printf("  ** cipher[4-7] XOR oid_le = CONSTANT %02X%02X%02X%02X across all users!\n",
                       ref[0],ref[1],ref[2],ref[3]);
            else
                printf("  cipher[4-7] XOR oid_le varies — not simply db_const XOR oid_le\n");

            if (all_same && db_prop273) {
                bool found = false;
                for (size_t j = 0; j+4 <= 273; ++j) {
                    if (memcmp(db_prop273+j, ref, 4) == 0) {
                        printf("  ** FOUND at db_prop273[%zu]\n", j); found = true;
                    }
                }
                if (!found) printf("  (constant not found verbatim in db_prop273)\n");
            }
        }
    }

    // Test: is (keystream XOR oid_le) constant across all users in this DB?
    {
        uint8_t ref[4] = {}; bool has_ref = false; bool all_same = true;
        for (const auto& u : users) {
            if (!u.has_cipher) continue;
            uint8_t ks[4]; memcpy(ks, u.cipher+8, 4);
            if (u.db_admin)  xor4(ks, DELTA_ADMIN,  ks);
            if (u.db_backup) xor4(ks, DELTA_BACKUP, ks);
            if (u.db_debug)  xor4(ks, DELTA_DEBUG,  ks);
            uint8_t oid_le[4] = {(uint8_t)u.oid,(uint8_t)(u.oid>>8),(uint8_t)(u.oid>>16),(uint8_t)(u.oid>>24)};
            uint8_t v[4]; xor4(ks, oid_le, v);
            if (!has_ref) { memcpy(ref, v, 4); has_ref = true; }
            else if (memcmp(ref, v, 4) != 0) { all_same = false; }
        }
        if (has_ref) {
            if (all_same)
                printf("  ** keystream XOR oid_le = CONSTANT %02X%02X%02X%02X across all users!\n",
                       ref[0],ref[1],ref[2],ref[3]);
            else
                printf("  keystream XOR oid_le varies — not simply db_const XOR oid_le\n");

            if (all_same && db_prop273) {
                bool found = false;
                for (size_t j = 0; j+4 <= 273; ++j) {
                    if (memcmp(db_prop273+j, ref, 4) == 0) {
                        printf("  ** FOUND at db_prop273[%zu]\n", j); found = true;
                    }
                }
                if (!found) printf("  (constant not found verbatim in db_prop273)\n");
            }
        }
    }

    // Also try oid_be
    {
        bool all_same_c47 = true, all_same_ks = true;
        uint8_t ref_c47[4]={}, ref_ks[4]={};
        bool has_c47=false, has_ks=false;
        for (const auto& u : users) {
            if (!u.has_cipher) continue;
            uint8_t oid_be[4] = {(uint8_t)(u.oid>>24),(uint8_t)(u.oid>>16),(uint8_t)(u.oid>>8),(uint8_t)u.oid};
            uint8_t ks[4]; memcpy(ks, u.cipher+8, 4);
            if (u.db_admin)  xor4(ks, DELTA_ADMIN,  ks);
            if (u.db_backup) xor4(ks, DELTA_BACKUP, ks);
            if (u.db_debug)  xor4(ks, DELTA_DEBUG,  ks);
            uint8_t c47_xoid[4]; xor4(u.cipher+4, oid_be, c47_xoid);
            uint8_t ks_xoid[4]; xor4(ks, oid_be, ks_xoid);
            if (!has_c47) { memcpy(ref_c47, c47_xoid, 4); has_c47=true; }
            else if (memcmp(ref_c47, c47_xoid, 4)!=0) all_same_c47=false;
            if (!has_ks) { memcpy(ref_ks, ks_xoid, 4); has_ks=true; }
            else if (memcmp(ref_ks, ks_xoid, 4)!=0) all_same_ks=false;
        }
        if (has_c47 && all_same_c47)
            printf("  ** cipher[4-7] XOR oid_BE = CONSTANT %02X%02X%02X%02X!\n",ref_c47[0],ref_c47[1],ref_c47[2],ref_c47[3]);
        if (has_ks && all_same_ks)
            printf("  ** keystream XOR oid_BE = CONSTANT %02X%02X%02X%02X!\n",ref_ks[0],ref_ks[1],ref_ks[2],ref_ks[3]);
    }

    // Dump the DB record prop273 (first 64 bytes)
    if (db_prop273) {
        printf("  DB record prop273[0..63]: ");
        for (int i = 0; i < 64; ++i) printf("%02X ", db_prop273[i]);
        printf("\n");
    }

    // Test known K[slot] values — do they appear in db_prop273?
    // pmsys K[0..2]: 00 71 0D 50 | E6 96 26 39 | 6F F3 58 E7
    // sfi-2021 K[0..6]: BC 56 29 7C | B5 CF 07 0D | F0 0B 6E B2 | CF A0 89 50 | F6 CE 0A D9 | 8B 02 78 96 | 7C 9E 0E 6C
    const uint8_t k_pmsys[3][4] = {{0x00,0x71,0x0D,0x50},{0xE6,0x96,0x26,0x39},{0x6F,0xF3,0x58,0xE7}};
    const uint8_t k_sfi[7][4]   = {{0xBC,0x56,0x29,0x7C},{0xB5,0xCF,0x07,0x0D},{0xF0,0x0B,0x6E,0xB2},
                                    {0xCF,0xA0,0x89,0x50},{0xF6,0xCE,0x0A,0xD9},{0x8B,0x02,0x78,0x96},{0x7C,0x9E,0x0E,0x6C}};
    if (db_prop273) {
        // Check pmsys K values
        bool any_k = false;
        for (int s = 0; s < 3; ++s)
            for (size_t j = 0; j+4 <= 273; ++j)
                if (memcmp(db_prop273+j, k_pmsys[s], 4)==0) {
                    printf("  ** FOUND pmsys K[%d] at db_prop273[%zu]\n", s, j); any_k=true;
                }
        for (int s = 0; s < 7; ++s)
            for (size_t j = 0; j+4 <= 273; ++j)
                if (memcmp(db_prop273+j, k_sfi[s], 4)==0) {
                    printf("  ** FOUND sfi K[%d] at db_prop273[%zu]\n", s, j); any_k=true;
                }
        if (!any_k) printf("  (no K[slot] constants found in db_prop273)\n");
    }
}

// ---------------------------------------------------------------------------
// Full (OID, 16-byte keystream) pair — for bits=0 users whose cipher = keystream
// Membership encoding (universal, confirmed):
//   Admin:  D1 75 DA BB 00 00 00 00 00 00 00 20 D9 3D 92 6B
//   Backup: 7B 52 D9 C3 00 00 00 00 01 00 00 00 E0 E9 C4 79
//   Debug:  6B D5 21 9A 00 00 00 00 06 00 01 00 0A 13 0F 3F
// For users with bits != 0: keystream = cipher XOR membership_enc
// ---------------------------------------------------------------------------
struct FullPair {
    uint32_t oid;
    uint8_t  ks[16]; // full 16-byte keystream
};

static const uint8_t MEM_ADMIN[16]  = {0xD1,0x75,0xDA,0xBB,0,0,0,0,0,0,0,0x20,0xD9,0x3D,0x92,0x6B};
static const uint8_t MEM_BACKUP[16] = {0x7B,0x52,0xD9,0xC3,0,0,0,0,1,0,0,0,0xE0,0xE9,0xC4,0x79};
static const uint8_t MEM_DEBUG[16]  = {0x6B,0xD5,0x21,0x9A,0,0,0,0,6,0,1,0,0x0A,0x13,0x0F,0x3F};

// Build full-pair list from the per-DB user lists
static std::vector<FullPair> build_full_pairs(
        const std::vector<std::pair<std::string,std::vector<User>>>& all_dbs) {
    std::map<uint32_t, std::array<uint8_t,16>> oid_to_ks;
    for (const auto& [label, users] : all_dbs) {
        for (const auto& u : users) {
            if (!u.has_cipher) continue;
            uint8_t ks[16]; memcpy(ks, u.cipher, 16);
            // Strip membership encoding to get keystream
            if (u.db_admin)  for (int i=0;i<16;i++) ks[i]^=MEM_ADMIN[i];
            if (u.db_backup) for (int i=0;i<16;i++) ks[i]^=MEM_BACKUP[i];
            if (u.db_debug)  for (int i=0;i<16;i++) ks[i]^=MEM_DEBUG[i];
            std::array<uint8_t,16> arr; memcpy(arr.data(), ks, 16);
            if (oid_to_ks.count(u.oid)) {
                if (memcmp(oid_to_ks[u.oid].data(), arr.data(), 16) != 0) {
                    printf("  FULL PAIR INCONSISTENCY OID=%04X!\n", u.oid);
                }
            } else {
                oid_to_ks[u.oid] = arr;
            }
        }
    }
    std::vector<FullPair> fp;
    for (auto& [oid, arr] : oid_to_ks) {
        FullPair p; p.oid = oid; memcpy(p.ks, arr.data(), 16);
        fp.push_back(p);
    }
    return fp;
}

// ---------------------------------------------------------------------------
// RC4 helpers
// ---------------------------------------------------------------------------
static void rc4_init(uint8_t S[256], const uint8_t* key, size_t klen) {
    for (int i=0;i<256;i++) S[i]=(uint8_t)i;
    int j=0;
    for (int i=0;i<256;i++) { j=(j+S[i]+key[i%klen])&255; uint8_t t=S[i];S[i]=S[j];S[j]=t; }
}
static void rc4_gen(uint8_t S[256], int& ii, int& jj, uint8_t* out, size_t n) {
    for (size_t k=0;k<n;k++) {
        ii=(ii+1)&255; jj=(jj+S[ii])&255;
        uint8_t t=S[ii];S[ii]=S[jj];S[jj]=t;
        out[k]=S[(uint8_t)(S[ii]+S[jj])];
    }
}
// Generate then skip to position pos, then read 8 bytes
static bool rc4_check_at(const uint8_t* key, size_t klen, uint64_t pos, const uint8_t* expected8) {
    uint8_t S[256]; rc4_init(S, key, klen);
    int ii=0, jj=0;
    for (uint64_t k=0; k<pos+8; k++) {
        ii=(ii+1)&255; jj=(jj+S[ii])&255;
        uint8_t t=S[ii];S[ii]=S[jj];S[jj]=t;
        if (k>=pos) { // compare inline during generation
            if (S[(uint8_t)(S[ii]+S[jj])] != expected8[k-pos]) return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// FIXED RC4 OID-keyed DLL scan: key = OID_le || W  (various W sizes)
// Compares only f||g (bytes[4-11], 8 bytes) — NOT the random bytes[0-3],[12-15]
// ---------------------------------------------------------------------------
static void scan_dll_rc4_oid_keyed(const char* dll_path,
                                    const std::vector<KnownPair>& pairs) {
    printf("\n=== DLL RC4 OID-keyed scan (key=OID||W or W||OID, output[off..+7]=fg): %s ===\n", dll_path);
    std::ifstream fi(dll_path, std::ios::binary);
    if (!fi) { printf("  Cannot open DLL\n"); return; }
    std::vector<uint8_t> dll((std::istreambuf_iterator<char>(fi)), {});

    // Use smallest OID as fast filter — only need 8 bytes of RC4 output
    auto sorted = pairs;
    std::sort(sorted.begin(), sorted.end(), [](const KnownPair& a, const KnownPair& b){ return a.oid < b.oid; });
    const KnownPair& p0 = sorted[0];
    // f0 = p0.fg[0..3], g0 = p0.fg[4..7]
    // Expected 8-byte output at offset 'out_off'

    struct KFmt { const char* name; int wlen; bool oid_first; bool xor_mode; };
    KFmt kfmts[] = {
        {"OID||W4",   4, true,  false},
        {"OID||W8",   8, true,  false},
        {"OID||W12", 12, true,  false},
        {"W4||OID",   4, false, false},
        {"W8||OID",   8, false, false},
        {"W12||OID", 12, false, false},
        {"W16^OID",  16, true,  true },
    };
    int out_offsets[] = {0, 4, 8}; // where in RC4 stream the f||g appears

    uint32_t hits = 0;
    for (const auto& kf : kfmts) {
        size_t w_end = dll.size() >= (size_t)kf.wlen ? dll.size() - kf.wlen + 1 : 0;
        uint8_t o0[4] = {(uint8_t)p0.oid,(uint8_t)(p0.oid>>8),(uint8_t)(p0.oid>>16),(uint8_t)(p0.oid>>24)};
        for (int out_off : out_offsets) {
            for (size_t off = 0; off < w_end; ++off) {
                const uint8_t* W = dll.data() + off;
                uint8_t key[20]; size_t klen;
                if (kf.xor_mode) {
                    memcpy(key, W, 16); key[0]^=o0[0]; key[1]^=o0[1]; key[2]^=o0[2]; key[3]^=o0[3]; klen=16;
                } else if (kf.oid_first) {
                    memcpy(key,o0,4); memcpy(key+4,W,kf.wlen); klen=4+kf.wlen;
                } else {
                    memcpy(key,W,kf.wlen); memcpy(key+kf.wlen,o0,4); klen=kf.wlen+4;
                }
                // Fast filter: check first pair
                if (!rc4_check_at(key, klen, out_off, p0.fg)) continue;

                // Verify all pairs
                bool all_ok = true;
                for (size_t pi=1; pi<sorted.size() && all_ok; pi++) {
                    const KnownPair& p = sorted[pi];
                    uint8_t oi[4]={(uint8_t)p.oid,(uint8_t)(p.oid>>8),(uint8_t)(p.oid>>16),(uint8_t)(p.oid>>24)};
                    uint8_t vk[20]; size_t vkl;
                    if (kf.xor_mode) { memcpy(vk,W,16); vk[0]^=oi[0];vk[1]^=oi[1];vk[2]^=oi[2];vk[3]^=oi[3]; vkl=16; }
                    else if (kf.oid_first) { memcpy(vk,oi,4);memcpy(vk+4,W,kf.wlen);vkl=4+kf.wlen; }
                    else { memcpy(vk,W,kf.wlen);memcpy(vk+kf.wlen,oi,4);vkl=kf.wlen+4; }
                    if (!rc4_check_at(vk, vkl, out_off, p.fg)) all_ok=false;
                }
                if (all_ok) {
                    printf("  *** RC4 OID-keyed FOUND! fmt=%s out_off=%d dll_off=0x%08zX\n", kf.name, out_off, off);
                    printf("      W: "); for (int i=0;i<kf.wlen;i++) printf("%02X ",W[i]); printf("\n");
                    hits++;
                }
            }
        }
    }
    if (hits == 0) printf("  No match.\n");
}

// ---------------------------------------------------------------------------
// RC4 offset scan: fixed DLL key K, f||g = RC4(K)[oid*scale .. +7]
// Hypothesis: SAP uses a single long RC4 keystream; OID selects which 8 bytes
// ---------------------------------------------------------------------------
static void scan_dll_rc4_oid_offset(const char* dll_path,
                                     const std::vector<KnownPair>& pairs) {
    printf("\n=== DLL RC4 OID-as-offset scan (key=DLL, output[oid*scale..+7]=fg): %s ===\n", dll_path);
    std::ifstream fi(dll_path, std::ios::binary);
    if (!fi) { printf("  Cannot open DLL\n"); return; }
    std::vector<uint8_t> dll((std::istreambuf_iterator<char>(fi)), {});

    auto sorted = pairs;
    std::sort(sorted.begin(), sorted.end(), [](const KnownPair& a, const KnownPair& b){ return a.oid<b.oid; });
    const KnownPair& p0 = sorted[0]; // smallest OID → smallest position → fastest filter

    // For scale s, fast filter generates RC4 up to position p0.oid*s + 8
    // Verify all: generate up to max_oid*s + 8
    uint32_t max_oid = sorted.back().oid;

    uint32_t scales[] = {1, 2, 4, 8, 16, 32};
    uint32_t hits = 0;
    for (uint32_t s : scales) {
        uint64_t pos0 = (uint64_t)p0.oid * s;
        uint64_t pos_max = (uint64_t)max_oid * s;
        printf("  scale=%u: filter_pos=%llu max_pos=%llu\n", s, (unsigned long long)pos0, (unsigned long long)pos_max);
        for (size_t off = 0; off+16 <= dll.size(); off++) {
            // Fast filter: generate stream to pos0+8
            if (!rc4_check_at(dll.data()+off, 16, pos0, p0.fg)) continue;
            // Verify all pairs
            bool all_ok = true;
            for (size_t pi=1; pi<sorted.size() && all_ok; pi++) {
                const KnownPair& p = sorted[pi];
                uint64_t pos = (uint64_t)p.oid * s;
                if (!rc4_check_at(dll.data()+off, 16, pos, p.fg)) all_ok = false;
            }
            if (all_ok) {
                printf("  *** RC4 OFFSET KEY FOUND! scale=%u dll_off=0x%08zX key: ", s, off);
                for (int i=0;i<16;i++) printf("%02X ",dll[off+i]); printf("\n");
                hits++;
            }
        }
    }
    if (hits == 0) printf("  No match.\n");
}

// ---------------------------------------------------------------------------
// Lookup table search: are any f or g values stored verbatim in the DLL?
// ---------------------------------------------------------------------------
static void scan_dll_fg_lookup(const char* dll_path, const std::vector<KnownPair>& pairs) {
    printf("\n=== DLL f/g value lookup: %s ===\n", dll_path);
    std::ifstream fi(dll_path, std::ios::binary);
    if (!fi) { printf("  Cannot open\n"); return; }
    std::vector<uint8_t> dll((std::istreambuf_iterator<char>(fi)), {});

    int found = 0;
    for (const auto& p : pairs) {
        // Search for f value (4 bytes) in DLL
        for (size_t i=0; i+4<=dll.size(); i++) {
            if (memcmp(dll.data()+i, p.fg, 4)==0) {
                printf("  f(OID=%04X)=%02X%02X%02X%02X found at DLL off 0x%08zX\n",
                       p.oid,p.fg[0],p.fg[1],p.fg[2],p.fg[3],i);
                found++;
            }
        }
        // Search for g value
        for (size_t i=0; i+4<=dll.size(); i++) {
            if (memcmp(dll.data()+i, p.fg+4, 4)==0) {
                printf("  g(OID=%04X)=%02X%02X%02X%02X found at DLL off 0x%08zX\n",
                       p.oid,p.fg[4],p.fg[5],p.fg[6],p.fg[7],i);
                found++;
            }
        }
    }
    if (found == 0) printf("  No f or g values found verbatim in DLL.\n");
    else printf("  %d 4-byte matches found (may include coincidental matches).\n", found);
}

// ---------------------------------------------------------------------------
// TEA / XTEA (Tiny Encryption Algorithms — no lookup tables, 1990s-era)
// ---------------------------------------------------------------------------
static void tea_encrypt(uint32_t v[2], const uint32_t key[4], uint32_t rounds = 32) {
    uint32_t v0=v[0], v1=v[1], sum=0;
    const uint32_t delta=0x9E3779B9;
    for (uint32_t i=0; i<rounds; i++) {
        sum += delta;
        v0 += ((v1<<4)+key[0])^(v1+sum)^((v1>>5)+key[1]);
        v1 += ((v0<<4)+key[2])^(v0+sum)^((v0>>5)+key[3]);
    }
    v[0]=v0; v[1]=v1;
}
static void xtea_encrypt(uint32_t v[2], const uint32_t key[4], uint32_t rounds = 32) {
    uint32_t v0=v[0], v1=v[1], sum=0;
    const uint32_t delta=0x9E3779B9;
    for (uint32_t i=0; i<rounds; i++) {
        v0 += (((v1<<4)^(v1>>5))+v1)^(sum+key[sum&3]);
        sum += delta;
        v1 += (((v0<<4)^(v0>>5))+v0)^(sum+key[(sum>>11)&3]);
    }
    v[0]=v0; v[1]=v1;
}

// Scan DLL for TEA or XTEA 16-byte key
static void scan_dll_for_tea_key(const char* dll_path, const std::vector<KnownPair>& pairs, bool use_xtea) {
    const char* algo = use_xtea ? "XTEA" : "TEA";
    printf("\n=== DLL %s key scan: %s ===\n", algo, dll_path);
    std::ifstream fi(dll_path, std::ios::binary);
    if (!fi) { printf("  Cannot open DLL\n"); return; }
    std::vector<uint8_t> dll((std::istreambuf_iterator<char>(fi)), {});
    printf("  DLL: %zu bytes, %zu 16-byte windows\n", dll.size(), dll.size()>=16?dll.size()-15:0);

    const KnownPair& pp0 = pairs[0];
    uint8_t o0_le[4]={(uint8_t)pp0.oid,(uint8_t)(pp0.oid>>8),(uint8_t)(pp0.oid>>16),(uint8_t)(pp0.oid>>24)};
    uint8_t o0_be[4]={(uint8_t)(pp0.oid>>24),(uint8_t)(pp0.oid>>16),(uint8_t)(pp0.oid>>8),(uint8_t)pp0.oid};

    // v0/v1 input hypotheses for the first pair
    struct PtH { const char* n; uint32_t v0; uint32_t v1; };
    uint32_t oid0_le_u32 = (uint32_t)o0_le[0]|((uint32_t)o0_le[1]<<8)|((uint32_t)o0_le[2]<<16)|((uint32_t)o0_le[3]<<24);
    uint32_t oid0_be_u32 = (uint32_t)o0_be[0]|((uint32_t)o0_be[1]<<8)|((uint32_t)o0_be[2]<<16)|((uint32_t)o0_be[3]<<24);
    std::vector<PtH> hyps = {
        {"le||0",    oid0_le_u32, 0},
        {"0||le",    0, oid0_le_u32},
        {"le||le",   oid0_le_u32, oid0_le_u32},
        {"be||0",    oid0_be_u32, 0},
        {"0||be",    0, oid0_be_u32},
        {"be||be",   oid0_be_u32, oid0_be_u32},
    };
    // Also try fg as if we need to DECRYPT: input=fg → output?
    // (not useful here; focus on encrypt direction)

    const uint32_t round_counts[] = {16, 32, 64};
    uint32_t hits = 0;

    for (size_t off = 0; off+16 <= dll.size(); off++) {
        const uint8_t* kb = dll.data()+off;
        uint32_t key[4];
        for (int i=0;i<4;i++) key[i]=(uint32_t)kb[i*4]|((uint32_t)kb[i*4+1]<<8)|((uint32_t)kb[i*4+2]<<16)|((uint32_t)kb[i*4+3]<<24);

        for (const auto& hyp : hyps) {
            for (uint32_t nr : round_counts) {
                uint32_t v[2] = {hyp.v0, hyp.v1};
                if (use_xtea) xtea_encrypt(v, key, nr);
                else          tea_encrypt(v, key, nr);

                // Compare output LE bytes with fg of first pair
                uint8_t out[8];
                for (int i=0;i<4;i++) out[i]=(uint8_t)(v[0]>>(i*8));
                for (int i=0;i<4;i++) out[4+i]=(uint8_t)(v[1]>>(i*8));
                if (memcmp(out, pp0.fg, 8)!=0) continue;

                // Verify all pairs
                bool all_ok = true;
                for (size_t pi=1; pi<pairs.size()&&all_ok; pi++) {
                    const KnownPair& vp = pairs[pi];
                    uint8_t v_le[4]={(uint8_t)vp.oid,(uint8_t)(vp.oid>>8),(uint8_t)(vp.oid>>16),(uint8_t)(vp.oid>>24)};
                    uint8_t v_be[4]={(uint8_t)(vp.oid>>24),(uint8_t)(vp.oid>>16),(uint8_t)(vp.oid>>8),(uint8_t)vp.oid};
                    uint32_t vle=(uint32_t)v_le[0]|((uint32_t)v_le[1]<<8)|((uint32_t)v_le[2]<<16)|((uint32_t)v_le[3]<<24);
                    uint32_t vbe=(uint32_t)v_be[0]|((uint32_t)v_be[1]<<8)|((uint32_t)v_be[2]<<16)|((uint32_t)v_be[3]<<24);
                    uint32_t vv[2];
                    if (strcmp(hyp.n,"le||0")==0)  { vv[0]=vle; vv[1]=0; }
                    else if (strcmp(hyp.n,"0||le")==0)  { vv[0]=0; vv[1]=vle; }
                    else if (strcmp(hyp.n,"le||le")==0) { vv[0]=vle; vv[1]=vle; }
                    else if (strcmp(hyp.n,"be||0")==0)  { vv[0]=vbe; vv[1]=0; }
                    else if (strcmp(hyp.n,"0||be")==0)  { vv[0]=0; vv[1]=vbe; }
                    else { vv[0]=vbe; vv[1]=vbe; }
                    if (use_xtea) xtea_encrypt(vv, key, nr);
                    else          tea_encrypt(vv, key, nr);
                    uint8_t vout[8];
                    for (int i=0;i<4;i++) vout[i]=(uint8_t)(vv[0]>>(i*8));
                    for (int i=0;i<4;i++) vout[4+i]=(uint8_t)(vv[1]>>(i*8));
                    if (memcmp(vout, vp.fg, 8)!=0) all_ok=false;
                }
                if (all_ok) {
                    printf("  *** %s KEY FOUND! off=0x%08zX rounds=%u hyp=%s\n",algo,off,nr,hyp.n);
                    printf("      key: "); for (int i=0;i<16;i++) printf("%02X ",kb[i]); printf("\n");
                    hits++;
                }
            }
        }
    }
    if (hits==0) printf("  No %s key found.\n", algo);
}

// ---------------------------------------------------------------------------
// RC4 keystream
// ---------------------------------------------------------------------------
static void rc4_keystream(const uint8_t* key, size_t klen, uint8_t* out, size_t out_len) {
    uint8_t S[256];
    for (int i = 0; i < 256; i++) S[i] = (uint8_t)i;
    int j = 0;
    for (int i = 0; i < 256; i++) {
        j = (j + S[i] + key[i % klen]) & 255;
        uint8_t t = S[i]; S[i] = S[j]; S[j] = t;
    }
    int ii = 0; j = 0;
    for (size_t k = 0; k < out_len; k++) {
        ii = (ii + 1) & 255;
        j = (j + S[ii]) & 255;
        uint8_t t = S[ii]; S[ii] = S[j]; S[j] = t;
        out[k] = S[(uint8_t)((S[ii] + S[j]) & 255)];
    }
}

// Test RC4: OID as key, check all 8-byte windows in first 20 bytes of keystream
static void test_rc4_oid_key(const std::vector<KnownPair>& pairs) {
    printf("\n=== RC4 with OID as key ===\n");
    bool any = false;
    struct KFmt { const char* name; int klen; bool be; };
    const KFmt kfmts[] = { {"le4",4,false}, {"be4",4,true}, {"le16",16,false} };
    for (const auto& kf : kfmts) {
        for (int off = 0; off <= 12; off++) {
            bool all_ok = true;
            for (const auto& pp : pairs) {
                uint8_t oid_le[16] = {};
                oid_le[0]=(uint8_t)pp.oid; oid_le[1]=(uint8_t)(pp.oid>>8);
                oid_le[2]=(uint8_t)(pp.oid>>16); oid_le[3]=(uint8_t)(pp.oid>>24);
                uint8_t oid_be[4] = {(uint8_t)(pp.oid>>24),(uint8_t)(pp.oid>>16),(uint8_t)(pp.oid>>8),(uint8_t)pp.oid};
                uint8_t ks[20];
                rc4_keystream(kf.be ? oid_be : oid_le, kf.klen, ks, 20);
                if (memcmp(ks+off, pp.fg, 8) != 0) { all_ok = false; break; }
            }
            if (all_ok) {
                printf("  *** MATCH! key=OID_%s off=%d for ALL %zu pairs!\n",
                       kf.name, off, pairs.size());
                any = true;
            }
        }
    }
    if (!any) printf("  No RC4 match found.\n");
}

// ---------------------------------------------------------------------------
// 64-bit hash tests
// ---------------------------------------------------------------------------
static uint64_t fnv1a_64(const uint8_t* d, size_t n) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 1099511628211ULL;
    return h;
}
static uint64_t murmur2_64(const uint8_t* data, size_t len, uint64_t seed = 0) {
    const uint64_t m = 0xc6a4a7935bd1e995ULL; const int r = 47;
    uint64_t h = seed ^ (len * m);
    const uint8_t* end = data + (len/8)*8;
    while (data != end) { uint64_t k; memcpy(&k,data,8); data+=8;
        k*=m; k^=k>>r; k*=m; h^=k; h*=m; }
    switch (len&7) {
        case 7: h^=(uint64_t)data[6]<<48; [[fallthrough]];
        case 6: h^=(uint64_t)data[5]<<40; [[fallthrough]];
        case 5: h^=(uint64_t)data[4]<<32; [[fallthrough]];
        case 4: h^=(uint64_t)data[3]<<24; [[fallthrough]];
        case 3: h^=(uint64_t)data[2]<<16; [[fallthrough]];
        case 2: h^=(uint64_t)data[1]<<8;  [[fallthrough]];
        case 1: h^=(uint64_t)data[0]; h*=m;
    }
    h^=h>>r; h*=m; h^=h>>r; return h;
}
static void test_64bit_hashes(const std::vector<KnownPair>& pairs) {
    printf("\n=== 64-bit hash tests (FNV1a-64, MurmurHash2-64) ===\n");
    bool any = false;
    struct H64Fmt { const char* name; bool be_input; uint64_t(*fn)(const uint8_t*,size_t,uint64_t); uint64_t seed; };
    for (const auto& pp : pairs) {
        uint8_t le4[4] = {(uint8_t)pp.oid,(uint8_t)(pp.oid>>8),(uint8_t)(pp.oid>>16),(uint8_t)(pp.oid>>24)};
        uint8_t be4[4] = {(uint8_t)(pp.oid>>24),(uint8_t)(pp.oid>>16),(uint8_t)(pp.oid>>8),(uint8_t)pp.oid};
        uint8_t le8[8] = {}; memcpy(le8, le4, 4);
        uint64_t hv[6] = {
            fnv1a_64(le4,4), fnv1a_64(be4,4), fnv1a_64(le8,8),
            murmur2_64(le4,4), murmur2_64(be4,4), murmur2_64(le8,8)
        };
        const char* hn[6] = {"fnv64(le4)","fnv64(be4)","fnv64(le8)","mm64(le4)","mm64(be4)","mm64(le8)"};
        for (int hi = 0; hi < 6; hi++) {
            uint8_t h_le[8], h_be[8];
            for (int i=0;i<8;i++) h_le[i]=(uint8_t)(hv[hi]>>(i*8));
            for (int i=0;i<8;i++) h_be[i]=(uint8_t)(hv[hi]>>((7-i)*8));
            if (memcmp(h_le, pp.fg, 8)==0) { printf("  MATCH: %s LE = f||g OID=%05X\n",hn[hi],pp.oid); any=true; }
            if (memcmp(h_be, pp.fg, 8)==0) { printf("  MATCH: %s BE = f||g OID=%05X\n",hn[hi],pp.oid); any=true; }
        }
    }
    if (!any) printf("  No 64-bit hash match found.\n");
}

// ---------------------------------------------------------------------------
// Comprehensive keyless integer mixing function tests
// These are single-word (32-bit) functions that need no key.
// We also try paired (f=mix1(OID), g=mix2(OID)) or (f=mix(OID), g=mix(OID^C))
// ---------------------------------------------------------------------------
static uint32_t wang_hash(uint32_t a) {
    a = (a^61)^(a>>16); a += a<<3; a ^= a>>4; a *= 0x27D4EB2DU; a ^= a>>15; return a;
}
static uint32_t jenkins_finalizer(uint32_t a) {
    a -= a<<6; a ^= a>>17; a -= a<<9; a ^= a<<4; a -= a>>3; a ^= a<<10; a ^= a>>15; return a;
}
static uint32_t murmur3_fmix(uint32_t h) {
    h ^= h>>16; h *= 0x85EBCA6BU; h ^= h>>13; h *= 0xC2B2AE35U; h ^= h>>16; return h;
}
static uint32_t lowbias32(uint32_t x) {
    x ^= x>>17; x *= 0xBF324C81U; x ^= x>>11; x *= 0x68BCE5F5U; x ^= x>>15; return x;
}
static uint32_t triple32(uint32_t x) {
    x ^= x>>17; x *= 0x45D9F3BU; x ^= x>>16; x *= 0x45D9F3BU; x ^= x>>16; return x;
}
static uint32_t hash32shift(uint32_t key) {
    key = ~key + (key<<15); key ^= key>>12; key += key<<2; key ^= key>>4;
    key *= 2057; key ^= key>>16; return key;
}
static uint32_t hash32shiftmult(uint32_t key) {
    key = (key>>14)^(key<<18); key *= 69069; key += 0x9E3779B9U;
    key = (key>>11)^(key<<21); key *= 0x2036A7C5U; key ^= key>>16; return key;
}
static uint32_t halfavalanche(uint32_t val) {
    val = val + 0x9E3779B9U + (val<<6) + (val>>2);
    val ^= val>>16; val *= 0x45D9F3BU; val ^= val>>11; return val;
}
// Simple 64-bit combine: run mix on OID and OID+stride to get f and g
static void test_keyless_mixing(const std::vector<KnownPair>& pairs) {
    printf("\n=== Keyless 32-bit integer mixing tests ===\n");
    struct Mix32 { const char* name; uint32_t(*fn)(uint32_t); };
    Mix32 mixes[] = {
        {"wang",     wang_hash},
        {"jenkins",  jenkins_finalizer},
        {"murmur3",  murmur3_fmix},
        {"lowbias",  lowbias32},
        {"triple32", triple32},
        {"hash32s",  hash32shift},
        {"h32sm",    hash32shiftmult},
        {"halfav",   halfavalanche},
    };
    bool any = false;
    for (const auto& m : mixes) {
        // Test: f=mix(OID), g=mix(OID+1) or mix(OID^C) for various C
        bool all_f_match = true;
        for (const auto& p : pairs) {
            if (m.fn(p.oid) != *(uint32_t*)p.fg) { all_f_match=false; break; }
        }
        if (all_f_match) {
            printf("  *** mix=%s gives f for ALL OIDs!\n", m.name);
            any = true;
            // Now check if g = mix(OID+C) for any small C
            for (uint32_t C = 0; C < 0x1000000U; C++) {
                bool ok = true;
                for (const auto& p : pairs)
                    if (m.fn(p.oid ^ C) != *(uint32_t*)(p.fg+4)) { ok=false; break; }
                if (ok) { printf("    g = %s(OID ^ 0x%08X)\n", m.name, C); break; }
            }
        }
        // Also: pair (f,g) together — check if mix64(OID) = f||g
        // where mix64 = 64-bit version by running mix on lower/upper halves
        bool all_fg_match = true;
        for (const auto& p : pairs) {
            uint32_t ff = m.fn(p.oid);
            uint32_t gg = m.fn(p.oid ^ 0x9E3779B9U);
            if (ff != *(uint32_t*)p.fg || gg != *(uint32_t*)(p.fg+4)) { all_fg_match=false; break; }
        }
        if (all_fg_match) { printf("  *** (f,g) = (%s(OID), %s(OID^GR)) for ALL!\n",m.name,m.name); any=true; }
    }
    // Test affine: f = (OID * A + B) mod 2^32
    // Solve from two pairs using modular inverse
    {
        auto modinv32 = [](uint32_t a) -> uint32_t {
            // a must be odd; iterative inversion mod 2^32
            uint32_t v = 1;
            for (int i=0;i<31;i++) { v *= 2 - a*v; }
            return v;
        };
        // Use pairs[0] and pairs[1] to solve
        if (pairs.size() >= 2) {
            uint32_t oid0 = pairs[0].oid, f0 = *(uint32_t*)pairs[0].fg;
            uint32_t oid1 = pairs[1].oid, f1 = *(uint32_t*)pairs[1].fg;
            uint32_t diff_oid = oid1 - oid0;
            uint32_t diff_f   = f1 - f0;
            // If diff_oid is odd, compute A = diff_f * modinv(diff_oid)
            if (diff_oid & 1) {
                uint32_t A = diff_f * modinv32(diff_oid);
                uint32_t B = f0 - A * oid0;
                bool ok = true;
                for (const auto& p : pairs)
                    if (p.oid * A + B != *(uint32_t*)p.fg) { ok=false; break; }
                if (ok) { printf("  *** f = OID*0x%08X + 0x%08X (affine) for all pairs!\n",A,B); any=true; }
            }
        }
    }
    if (!any) printf("  No keyless mixing match.\n");
}

// ---------------------------------------------------------------------------
// AES-128 with OID as key, constant plaintext — all key placements and output ranges
// ---------------------------------------------------------------------------
static void test_oid_as_aes_key(const std::vector<KnownPair>& pairs) {
    printf("\n=== AES-128 with OID as key, constant plaintext ===\n");
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0))) return;
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
        (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
    DWORD obj_sz = 0, res = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&obj_sz, sizeof(DWORD), &res, 0);
    std::vector<uint8_t> kobj(obj_sz);
    struct KBlob { BCRYPT_KEY_DATA_BLOB_HEADER h; uint8_t key[16]; } kb;
    kb.h.dwMagic=BCRYPT_KEY_DATA_BLOB_MAGIC; kb.h.dwVersion=BCRYPT_KEY_DATA_BLOB_VERSION1; kb.h.cbKeyData=16;
    bool any = false;

    struct PtCfg { const char* name; uint8_t pt[16]; };
    PtCfg ptcs[] = {
        {"zeros",  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},
        {"0xFF",   {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}},
        {"0x01×16",{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}},
    };
    // key formats: oid_le placed at byte offset kp, rest 0
    const int kpos[] = {0, 4, 8, 12};
    const bool kbe[] = {false, false, false, false, true, true, true, true};
    const int kpos_be[] = {0, 4, 8, 12};

    for (int kpi = 0; kpi < 4; kpi++) {
        for (bool be_oid : {false, true}) {
            for (const auto& ptcfg : ptcs) {
                for (int out_off = 0; out_off <= 8; out_off += 4) {
                    bool all_ok = true;
                    for (const auto& pp : pairs) {
                        uint8_t oid_le[4] = {(uint8_t)pp.oid,(uint8_t)(pp.oid>>8),(uint8_t)(pp.oid>>16),(uint8_t)(pp.oid>>24)};
                        uint8_t oid_be[4] = {(uint8_t)(pp.oid>>24),(uint8_t)(pp.oid>>16),(uint8_t)(pp.oid>>8),(uint8_t)pp.oid};
                        memset(kb.key, 0, 16);
                        memcpy(kb.key + kpos[kpi], be_oid ? oid_be : oid_le, 4);
                        BCRYPT_KEY_HANDLE hKey = nullptr;
                        if (!BCRYPT_SUCCESS(BCryptImportKey(hAlg, nullptr, BCRYPT_KEY_DATA_BLOB,
                                                            &hKey, kobj.data(), obj_sz,
                                                            (PUCHAR)&kb, sizeof(kb), 0))) { all_ok=false; break; }
                        uint8_t ct[16]; ULONG rlen=0;
                        bool ok = BCRYPT_SUCCESS(BCryptEncrypt(hKey, (PUCHAR)ptcfg.pt, 16,
                                                                nullptr, nullptr, 0, ct, 16, &rlen, 0));
                        BCryptDestroyKey(hKey);
                        if (!ok || memcmp(ct+out_off, pp.fg, 8)!=0) { all_ok=false; break; }
                    }
                    if (all_ok) {
                        printf("  *** MATCH! key=OID_%s@%d pt=%s out[%d..%d] ALL %zu pairs!\n",
                               be_oid?"be":"le", kpos[kpi], ptcfg.name, out_off, out_off+7, pairs.size());
                        any = true;
                    }
                }
            }
        }
    }
    BCryptCloseAlgorithmProvider(hAlg, 0);
    if (!any) printf("  No match found.\n");
}

// ---------------------------------------------------------------------------
// Reversed AES DLL scan: OID as key, DLL windows as plaintext
// Checks all 3 output ranges (0..7, 4..11, 8..15)
// ---------------------------------------------------------------------------
static void scan_dll_aes_reversed(const char* dll_path, const std::vector<KnownPair>& pairs) {
    printf("\n=== DLL AES reversed scan (OID=key, DLL=plain): %s ===\n", dll_path);
    std::ifstream f(dll_path, std::ios::binary);
    if (!f) { printf("  Cannot open DLL\n"); return; }
    std::vector<uint8_t> dll((std::istreambuf_iterator<char>(f)), {});
    size_t nw = dll.size() >= 16 ? dll.size()-15 : 0;
    printf("  DLL size: %zu bytes, %zu windows...\n", dll.size(), nw);

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0))) return;
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
        (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
    DWORD obj_sz = 0, res = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&obj_sz, sizeof(DWORD), &res, 0);

    struct KBlob { BCRYPT_KEY_DATA_BLOB_HEADER h; uint8_t key[16]; } kb;
    kb.h.dwMagic=BCRYPT_KEY_DATA_BLOB_MAGIC; kb.h.dwVersion=BCRYPT_KEY_DATA_BLOB_VERSION1; kb.h.cbKeyData=16;

    // Precompute one key handle per pair (OID_le padded to 16 bytes)
    std::vector<BCRYPT_KEY_HANDLE> handles(pairs.size(), nullptr);
    std::vector<std::vector<uint8_t>> kobjs(pairs.size(), std::vector<uint8_t>(obj_sz));
    for (size_t i = 0; i < pairs.size(); i++) {
        memset(kb.key, 0, 16);
        kb.key[0]=(uint8_t)pairs[i].oid; kb.key[1]=(uint8_t)(pairs[i].oid>>8);
        kb.key[2]=(uint8_t)(pairs[i].oid>>16); kb.key[3]=(uint8_t)(pairs[i].oid>>24);
        BCryptImportKey(hAlg, nullptr, BCRYPT_KEY_DATA_BLOB,
                        &handles[i], kobjs[i].data(), obj_sz, (PUCHAR)&kb, sizeof(kb), 0);
    }

    uint32_t hits = 0;
    const int check_offs[] = {0, 4, 8};

    for (size_t off = 0; off + 16 <= dll.size(); ++off) {
        const uint8_t* W = dll.data() + off;
        if (!handles[0]) continue;
        uint8_t ct0[16]; ULONG rlen = 0;
        if (!BCRYPT_SUCCESS(BCryptEncrypt(handles[0], (PUCHAR)W, 16, nullptr,
                                          nullptr, 0, ct0, 16, &rlen, 0))) continue;

        int moff = -1;
        for (int co : check_offs) if (memcmp(ct0+co, pairs[0].fg, 8)==0) { moff=co; break; }
        if (moff < 0) continue;

        bool all_ok = true;
        for (size_t pi = 1; pi < pairs.size() && all_ok; pi++) {
            if (!handles[pi]) { all_ok=false; break; }
            uint8_t cti[16]; ULONG r2=0;
            if (!BCRYPT_SUCCESS(BCryptEncrypt(handles[pi], (PUCHAR)W, 16, nullptr,
                                               nullptr, 0, cti, 16, &r2, 0))) { all_ok=false; break; }
            if (memcmp(cti+moff, pairs[pi].fg, 8)!=0) all_ok=false;
        }
        if (all_ok) {
            printf("  *** REVERSED AES! DLL+0x%08zX out[%d..%d]\n", off, moff, moff+7);
            printf("      plain: "); for (int i=0;i<16;i++) printf("%02X ",W[i]); printf("\n");
            hits++;
        }
    }
    for (auto h : handles) if (h) BCryptDestroyKey(h);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    if (hits == 0) printf("  No reversed AES match found.\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    // Database list: path, adssys password, short label
    struct DbInfo { const char* path; const char* pw; const char* label; };
    const DbInfo dbs[] = {
        { "F:\\OpenADS\\testdata\\pmsys\\pmsys.add", "pmsys", "pmsys-test" },
        { "E:\\AdsData\\PMSys\\pmsys.add",           "pmsys", "pmsys"      },
        { "E:\\AdsData\\cqm\\mp.add",                "mp10",  "cqm"        },
        { "E:\\AdsData\\cqm-2021\\mp.add",           "mp10",  "cqm-2021"   },
        { "E:\\AdsData\\saa\\mp.add",                "mp10",  "saa"        },
        { "E:\\AdsData\\saa2\\mp.add",               "mp10",  "saa2"       },
        { "E:\\AdsData\\sfi\\mp.add",                "mp10",  "sfi"        },
        { "E:\\AdsData\\sfi-2021\\mp.add",           "mp10",  "sfi-2021"   },
    };

    printf("=== SAP DB: built-in group cipher analysis — keystream/DB-key hypothesis ===\n\n");

    std::vector<std::pair<std::string, std::vector<User>>> all_dbs;

    for (const auto& db : dbs) {
        auto users = read_add_binary(db.path);
        if (users.empty()) continue;

        printf("\n[DB: %s]\n", db.path);

        // Extract Database record prop273
        uint8_t db_prop[273]; uint16_t db_plen = 0;
        bool have_db_rec = read_db_record(db.path, db_prop, &db_plen);
        if (have_db_rec) {
            printf("  DB record: plen=%u\n", db_plen);
            printf("  DB prop[0..%u]: ", db_plen < 48 ? db_plen : 48);
            for (size_t j = 0; j < (size_t)db_plen && j < 48; ++j) printf("%02X ", db_prop[j]);
            if (db_plen > 48) printf("...");
            printf("\n");
        } else {
            printf("  (no Database record found)\n");
            memset(db_prop, 0xFF, 273);
        }

        // Get ground-truth memberships from SAP DLL
        query_sap(db.path, db.pw, users);

        // Keystream/DB-key analysis
        keystream_analysis(users, db.label, have_db_rec ? db_prop : nullptr);

        all_dbs.push_back({db.label, std::move(users)});
    }

    cross_db_analysis(all_dbs);

    // Build known (OID, f, g) pairs from users with confirmed memberships.
    // For bits=0 users, cipher[8-11] = g(OID) directly (no membership XOR).
    // For other users, g = cipher[8-11] XOR membership_encoding.
    printf("\n=== Building known-pair table for DLL key scan ===\n");
    std::map<uint32_t, std::array<uint8_t,8>> oid_to_fg;
    for (const auto& [label, users] : all_dbs) {
        for (const auto& u : users) {
            if (!u.has_cipher) continue;
            // Compute g = keystream[8-11]
            uint8_t ks[4]; memcpy(ks, u.cipher+8, 4);
            if (u.db_admin)  xor4(ks, DELTA_ADMIN,  ks);
            if (u.db_backup) xor4(ks, DELTA_BACKUP, ks);
            if (u.db_debug)  xor4(ks, DELTA_DEBUG,  ks);
            // f = cipher[4-7]
            std::array<uint8_t,8> fg;
            memcpy(fg.data(), u.cipher+4, 4);
            memcpy(fg.data()+4, ks, 4);
            if (oid_to_fg.count(u.oid)) {
                // Verify consistency
                if (memcmp(oid_to_fg[u.oid].data(), fg.data(), 8) != 0) {
                    printf("  INCONSISTENCY for OID=%04X in %s!\n", u.oid, label.c_str());
                }
            } else {
                oid_to_fg[u.oid] = fg;
            }
        }
    }
    printf("  Total unique (OID, f, g) pairs: %zu\n", oid_to_fg.size());

    std::vector<KnownPair> pairs;
    for (auto& [oid, fg] : oid_to_fg) {
        KnownPair p; p.oid = oid; memcpy(p.fg, fg.data(), 8);
        pairs.push_back(p);
        printf("    OID=%05X  f=%02X%02X%02X%02X  g=%02X%02X%02X%02X\n",
               oid, fg[0],fg[1],fg[2],fg[3],fg[4],fg[5],fg[6],fg[7]);
    }

    // Collect 200 more consecutive OID pairs from a fresh test database
    {
        printf("\n=== Collecting fresh consecutive OID pairs ===\n");
        // Use live database so remote server writes proper cipher blocks
        const char* base_add = "E:\\AdsData\\PMSys\\pmsys.add";
        auto fresh = collect_consecutive_pairs(200, base_add, "pmsys");
        if (!fresh.empty()) {
            // Add fresh pairs to main pair set (avoiding duplicates)
            std::set<uint32_t> existing_oids;
            for (const auto& p : pairs) existing_oids.insert(p.oid);
            for (const auto& p : fresh)
                if (!existing_oids.count(p.oid)) pairs.push_back(p);
            printf("  Total pairs after merge: %zu\n", pairs.size());
            differential_analysis(fresh);
        }
    }

    if (pairs.size() >= 2) {
        // 1. Keyless hash tests (fast)
        keyless_hash_test(pairs);

        // 2. 64-bit hashes (FNV1a-64, MurmurHash2-64)
        test_64bit_hashes(pairs);

        // 2b. Keyless 32-bit integer mixing functions
        test_keyless_mixing(pairs);

        // 3. RC4 with OID as key
        test_rc4_oid_key(pairs);

        // 4. AES-128 with OID as key, constant plaintext
        test_oid_as_aes_key(pairs);

        const char* dll_paths[] = {
            "f:\\Ads11\\ace64.dll",
            "f:\\Ads11\\adsloc64.dll",
            "C:\\Program Files\\Advantage 11.10\\ace64.dll",
            "C:\\Program Files\\Advantage 11.10\\adsloc64.dll",
            nullptr
        };
        for (const char** p = dll_paths; *p; ++p) {
            DWORD attr = GetFileAttributesA(*p);
            if (attr == INVALID_FILE_ATTRIBUTES) continue;
            // 5. DES-ECB 8-byte key scan
            scan_dll_for_des_key(*p, pairs);
            // 6. AES-128 16-byte key scan (DLL as key, OID as plain)
            scan_dll_for_aes_key(*p, pairs);
            // 7. Reversed AES: OID as key, DLL window as plain
            scan_dll_aes_reversed(*p, pairs);
            // 8. TEA 16-byte key scan
            scan_dll_for_tea_key(*p, pairs, false);
            // 9. XTEA 16-byte key scan
            scan_dll_for_tea_key(*p, pairs, true);
        }

        // 10. RC4: OID as part of key (FIXED: compare only f||g, not random bytes)
        for (const char** p = dll_paths; *p; ++p) {
            DWORD attr = GetFileAttributesA(*p);
            if (attr == INVALID_FILE_ATTRIBUTES) continue;
            scan_dll_rc4_oid_keyed(*p, pairs);
        }
        // 11. RC4: fixed DLL key, OID*scale = offset into keystream
        for (const char** p = dll_paths; *p; ++p) {
            DWORD attr = GetFileAttributesA(*p);
            if (attr == INVALID_FILE_ATTRIBUTES) continue;
            scan_dll_rc4_oid_offset(*p, pairs);
        }
        // 12. Check if any f or g values appear verbatim in the DLL (lookup table?)
        for (const char** p = dll_paths; *p; ++p) {
            DWORD attr = GetFileAttributesA(*p);
            if (attr == INVALID_FILE_ATTRIBUTES) continue;
            scan_dll_fg_lookup(*p, pairs);
        }
        // New: scan for bijective S-boxes in each DLL
        for (const char** p = dll_paths; *p; ++p) {
            DWORD attr = GetFileAttributesA(*p);
            if (attr == INVALID_FILE_ATTRIBUTES) continue;
            scan_dll_sbox(*p, pairs);
        }
    } else {
        printf("  (need >= 2 pairs for DLL scan verification)\n");
    }

    // New hash/LFSR tests
    test_xorshift(pairs);

    printf("\n=== Done ===\n");
    return 0;
}
