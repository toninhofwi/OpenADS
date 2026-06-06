// sap_full_membership.cpp
// Reads user list from a text file, queries AdsDDGetUserProperty(1102) for
// each user via SAP, and prints the complete group membership including DB: groups.
//
// Build (MSVC x64):
//   cl.exe sap_full_membership.cpp /Dx64 /EHsc /I f:\Ads11 /Fe:sap_full_membership.exe /link f:\Ads11\ace64.lib

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ace.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <set>
#include <map>
#include <algorithm>

static void trim_r(std::string& s) {
    while (!s.empty() && (s.back()=='\r'||s.back()=='\n'||s.back()==' ')) s.pop_back();
}

// Parse "A;B;C\0" → vector of group names
static std::vector<std::string> parse_groups(const char* buf, int len) {
    std::vector<std::string> v;
    std::string cur;
    for (int i = 0; i < len; ++i) {
        char c = buf[i];
        if (c == '\0' || c == ';') {
            if (!cur.empty()) { v.push_back(cur); cur.clear(); }
        } else cur += c;
    }
    if (!cur.empty()) v.push_back(cur);
    return v;
}

int main(int argc, char** argv) {
    const char* add_path  = (argc > 1) ? argv[1] : "E:\\AdsData\\sfi-2021\\mp.add";
    const char* adssys_pw = (argc > 2) ? argv[2] : "mp10";
    const char* user_file = (argc > 3) ? argv[3] : "";

    printf("SAP full membership dump via AdsDDGetUserProperty(1102)\n");
    printf("DD: %s\n\n", add_path);

    ADSHANDLE hConn = 0;
    UNSIGNED32 rc = AdsConnect60((UNSIGNED8*)add_path, ADS_LOCAL_SERVER,
                                  (UNSIGNED8*)"adssys", (UNSIGNED8*)adssys_pw,
                                  0, &hConn);
    if (rc != 0) { printf("Connect failed: %u\n", rc); return 1; }
    printf("Connected.\n\n");

    // Read user list
    std::vector<std::string> users;
    if (user_file[0]) {
        std::ifstream f(user_file);
        std::string line;
        while (std::getline(f, line)) { trim_r(line); if (!line.empty()) users.push_back(line); }
    }
    if (users.empty()) {
        // fallback
        for (auto& n : {"x12","billing","YCL","CRB","TECHS","CPA","MVM","GB","JotForm","BG",
                         "PteConfirmations","autoreg_user","KR","LM","Backup","YCV","DR","FCT",
                         "FD","JG","MV","RCB","SD","GUEST","BS","AutoTasks","CD","MA","DA","VR",
                         "CDJ","YC","LS","VB","RS","CP","SS","NR","CN","MDV","FHL","ED","AM"})
            users.push_back(n);
    }

    // Collect all group names seen (for column header)
    std::map<std::string, std::set<std::string>> all_memberships;
    std::set<std::string> all_groups;

    printf("%-20s  Groups\n", "User");
    printf("%s\n", std::string(100, '-').c_str());

    for (auto& uname : users) {
        unsigned char buf[4096] = {}; UNSIGNED16 len = sizeof(buf);
        rc = AdsDDGetUserProperty(hConn, (UNSIGNED8*)uname.c_str(),
                                   ADS_DD_USER_GROUP_MEMBERSHIP, buf, &len);
        if (rc != 0) {
            printf("%-20s  [no membership or not found rc=%u]\n", uname.c_str(), rc);
            continue;
        }
        auto grps = parse_groups((char*)buf, (int)len);
        std::sort(grps.begin(), grps.end());
        printf("%-20s  ", uname.c_str());
        for (size_t i = 0; i < grps.size(); ++i) {
            if (i) printf(";");
            printf("%s", grps[i].c_str());
            all_groups.insert(grps[i]);
        }
        printf("\n");
        all_memberships[uname] = std::set<std::string>(grps.begin(), grps.end());
    }

    // Summary: which users are in each DB: group
    printf("\n=== DB: built-in group membership (not in binary XOR tokens) ===\n");
    for (const char* dbg : {"DB:Admin","DB:Backup","DB:Debug","DB:Public"}) {
        std::vector<std::string> members;
        for (auto& kv : all_memberships)
            if (kv.second.count(dbg)) members.push_back(kv.first);
        std::sort(members.begin(), members.end());
        printf("%-12s (%zu users): ", dbg, members.size());
        for (size_t i = 0; i < members.size(); ++i) { if(i) printf(", "); printf("%s",members[i].c_str()); }
        printf("\n");
    }

    // Summary: groups NOT covered by binary XOR decoder (db: groups)
    printf("\n=== All distinct groups seen ===\n");
    for (auto& g : all_groups) printf("  %s\n", g.c_str());

    AdsDisconnect(hConn);
    return 0;
}
