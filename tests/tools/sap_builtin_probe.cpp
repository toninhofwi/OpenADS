// sap_builtin_probe.cpp
// Investigate how DB:Admin, DB:Backup, DB:Debug membership is encoded
// in the binary .add file's User record property zone.
//
// For each User record, dumps:
//   - info1, info2 from the record
//   - plen and the token section (cf, ntok)
//   - The post-token section bytes (after XOR tokens)
//   - The FULL 273-byte property zone
//
// Cross-referenced against AdsDDGetUserProperty(1102) ground truth so
// we can visually correlate binary patterns with DB: membership.
//
// Build (MSVC x64, from tests/tools/):
//   cl.exe sap_builtin_probe.cpp /Dx64 /EHsc /I f:\Ads11 /Fe:sap_builtin_probe.exe /link f:\Ads11\ace64.lib

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "ace.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <fstream>

static uint32_t le32b(const uint8_t* b, size_t o) {
    return (uint32_t)b[o] | ((uint32_t)b[o+1]<<8) | ((uint32_t)b[o+2]<<16) | ((uint32_t)b[o+3]<<24);
}
static uint16_t le16b(const uint8_t* b, size_t o) {
    return (uint16_t)b[o] | ((uint16_t)b[o+1]<<8);
}
static std::string trim_field(const uint8_t* b, size_t o, size_t len) {
    std::string s((const char*)b + o, len);
    auto n = s.find('\0'); if (n != std::string::npos) s.resize(n);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
static void hexrow(const uint8_t* b, size_t len, size_t base_off = 0, int cols = 16) {
    for (size_t i = 0; i < len; i += cols) {
        printf("    %04X: ", (unsigned)(base_off + i));
        for (int j = 0; j < cols; ++j) {
            if (i+(size_t)j < len) printf("%02X ", b[i+j]); else printf("   ");
        }
        printf(" |");
        for (int j = 0; j < cols && i+(size_t)j < len; ++j) {
            uint8_t c = b[i+j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("|\n");
    }
}

static std::set<std::string> parse_groups(const char* buf, int len) {
    std::set<std::string> v;
    std::string cur;
    for (int i = 0; i < len; ++i) {
        char c = buf[i];
        if (c == '\0' || c == ';') { if (!cur.empty()) { v.insert(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) v.insert(cur);
    return v;
}

static const char* yn(bool v) { return v ? "YES" : "no"; }

int main(int argc, char** argv) {
    const char* add_path  = (argc > 1) ? argv[1] : "E:\\AdsData\\cqm-2021\\mp.add";
    const char* adssys_pw = (argc > 2) ? argv[2] : "mp10";

    printf("=== DB: built-in group binary probe ===\n");
    printf("DD: %s\n\n", add_path);

    // --- Read binary .add ---
    std::ifstream f(add_path, std::ios::binary);
    if (!f) { printf("Cannot open %s\n", add_path); return 1; }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), {});
    const uint8_t* B = buf.data();

    uint32_t hdr_len = le32b(B, 0x20);
    uint32_t rec_len = le32b(B, 0x24);
    size_t total = (buf.size() - hdr_len) / rec_len;
    printf("hdr_len=%u  rec_len=%u  total_records=%zu\n\n", hdr_len, rec_len, total);

    struct UserInfo {
        uint32_t oid, info1, info2;
        std::string name;
        uint16_t plen;
        std::vector<uint8_t> prop273;  // exactly 273 bytes (padded with 0xFF if needed)
    };
    std::vector<UserInfo> users;

    for (size_t i = 0; i < total; ++i) {
        size_t base = hdr_len + i * rec_len;
        if (base + rec_len > buf.size()) break;
        if (B[base] != 0x04) continue;
        if (trim_field(B, base+13, 10) != "User") continue;
        std::string uname = trim_field(B, base+23, 200);
        if (uname.empty()) continue;

        UserInfo u;
        u.oid   = le32b(B, base+5);
        u.info1 = le32b(B, base+507);
        u.info2 = le32b(B, base+511);
        u.name  = uname;
        u.plen  = le16b(B, base+223);

        size_t ps = base + 225;
        size_t pe = std::min(ps + 273, buf.size());
        u.prop273.assign(B + ps, B + pe);
        u.prop273.resize(273, 0xFF);
        users.push_back(std::move(u));
    }
    printf("Found %zu User records.\n\n", users.size());

    // --- SAP query ---
    ADSHANDLE hConn = 0;
    UNSIGNED32 rc = AdsConnect60((UNSIGNED8*)add_path, ADS_LOCAL_SERVER,
                                  (UNSIGNED8*)"adssys", (UNSIGNED8*)adssys_pw, 0, &hConn);
    std::map<std::string, std::set<std::string>> sap_mbr;
    if (rc != 0) {
        printf("SAP connect failed (%u) — DB: labels will be absent\n\n", rc);
    } else {
        for (auto& u : users) {
            unsigned char mbuf[4096] = {}; UNSIGNED16 mlen = sizeof(mbuf);
            if (AdsDDGetUserProperty(hConn, (UNSIGNED8*)u.name.c_str(),
                                     ADS_DD_USER_GROUP_MEMBERSHIP, mbuf, &mlen) == 0 && mlen > 0)
                sap_mbr[u.name] = parse_groups((char*)mbuf, (int)mlen);
        }
        AdsDisconnect(hConn);
        printf("SAP membership queried for %zu users.\n\n", sap_mbr.size());
    }

    // --- Dump each user ---
    for (auto& u : users) {
        auto& grps = sap_mbr[u.name];
        bool isAdmin  = grps.count("DB:Admin")  > 0;
        bool isBackup = grps.count("DB:Backup") > 0;
        bool isDebug  = grps.count("DB:Debug")  > 0;

        printf("╔══ User: %-20s  OID=0x%08X\n", u.name.c_str(), u.oid);
        printf("║   info1=0x%08X  info2=0x%08X\n", u.info1, u.info2);
        printf("║   DB:Admin=%s  DB:Backup=%s  DB:Debug=%s\n",
               yn(isAdmin), yn(isBackup), yn(isDebug));

        uint16_t plen = u.plen;
        if (plen == 0xFFFFu) {
            printf("║   plen=NULL (property zone absent)\n╚\n\n");
            continue;
        }
        printf("║   plen=0x%04X (%u)\n", plen, plen);

        const uint8_t* P = u.prop273.data();

        // Token section
        size_t xoff = plen;
        if (xoff + 2 <= 273) {
            uint16_t cf = le16b(P, xoff);
            uint32_t ntok = (cf == 0xFFFFu || cf == 0) ? 0 : (uint32_t)cf / 4u;
            size_t post_off = xoff + 2 + ntok * 4;

            printf("║   Token zone: prop273[%zu] cf=0x%04X  ntok=%u  post-token→prop273[%zu]\n",
                   xoff, cf, ntok, post_off);

            // Dump XOR tokens
            if (ntok > 0) {
                printf("║   XOR tokens:");
                for (uint32_t s = 0; s < ntok; ++s) {
                    size_t to = xoff + 2 + s*4;
                    printf(" [%02X %02X %02X %02X]", P[to], P[to+1], P[to+2], P[to+3]);
                }
                printf("\n");
            }

            // Post-token section: from post_off to end of 273-byte zone
            if (post_off < 273) {
                size_t ptsz = 273 - post_off;
                // Find real end (trim trailing 0xFF)
                size_t real_end = ptsz;
                while (real_end > 0 && P[post_off + real_end - 1] == 0xFF) --real_end;

                if (real_end == 0) {
                    printf("║   Post-token: (all 0xFF — nothing encoded)\n");
                } else {
                    printf("║   Post-token (%zu non-FF bytes of %zu total):\n", real_end, ptsz);
                    hexrow(P + post_off, real_end, post_off);
                }
            }
        } else {
            printf("║   plen=%u >= 273, token section overruns zone\n", plen);
        }

        // Full 273-byte property zone
        printf("║   Full prop273 (273 bytes):\n");
        hexrow(P, 273, 0, 16);
        printf("╚\n\n");
    }

    // --- Summary table: post-token bytes 0..7 vs DB: membership ---
    printf("=== SUMMARY: first 16 bytes of post-token section per user ===\n");
    printf("%-20s  Admin Backup Debug  PostToken[0..15]\n", "User");
    printf("%s\n", std::string(80, '-').c_str());
    for (auto& u : users) {
        auto& grps = sap_mbr[u.name];
        bool isAdmin  = grps.count("DB:Admin")  > 0;
        bool isBackup = grps.count("DB:Backup") > 0;
        bool isDebug  = grps.count("DB:Debug")  > 0;
        if (u.plen == 0xFFFFu) {
            printf("%-20s  %-5s %-6s %-5s  (NULL prop)\n",
                   u.name.c_str(), yn(isAdmin), yn(isBackup), yn(isDebug));
            continue;
        }
        const uint8_t* P = u.prop273.data();
        size_t xoff = u.plen;
        uint16_t cf = (xoff+2 <= 273) ? le16b(P, xoff) : 0xFFFF;
        uint32_t ntok = (cf == 0xFFFFu || cf == 0) ? 0 : (uint32_t)cf / 4u;
        size_t post_off = xoff + 2 + ntok * 4;
        printf("%-20s  %-5s %-6s %-5s  ", u.name.c_str(), yn(isAdmin), yn(isBackup), yn(isDebug));
        for (int k = 0; k < 16; ++k) {
            size_t idx = post_off + k;
            if (idx < 273) printf("%02X ", P[idx]); else printf("-- ");
        }
        printf("\n");
    }
    printf("\n");

    return 0;
}
