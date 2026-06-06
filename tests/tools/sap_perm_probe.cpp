// sap_perm_probe.cpp
// Connects to pmsys.add via SAP ACE64 and dumps raw AdsDDGetPermissions()
// bitmasks for known grantees, then correlates with ADS_PERMISSION constants.
//
// Build (from this directory, MSVC x64):
//   cl.exe sap_perm_probe.cpp /Dx64 /I f:\Ads11 /Fe:sap_perm_probe.exe /link f:\Ads11\ace64.lib
//   (ace.h requires windows.h to be included first, and x64 defined for ADSHANDLE typedef)
//
// Run:
//   copy f:\Ads11\ace64.dll . && sap_perm_probe.exe

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ace.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Permission bit names (ADS_PERMISSION_* from ace.h)
// ---------------------------------------------------------------------------
struct BitDesc { uint32_t bit; const char* name; };
static const BitDesc kBits[] = {
    { ADS_PERMISSION_READ,         "READ/SELECT"  },
    { ADS_PERMISSION_UPDATE,       "UPDATE"       },
    { ADS_PERMISSION_EXECUTE,      "EXECUTE"      },
    { ADS_PERMISSION_INHERIT,      "INHERIT"      },
    { ADS_PERMISSION_INSERT,       "INSERT"       },
    { ADS_PERMISSION_DELETE,       "DELETE"       },
    { ADS_PERMISSION_LINK_ACCESS,  "LINK_ACCESS"  },
    { ADS_PERMISSION_CREATE,       "CREATE"       },
    { ADS_PERMISSION_ALTER,        "ALTER"        },
    { ADS_PERMISSION_DROP,         "DROP"         },
    { ADS_PERMISSION_WITH_GRANT,   "WITH_GRANT"   },
};
static const int kNumBits = (int)(sizeof(kBits)/sizeof(kBits[0]));

// Decode a bitmask to a readable string
static std::string decode(uint32_t mask) {
    if (mask == 0) return "(none)";
    std::string s;
    for (int i = 0; i < kNumBits; ++i) {
        if (mask & kBits[i].bit) {
            if (!s.empty()) s += "|";
            s += kBits[i].name;
        }
    }
    // Show unknown bits
    uint32_t known = 0;
    for (int i = 0; i < kNumBits; ++i) known |= kBits[i].bit;
    uint32_t unk = mask & ~known;
    if (unk) {
        char buf[32]; sprintf(buf, "|UNK:0x%08X", unk);
        s += buf;
    }
    return s;
}

// Decode to system.permissions column values: 0=none,1=granted,2=with_grant
static void print_columns(uint32_t mask) {
    bool wg = (mask & ADS_PERMISSION_WITH_GRANT) != 0;
    printf("  Columns (0=none,1=granted,2=with_grant):\n");
    const struct { uint32_t bit; const char* col; } cols[] = {
        { ADS_PERMISSION_READ,        "Select  " },
        { ADS_PERMISSION_UPDATE,      "Update  " },
        { ADS_PERMISSION_INSERT,      "Insert  " },
        { ADS_PERMISSION_DELETE,      "Delete  " },
        { ADS_PERMISSION_EXECUTE,     "Execute " },
        { ADS_PERMISSION_LINK_ACCESS, "Access  " },
        { ADS_PERMISSION_INHERIT,     "Inherit " },
        { ADS_PERMISSION_CREATE,      "Create  " },
        { ADS_PERMISSION_ALTER,       "Alter   " },
        { ADS_PERMISSION_DROP,        "Drop    " },
    };
    for (auto& c : cols) {
        bool set = (mask & c.bit) != 0;
        int val;
        if (!set) val = 0;
        else if (c.bit == ADS_PERMISSION_INHERIT) val = 1; // INHERIT never shows as 2
        else if (wg) val = 2;
        else val = 1;
        printf("    %-10s = %d\n", c.col, val);
    }
}

// ---------------------------------------------------------------------------
// Query one grantee + object
// ---------------------------------------------------------------------------
static void query(ADSHANDLE hConn,
                  const char* grantee,
                  UNSIGNED16 obj_type,
                  const char* obj_name,
                  const char* parent_name,
                  UNSIGNED16 get_inherited)
{
    UNSIGNED32 perms = 0;
    UNSIGNED32 rc = AdsDDGetPermissions(
        hConn,
        (UNSIGNED8*)grantee,
        obj_type,
        (UNSIGNED8*)obj_name,
        parent_name ? (UNSIGNED8*)parent_name : nullptr,
        get_inherited,
        &perms);

    if (rc != 0) {
        printf("  [ERROR %u] AdsDDGetPermissions('%s' on '%s') failed\n",
               rc, grantee, obj_name);
        return;
    }

    printf("  Grantee=%-12s  Object=%-30s  Mask=0x%08X  [%s]\n",
           grantee, obj_name, perms, decode(perms).c_str());
    if (perms != 0)
        print_columns(perms);
}

