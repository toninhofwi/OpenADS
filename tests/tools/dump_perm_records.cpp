// dump_perm_records.cpp
// Read binary pmsys.add and dump all Permission records with their info2 values.
// This lets us see what is ACTUALLY stored vs what AdsDDGetPermissions returns.
//
// Build:
//   cl.exe dump_perm_records.cpp /Fe:dump_perm_records.exe /link
// Run:
//   dump_perm_records.exe [path_to.add]

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>

// LE readers
static uint32_t le32(const char* buf, size_t off) {
    const auto* p = reinterpret_cast<const unsigned char*>(buf + off);
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint16_t le16(const char* buf, size_t off) {
    const auto* p = reinterpret_cast<const unsigned char*>(buf + off);
    return (uint16_t)p[0] | ((uint16_t)p[1]<<8);
}

// Trim fixed-length field (null/space terminated)
static std::string trim(const char* buf, size_t off, size_t maxlen) {
    std::string s(buf + off, maxlen);
    size_t n = s.find_first_of('\0');
    if (n != std::string::npos) s.resize(n);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

static std::string decode_perm(uint32_t mask) {
    std::string s;
    if (!mask) return "(none)";
    struct B { uint32_t bit; const char* name; };
    static const B bits[] = {
        {0x00000001,"READ"},  {0x00000002,"UPDATE"}, {0x00000004,"EXEC"},
        {0x00000008,"INHERIT"},{0x00000010,"INSERT"},{0x00000020,"DELETE"},
        {0x00000040,"ACCESS"},{0x00000080,"CREATE"}, {0x00000100,"ALTER"},
        {0x00000200,"DROP"},  {0x80000000,"WITH_GRANT"}
    };
    for (auto& b : bits)
        if (mask & b.bit) { if (!s.empty()) s+="|"; s+=b.name; }
    uint32_t known=0;
    for (auto& b : bits) known|=b.bit;
    uint32_t unk=mask&~known;
    if (unk) { char tmp[32]; sprintf(tmp,"|UNK:0x%08X",unk); s+=tmp; }
    return s;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1]
                     : "f:\\OpenADS\\testdata\\pmsys\\pmsys.add";
    printf("Dumping Permission records from: %s\n\n", path);

    // Read entire file
    std::ifstream f(path, std::ios::binary);
    if (!f) { printf("Cannot open file\n"); return 1; }
    std::string buf((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());

    if (buf.size() < 40) { printf("File too small\n"); return 1; }

    uint32_t hdr_len = le32(buf.c_str(), 0x20);
    uint32_t rec_len = le32(buf.c_str(), 0x24);
    printf("hdr_len=%u rec_len=%u  total records=%u\n\n",
           hdr_len, rec_len, (unsigned)((buf.size()-hdr_len)/rec_len));

    if (rec_len == 0 || hdr_len >= buf.size()) { printf("Bad header\n"); return 1; }

    size_t total = (buf.size() - hdr_len) / rec_len;

    // First pass: build id→name and id→type maps
    std::unordered_map<uint32_t,std::string> id_name, id_type;
    for (size_t i = 0; i < total; ++i) {
        size_t base = hdr_len + i * rec_len;
        uint8_t status = (uint8_t)buf[base];
        if (status != 0x04) continue;   // active only
        uint32_t obj_id = le32(buf.c_str(), base + 5);
        std::string type = trim(buf.c_str(), base + 13, 10);
        std::string name = trim(buf.c_str(), base + 23, 200);
        if (!type.empty() && !name.empty()) {
            id_name[obj_id] = name;
            id_type[obj_id] = type;
        }
    }

    // Second pass: dump Permission records
    printf("%-6s %-12s %-30s %-20s  info2(raw)    bits\n",
           "Rec","Principal","Target","Object-type","");
    printf("%s\n", std::string(110,'-').c_str());

    int count = 0;
    for (size_t i = 0; i < total; ++i) {
        size_t base = hdr_len + i * rec_len;
        uint8_t status = (uint8_t)buf[base];
        bool active = (status == 0x04);

        // obj_type at base+13
        std::string obj_type = trim(buf.c_str(), base + 13, 10);
        if (obj_type != "Permission") continue;
        ++count;

        uint32_t obj_id    = le32(buf.c_str(), base + 5);
        uint32_t parent_id = le32(buf.c_str(), base + 9);
        uint32_t info1     = le32(buf.c_str(), base + 227); // after property VarChar (223+2+? = need to find)
        uint32_t info2     = 0;

        // In binary .add: after the 275-byte property VarChar, there's
        // obj_id(4) parent_id(4) obj_type(10) obj_name(200) property(275) info1(4) info2(4)...
        // Property VarChar is at offset 223 (2-byte length + 273 data bytes = 275 total)
        // info1 starts at base + 223 + 275 = base + 498? No - let me use the known rec_len
        // to figure out the layout.
        //
        // From the main decoder:
        //   r.obj_id    = le32(buf, base + 5)
        //   r.parent_id = le32(buf, base + 9)
        //   r.obj_type  = trim_char(buf, base + 13, 10)
        //   r.obj_name  = trim_char(buf, base + 23, 200)
        //   plen        = le16(buf, base + 223)
        //   property    = [base+225 .. base+225+273-1]
        //   info1       = ?
        //   info2       = ?
        //
        // Let's scan for info1/info2 by looking at where the data_dict reads them.
        // From data_dict.cpp:
        //   r.info1 = le32(buf, base + 498);  // base + 223 + 275
        //   r.info2 = le32(buf, base + 502);
        // Actually we need to check the exact offsets used in data_dict.cpp.
        // Let's just try base+498 and base+502 based on field size calculation:
        //   status(1) + unknown(4) + obj_id(4) + parent_id(4) + obj_type(10) + obj_name(200)
        //   = 223 bytes. Then property_len(2) + property(273) = 275 bytes.
        //   Total so far: 223 + 275 = 498.
        //   Then info1(4) + info2(4) = 8 bytes. So info1 at base+498, info2 at base+502.

        // But wait, status is at base+0, next fields...
        // Let's look at the actual rec_len to validate.
        // rec_len should be >= 506 at minimum.

        if (base + 506 <= buf.size()) {
            info1 = le32(buf.c_str(), base + 498);
            info2 = le32(buf.c_str(), base + 502);
        } else {
            // Try other offsets
            info1 = le32(buf.c_str(), base + rec_len - 12);
            info2 = le32(buf.c_str(), base + rec_len - 8);
        }

        std::string principal = id_name.count(parent_id) ? id_name[parent_id] : "?unk?";
        std::string target    = id_name.count(info1)     ? id_name[info1]     : "?unk?";
        std::string ttype     = id_type.count(info1)     ? id_type[info1]     : "?";

        printf("%-6zu %-12s %-30s %-20s  0x%08X  %s%s\n",
               i, principal.c_str(), target.c_str(), ttype.c_str(),
               info2, decode_perm(info2).c_str(),
               active ? "" : " [DELETED]");
    }
    printf("\nTotal Permission records: %d\n", count);

    // Summary: show unique bitmasks seen
    printf("\n--- Unique info2 bitmask values seen ---\n");
    std::unordered_map<uint32_t,int> seen;
    for (size_t i = 0; i < total; ++i) {
        size_t base = hdr_len + i * rec_len;
        std::string obj_type = trim(buf.c_str(), base + 13, 10);
        if (obj_type != "Permission") continue;
        uint32_t info2 = 0;
        if (base + 506 <= buf.size())
            info2 = le32(buf.c_str(), base + 502);
        seen[info2]++;
    }
    for (auto& kv : seen)
        printf("  0x%08X (%d times)  [%s]\n", kv.first, kv.second, decode_perm(kv.first).c_str());

    return 0;
}
