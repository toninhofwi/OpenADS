// sap_groups_probe.cpp
// Connect to a DD via SAP ACE and dump all user<->group memberships,
// then dump raw XOR token data from User records' property zones for comparison.
//
// Build (MSVC x64):
//   cl.exe sap_groups_probe.cpp /Dx64 /I f:\Ads11 /Fe:sap_groups_probe.exe /link f:\Ads11\ace64.lib
//       (windows.h included first; ace.h requires it)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ace.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <algorithm>
#include <fstream>

// LE readers for binary file
static uint32_t le32b(const uint8_t* b, size_t off) {
    return (uint32_t)b[off] | ((uint32_t)b[off+1]<<8) | ((uint32_t)b[off+2]<<16) | ((uint32_t)b[off+3]<<24);
}
static uint16_t le16b(const uint8_t* b, size_t off) {
    return (uint16_t)b[off] | ((uint16_t)b[off+1]<<8);
}
static std::string trim_field(const uint8_t* b, size_t off, size_t len) {
    std::string s((const char*)b + off, len);
    auto n = s.find('\0'); if (n != std::string::npos) s.resize(n);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// ---------------------------------------------------------------------------
// Part 1: SAP ACE query of system.usergroups
// ---------------------------------------------------------------------------
static void query_memberships(ADSHANDLE hConn,
                               std::map<std::string, std::set<std::string>>& out_user_grps)
{
    ADSHANDLE hCur = 0;
    const char* sql = "SELECT USER_NAME, GROUP_NAME FROM system.usergroups ORDER BY USER_NAME, GROUP_NAME";
    UNSIGNED32 rc = AdsExecuteSQLDirect(hConn, (UNSIGNED8*)sql, &hCur);
    if (rc != 0) { printf("SQL error %u\n", rc); return; }

    char ubuf[256], gbuf[256];
    UNSIGNED16 eof_flag = 0;
    UNSIGNED32 ulen, glen;
    while (AdsAtEOF(hCur, &eof_flag), eof_flag == 0) {
        ulen = sizeof(ubuf); glen = sizeof(gbuf);
        AdsGetString(hCur, (UNSIGNED8*)"USER_NAME",  (UNSIGNED8*)ubuf, &ulen, ADS_NONE);
        AdsGetString(hCur, (UNSIGNED8*)"GROUP_NAME", (UNSIGNED8*)gbuf, &glen, ADS_NONE);
        std::string u(ubuf, ulen), g(gbuf, glen);
        while (!u.empty() && u.back() == ' ') u.pop_back();
        while (!g.empty() && g.back() == ' ') g.pop_back();
        if (!u.empty() && !g.empty())
            out_user_grps[u].insert(g);
        if (AdsSkip(hCur, 1) != 0) break;
    }
    AdsCloseTable(hCur);
}

// ---------------------------------------------------------------------------
// Part 2: Binary decoder of XOR tokens from the .add file
// ---------------------------------------------------------------------------
struct GroupEntry {
    uint32_t oid;
    std::string name;
};
struct UserEntry {
    uint32_t oid;
    std::string name;
    std::vector<std::array<uint8_t,4>> tokens; // slot 0..N-1
};

static void decode_binary(const char* path,
    std::vector<GroupEntry>& groups,
    std::vector<UserEntry>& users)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { printf("Cannot open %s\n", path); return; }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (buf.size() < 40) { printf("File too small\n"); return; }
    const uint8_t* B = buf.data();
    uint32_t hdr_len = le32b(B, 0x20);
    uint32_t rec_len = le32b(B, 0x24);
    size_t total = (buf.size() - hdr_len) / rec_len;

    // First pass: collect objects
    std::map<uint32_t,std::string> id_name, id_type;
    for (size_t i = 0; i < total; ++i) {
        size_t base = hdr_len + i * rec_len;
        if (B[base] != 0x04) continue;
        uint32_t oid = le32b(B, base+5);
        std::string otype = trim_field(B, base+13, 10);
        std::string oname = trim_field(B, base+23, 200);
        if (!oname.empty() && !otype.empty()) { id_name[oid]=oname; id_type[oid]=otype; }
    }

    // Build group list
    for (auto& kv : id_name)
        if (id_type[kv.first] == "Group")
            groups.push_back({kv.first, kv.second});
    std::sort(groups.begin(), groups.end(), [](auto& a, auto& b){ return a.oid < b.oid; });

    // Collect User records + tokens
    for (size_t i = 0; i < total; ++i) {
        size_t base = hdr_len + i * rec_len;
        if (B[base] != 0x04) continue;
        std::string otype = trim_field(B, base+13, 10);
        if (otype != "User") continue;
        std::string uname = trim_field(B, base+23, 200);
        if (uname.empty()) continue;
        uint32_t oid = le32b(B, base+5);

        uint16_t plen = le16b(B, base+223);
        if (plen == 0xFFFFu) { users.push_back({oid, uname, {}}); continue; }

        size_t xoff = base + 225 + plen;
        size_t xend = base + 225 + 273;
        if (xoff + 2 > xend) { users.push_back({oid, uname, {}}); continue; }

        uint16_t cf = le16b(B, xoff);
        if (cf == 0xFFFFu || cf == 0) { users.push_back({oid, uname, {}}); continue; }

        uint32_t ntok = cf / 4u;
        UserEntry ue;
        ue.oid = oid; ue.name = uname;
        for (uint32_t s = 0; s < ntok && s < 20; ++s) {
            size_t toff = xoff + 2 + s * 4;
            if (toff + 4 > xend) break;
            std::array<uint8_t,4> tok = {B[toff], B[toff+1], B[toff+2], B[toff+3]};
            ue.tokens.push_back(tok);
        }
        users.push_back(std::move(ue));
    }
}