// ---------------------------------------------------------------------------
// Tables of interest from the pmsys test cases
// ---------------------------------------------------------------------------
static const char* kTables[] = {
    "sys_registry",
    "landlords",
    "properties",
    "leases",
    "propertyphotos",
    "taglist",
    "tags",
    "propertytransactions",
    "autoreports",
    "queries",
    "queryparameters",
    "systemerrors",
    "xbrowsestates",
    "fastreports",
    "auditlog",
    "system",
    "managers",
    "servorders",
    "catcodes",
    "escrows",
    "sequences",
    "ownerdraws",
    "s8leases",
    "attachments",
    "memotrans",
    "longtermimprovements",
    "reminders",
    "contractors",
    "latefeenotices",
    "otherunits",
    "invoices",
    "user_column_preferences",
    nullptr
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    const char* add_path = (argc > 1)
        ? argv[1]
        : "f:\\OpenADS\\testdata\\pmsys\\pmsys.add";
    const char* user = "adssys";
    const char* pass = "pmsys";

    printf("SAP AdsDDGetPermissions probe\n");
    printf("DD: %s\n\n", add_path);

    // Connect
    ADSHANDLE hConn = 0;
    UNSIGNED32 rc = AdsConnect60(
        (UNSIGNED8*)add_path,
        ADS_LOCAL_SERVER,
        (UNSIGNED8*)user,
        (UNSIGNED8*)pass,
        0,
        &hConn);
    if (rc != 0) {
        printf("AdsConnect60 failed: %u\n", rc);
        return 1;
    }
    printf("Connected as %s (handle=%llu)\n\n", user, (unsigned long long)hConn);

    // -----------------------------------------------------------------------
    // 1. Group "General" — direct permissions (usGetInherited=0)
    // -----------------------------------------------------------------------
    printf("=== Group 'General' on tables (usGetInherited=0) ===\n");
    for (int i = 0; kTables[i]; ++i) {
        query(hConn, "General", ADS_DD_TABLE_OBJECT, kTables[i], nullptr, 0);
    }

    // -----------------------------------------------------------------------
    // 2. User "RCB" — direct permissions (usGetInherited=0)
    // -----------------------------------------------------------------------
    printf("\n=== User 'RCB' on tables (usGetInherited=0) ===\n");
    for (int i = 0; kTables[i]; ++i) {
        query(hConn, "RCB", ADS_DD_TABLE_OBJECT, kTables[i], nullptr, 0);
    }

    // -----------------------------------------------------------------------
    // 3. User "RCB" — effective (usGetInherited=1)
    // -----------------------------------------------------------------------
    printf("\n=== User 'RCB' on tables (usGetInherited=1, effective) ===\n");
    for (int i = 0; kTables[i]; ++i) {
        query(hConn, "RCB", ADS_DD_TABLE_OBJECT, kTables[i], nullptr, 1);
    }

    // -----------------------------------------------------------------------
    // 4. Group "General" — field-level on landlords (sample)
    // -----------------------------------------------------------------------
    printf("\n=== Group 'General' field permissions on 'landlords' ===\n");
    const char* fields[] = {
        "LandLordID","FName","LName","Tel","address","city","State",
        "ZipCode","email","Notes","inactive","ManagerID", nullptr
    };
    for (int i = 0; fields[i]; ++i) {
        query(hConn, "General", ADS_DD_FIELD_OBJECT,
              fields[i], "landlords", 0);
    }

    // -----------------------------------------------------------------------
    // 5. Special: what bitmask does WITH_GRANT look like?
    //    Test on a table where RCB shows all-2 (propertytransactions)
    // -----------------------------------------------------------------------
    printf("\n=== RCB / propertytransactions bitmask detail ===\n");
    query(hConn, "RCB", ADS_DD_TABLE_OBJECT, "propertytransactions", nullptr, 0);

    // Constant check
    printf("\n=== ADS_PERMISSION constant values ===\n");
    printf("  READ        = 0x%08X\n", ADS_PERMISSION_READ);
    printf("  UPDATE      = 0x%08X\n", ADS_PERMISSION_UPDATE);
    printf("  EXECUTE     = 0x%08X\n", ADS_PERMISSION_EXECUTE);
    printf("  INHERIT     = 0x%08X\n", ADS_PERMISSION_INHERIT);
    printf("  INSERT      = 0x%08X\n", ADS_PERMISSION_INSERT);
    printf("  DELETE      = 0x%08X\n", ADS_PERMISSION_DELETE);
    printf("  LINK_ACCESS = 0x%08X\n", ADS_PERMISSION_LINK_ACCESS);
    printf("  CREATE      = 0x%08X\n", ADS_PERMISSION_CREATE);
    printf("  ALTER       = 0x%08X\n", ADS_PERMISSION_ALTER);
    printf("  DROP        = 0x%08X\n", ADS_PERMISSION_DROP);
    printf("  WITH_GRANT  = 0x%08X\n", ADS_PERMISSION_WITH_GRANT);
    printf("  ALL         = 0x%08X\n", ADS_PERMISSION_ALL);
    printf("  ALL_WITH_GRANT = 0x%08X\n", ADS_PERMISSION_ALL_WITH_GRANT);

    AdsDisconnect(hConn);
    printf("\nDone.\n");
    return 0;
}
