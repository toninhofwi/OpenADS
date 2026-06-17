#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <fstream>
#include <string>

static uint16_t le16(const uint8_t* b, size_t o) {
    return (uint16_t)b[o]|((uint16_t)b[o+1]<<8);
}
static uint32_t le32(const uint8_t* b, size_t o) {
    return (uint32_t)b[o]|((uint32_t)b[o+1]<<8)|((uint32_t)b[o+2]<<16)|((uint32_t)b[o+3]<<24);
}
static std::string trim(const uint8_t* b, size_t o, size_t len) {
    std::string s((const char*)b+o, len);
    auto n = s.find('\0'); if (n != std::string::npos) s.resize(n);
    while (!s.empty() && (uint8_t)s.back() <= 0x20) s.pop_back();
    return s;
}
static void hex(const uint8_t* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        printf("%02X ", b[i]);
        if ((i+1)%16==0) printf("\n    ");
    }
    printf("\n");
}

int main(int argc, char** argv) {
    const char* path = argc>1 ? argv[1] : "f:\\OpenADS\\testdata\\pmsys\\pmsys.add";
    std::ifstream f(path, std::ios::binary);
    if (!f) { printf("Cannot open %s\n", path); return 1; }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), {});
    printf("File size: %zu\n", buf.size());
    if (buf.size() < 20 || memcmp(buf.data(), "ADS Data Dictionary", 19) != 0) {
        printf("Not an ADS binary .add file\n"); return 1;
    }
    const uint8_t* B = buf.data();
    uint32_t hdr_len = le32(B, 0x20);
    uint32_t rec_len = le32(B, 0x24);
    if (hdr_len >= buf.size() || rec_len < 100) { printf("Bad header\n"); return 1; }
    size_t total = (buf.size() - hdr_len) / rec_len;
    printf("hdr=%u rec_len=%u total=%zu\n\n", hdr_len, rec_len, total);

    for (size_t i = 0; i < total && i < 2000; ++i) {
        size_t base = hdr_len + (size_t)i * rec_len;
        if (base + rec_len > buf.size()) break;
        if (B[base] != 0x04) continue;
        std::string type = trim(B, base+13, 10);
        if (type != "User") continue;
        std::string name = trim(B, base+23, 200);
        uint32_t oid = le32(B, base+4);
        uint16_t plen = le16(B, base+223);
        printf("=== User: [%s]  OID=%08X  plen=%u ===\n", name.c_str(), oid, plen);

        // Show prop273 area (225..225+273)
        size_t ps = base + 225;
        size_t pe = ps + 273;
        if (pe > buf.size()) pe = buf.size();
        printf("  prop273[0..272]:\n    ");
        hex(B+ps, pe-ps);

        // Post-token section starts after plen bytes of XOR tokens
        size_t post = ps + plen;
        printf("  post-token section at offset=%zu (within record, base=%zu):\n", post-base, base);
        if (post + 2 > base + 225 + 273) { printf("  (no post-token area)\n\n"); continue; }

        // Show the post-token section up to end of prop273 area
        size_t post_end = ps + 273;
        printf("  post-token bytes (%zu total):\n    ", post_end-post);
        hex(B+post, post_end-post);

        // Try to find the cipher block: FF*6, 02 00 00 00, FF*4, 02 00 00 00, len16, 16bytes
        for (size_t j = post; j + 36 <= post_end; ++j) {
            if (B[j]!=0xFF||B[j+1]!=0xFF||B[j+2]!=0xFF) continue;
            if (B[j+3]!=0xFF||B[j+4]!=0xFF||B[j+5]!=0xFF) continue;
            if (le32(B,j+6)!=0x00000002u) continue;
            if (B[j+10]!=0xFF||B[j+11]!=0xFF||B[j+12]!=0xFF||B[j+13]!=0xFF) continue;
            if (le32(B,j+14)!=0x00000002u) continue;
            uint16_t cb = le16(B, j+18);
            printf("  ** cipher block marker at offset %zu within file:\n", j);
            printf("     cb_len=%u (0x%04X)\n", cb, cb);
            if (cb == 16 && j+20+16 <= buf.size()) {
                printf("     cipher block: ");
                hex(B+j+20, 16);
            }
            break;
        }

        // Also: show TLV field[4] structure
        // The TLV area starts at prop273 after plen
        // TLV entry 2 = the 81-byte blob
        printf("  Searching for 81-byte blob (TLV entry 2 = 51 00 ...):\n");
        // TLV field[4] is indexed differently — look for 0x51 0x00 pattern
        for (size_t j = post; j + 3 <= post_end; ++j) {
            if (B[j]==0x51 && B[j+1]==0x00) {
                printf("    Found 0x51 0x00 at offset %zu (within file)\n", j);
                printf("    Following bytes: ");
                hex(B+j, std::min<size_t>(85, post_end-j));
                break;
            }
        }
        printf("\n");
    }
    return 0;
}
