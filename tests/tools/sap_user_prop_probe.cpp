// sap_user_prop_probe.cpp
// Calls AdsDDGetUserProperty(1102=GROUP_MEMBERSHIP) for every user in a DD
// to see the format of the group membership string returned by SAP.
// Also tests AdsDDGetUserGroupProperty for group listing.
//
// Build (MSVC x64):
//   cl.exe sap_user_prop_probe.cpp /Dx64 /I f:\Ads11 /Fe:sap_user_prop_probe.exe /link f:\Ads11\ace64.lib

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ace.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Hex-dump a buffer
static void hexdump(const unsigned char* buf, int len, int per_row = 32) {
    for (int i = 0; i < len; i += per_row) {
        printf("    %04X: ", i);
        for (int j = 0; j < per_row && i+j < len; ++j)
            printf("%02X ", buf[i+j]);
        printf(" |");
        for (int j = 0; j < per_row && i+j < len; ++j) {
            unsigned char c = buf[i+j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("|\n");
    }
}

static std::string safe_str(const unsigned char* buf, int len) {
    std::string s;
    for (int i = 0; i < len; ++i) {
        unsigned char c = buf[i];
        if (c == 0) s += "\\0"; else if (c == ';') s += ";"; else s += (char)c;
    }
    return s;
}

int main(int argc, char** argv) {
    const char* add_path = (argc > 1) ? argv[1] : "f:\\OpenADS\\testdata\\pmsys\\pmsys.add";
    const char* user_arg = (argc > 2) ? argv[2] : "adssys";
    const char* pass_arg = (argc > 3) ? argv[3] : "pmsys";

    printf("AdsDDGetUserProperty(1102) probe\n");
    printf("DD: %s\n\n", add_path);

    ADSHANDLE hConn = 0;
    UNSIGNED32 rc = AdsConnect60((UNSIGNED8*)add_path, ADS_LOCAL_SERVER,
                                  (UNSIGNED8*)user_arg, (UNSIGNED8*)pass_arg,
                                  0, &hConn);
    if (rc != 0) {
        printf("AdsConnect60 failed: %u\n", rc);
        return 1;
    }
    printf("Connected (handle=%llu)\n\n", (unsigned long long)hConn);

    // --- List all users first ---
    ADSHANDLE hCur = 0;
    rc = AdsExecuteSQLDirect(hConn, (UNSIGNED8*)"SELECT NAME FROM system.iota(1) WHERE 1=0", &hCur);
    // Use system.usernames or just query system table
    if (hCur) AdsCloseTable(hCur); hCur=0;

    rc = AdsExecuteSQLDirect(hConn,
        (UNSIGNED8*)"SELECT NAME FROM system.objects WHERE OBJECT_TYPE=8 ORDER BY NAME", &hCur);
    if (rc != 0) {
        printf("Cannot list users (rc=%u), trying hardcoded list\n", rc);
    }

    // Collect user names
    std::vector<std::string> users_list;

    if (hCur) {
        UNSIGNED16 eof = 0;
        while (AdsAtEOF(hCur, &eof), eof == 0) {
            unsigned char nbuf[256] = {}; UNSIGNED32 nlen = sizeof(nbuf);
            if (AdsGetString(hCur, (UNSIGNED8*)"NAME", nbuf, &nlen, ADS_NONE) == 0) {
                std::string u((char*)nbuf, nlen);
                while (!u.empty() && u.back() == ' ') u.pop_back();
                if (!u.empty()) users_list.push_back(u);
            }
            if (AdsSkip(hCur, 1) != 0) break;
        }
        AdsCloseTable(hCur);
    } else {
        // Hardcoded list from pmsys
        const char* default_users[] = {"adssys","RCB","user","root","AutoTasks", nullptr};
        for (int i = 0; default_users[i]; ++i) users_list.push_back(default_users[i]);
    }

    printf("=== AdsDDGetUserProperty(USER_GROUP_MEMBERSHIP=1102) for each user ===\n\n");
    for (auto& uname : users_list) {
        unsigned char buf[2048] = {};
        UNSIGNED16 len = sizeof(buf);
        rc = AdsDDGetUserProperty(hConn, (UNSIGNED8*)uname.c_str(),
                                   ADS_DD_USER_GROUP_MEMBERSHIP, buf, &len);
        if (rc != 0) {
            printf("User %-20s rc=%u (no membership?)\n", uname.c_str(), rc);
            continue;
        }
        printf("User %-20s  len=%u\n", uname.c_str(), len);
        if (len > 0) {
            printf("  String: [%s]\n", safe_str(buf, len).c_str());
            printf("  Hex:\n");
            hexdump(buf, len);
        }
        printf("\n");
    }

    // --- Also test with different group membership property IDs ---
    printf("=== Property 1102 raw on specific users ===\n");
    for (auto& uname : users_list) {
        unsigned char buf[2048] = {};
        UNSIGNED16 len = sizeof(buf);
        rc = AdsDDGetUserProperty(hConn, (UNSIGNED8*)uname.c_str(),
                                   ADS_DD_USER_GROUP_MEMBERSHIP, buf, &len);
        if (rc == 0 && len > 0) {
            // Count semicolons to see how many groups
            int nsep = 0; for (int i = 0; i < (int)len; i++) if (buf[i] == ';') ++nsep;
            printf("  %-20s : %d group(s) → [%s]\n",
                   uname.c_str(), nsep+1, safe_str(buf, len).c_str());
        }
    }

    AdsDisconnect(hConn);
    printf("\nDone.\n");
    return 0;
}