// Brute-force K[slot] = token[slot][0] XOR gid_LE, verified across all users.
static bool solve_slots(
    const std::vector<GroupEntry>& groups,
    const std::vector<UserEntry>& users,
    int max_slots,
    std::map<std::string, std::set<std::string>>& out)
{
    std::set<uint32_t> valid_gids;
    for (auto& g : groups) valid_gids.insert(g.oid);
    std::map<uint32_t,std::string> gid_name;
    for (auto& g : groups) gid_name[g.oid] = g.name;

    // Slot data: all users' tokens at each slot index
    std::vector<std::vector<std::pair<std::string,std::array<uint8_t,4>>>> slot_data(max_slots);
    for (auto& u : users) {
        for (int s = 0; s < (int)u.tokens.size() && s < max_slots; ++s)
            slot_data[s].push_back({u.name, u.tokens[s]});
    }

    std::vector<std::array<uint8_t,4>> slot_keys(max_slots);
    std::vector<bool> slot_known(max_slots, false);
    std::map<std::string, std::set<uint32_t>> user_prop_gids;

    for (int s = 0; s < max_slots; ++s) {
        auto& sd = slot_data[s];
        if (sd.empty()) break;
        auto& t0 = sd[0].second;

        int n_valid = 0;
        std::array<uint8_t,4> first_K{};

        for (auto& g : groups) {
            uint32_t gid = g.oid;
            std::array<uint8_t,4> K = {
                uint8_t(t0[0] ^ uint8_t(gid & 0xFF)),
                uint8_t(t0[1] ^ uint8_t((gid>>8) & 0xFF)),
                uint8_t(t0[2] ^ uint8_t((gid>>16) & 0xFF)),
                uint8_t(t0[3] ^ uint8_t((gid>>24) & 0xFF))};
            bool ok = true;
            for (auto& e : sd) {
                auto& t = e.second;
                uint32_t dec = uint32_t(t[0]^K[0]) | (uint32_t(t[1]^K[1])<<8)
                             | (uint32_t(t[2]^K[2])<<16) | (uint32_t(t[3]^K[3])<<24);
                if (!valid_gids.count(dec)) { ok = false; break; }
                auto ui = user_prop_gids.find(e.first);
                if (ui != user_prop_gids.end() && ui->second.count(dec)) { ok = false; break; }
            }
            if (ok) {
                if (n_valid == 0) first_K = K;
                ++n_valid;
                if (sd.size() >= 2) break;
                if (n_valid >= 2) break;
            }
        }
        if (n_valid == 0) continue;
        if (n_valid >= 2 && sd.size() < 2) { printf("  [slot %d ambiguous, single user]\n", s); continue; }

        slot_keys[s] = first_K;
        slot_known[s] = true;

        for (auto& e : sd) {
            auto& t = e.second;
            uint32_t gid = uint32_t(t[0]^first_K[0]) | (uint32_t(t[1]^first_K[1])<<8)
                         | (uint32_t(t[2]^first_K[2])<<16) | (uint32_t(t[3]^first_K[3])<<24);
            user_prop_gids[e.first].insert(gid);
        }

        // Print discovered key
        uint32_t K32 = uint32_t(first_K[0]) | (uint32_t(first_K[1])<<8)
                     | (uint32_t(first_K[2])<<16) | (uint32_t(first_K[3])<<24);
        uint32_t grp0 = uint32_t(t0[0]^first_K[0]) | (uint32_t(t0[1]^first_K[1])<<8)
                      | (uint32_t(t0[2]^first_K[2])<<16) | (uint32_t(t0[3]^first_K[3])<<24);
        printf("  K[%d] = 0x%02X%02X%02X%02X  (first token → %s)\n",
               s, first_K[0], first_K[1], first_K[2], first_K[3],
               gid_name.count(grp0) ? gid_name[grp0].c_str() : "?");
    }

    // Populate output
    for (int s = 0; s < max_slots; ++s) {
        if (!slot_known[s]) continue;
        const std::array<uint8_t,4>& Ks = slot_keys[s];
        for (auto& e2 : slot_data[s]) {
            const std::array<uint8_t,4>& tk = e2.second;
            uint32_t b0 = uint32_t(tk[0]) ^ uint32_t(Ks[0]);
            uint32_t b1 = uint32_t(tk[1]) ^ uint32_t(Ks[1]);
            uint32_t b2 = uint32_t(tk[2]) ^ uint32_t(Ks[2]);
            uint32_t b3 = uint32_t(tk[3]) ^ uint32_t(Ks[3]);
            uint32_t gid = b0 | (b1<<8) | (b2<<16) | (b3<<24);
            if (gid_name.count(gid))
                out[e2.first].insert(gid_name[gid]);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    const char* add_path = (argc > 1) ? argv[1] : "E:\\AdsData\\sfi-2021\\mp.add";
    const char* user = (argc > 2) ? argv[2] : "adssys";
    const char* pass = (argc > 3) ? argv[3] : "mp";

    printf("=== SAP group membership probe + binary XOR decoder ===\n");
    printf("DD: %s\n\n", add_path);

    // --- SAP query ---
    printf("--- Connecting via SAP ACE ---\n");
    ADSHANDLE hConn = 0;
    UNSIGNED32 rc = AdsConnect60((UNSIGNED8*)add_path, ADS_LOCAL_SERVER,
                                  (UNSIGNED8*)user, (UNSIGNED8*)pass, 0, &hConn);
    std::map<std::string, std::set<std::string>> sap_mbr;
    if (rc != 0) {
        printf("AdsConnect60 failed (%u) — skipping SAP query\n\n", rc);
    } else {
        printf("Connected. Querying system.usergroups...\n");
        query_memberships(hConn, sap_mbr);
        AdsDisconnect(hConn);
        printf("SAP reports %zu user-group pairs.\n\n", (size_t)[&](){
            size_t t=0; for(auto&kv:sap_mbr) t+=kv.second.size(); return t; }());
    }

    // --- Binary XOR decode ---
    printf("--- Binary XOR decoder ---\n");
    std::vector<GroupEntry> groups;
    std::vector<UserEntry> users;
    decode_binary(add_path, groups, users);
    printf("Found %zu groups, %zu users in binary file.\n", groups.size(), users.size());
    printf("Groups: ");
    for (auto& g : groups) printf("%s(0x%X) ", g.name.c_str(), g.oid);
    printf("\n\nSolving slot keys (max 15 slots):\n");

    std::map<std::string, std::set<std::string>> bin_mbr;
    solve_slots(groups, users, 15, bin_mbr);

    // DB:Public — add for all users (built-in)
    for (auto& u : users) bin_mbr[u.name].insert("DB:Public");

    printf("\n--- Comparison (SAP vs Binary XOR) ---\n");
    std::set<std::string> all_users;
    for (auto& kv : sap_mbr) all_users.insert(kv.first);
    for (auto& kv : bin_mbr) all_users.insert(kv.first);

    int mismatch = 0;
    for (auto& uname : all_users) {
        auto& sg = sap_mbr[uname];
        auto& bg = bin_mbr[uname];
        // Remove DB:Admin/Backup/Debug from SAP set (we can't decode those)
        std::set<std::string> sg_filtered;
        for (auto& g : sg)
            if (g != "DB:Admin" && g != "DB:Backup" && g != "DB:Debug")
                sg_filtered.insert(g);
        if (sg_filtered == bg) {
            printf("  %-20s OK  (%zu groups)\n", uname.c_str(), bg.size());
        } else {
            printf("  %-20s MISMATCH\n", uname.c_str());
            // Show SAP only
            for (auto& g : sg_filtered) if (!bg.count(g))
                printf("    SAP only:  %s\n", g.c_str());
            // Show binary only
            for (auto& g : bg) if (!sg_filtered.count(g))
                printf("    BIN only:  %s\n", g.c_str());
            ++mismatch;
        }
    }
    printf("\nTotal mismatches: %d\n", mismatch);
    return mismatch > 0 ? 1 : 0;
}
