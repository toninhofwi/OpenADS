// openads_import_dd — one-shot SAP ADS → OpenADS data dictionary importer.
//
// Reads DB:-group memberships and fine-grained object permissions from an
// existing SAP ADS .add file using the SAP ACE runtime, then writes them
// into a copy of that file using the OpenADS DataDict API.  After the
// import the copy is a fully self-contained OpenADS DD that no longer
// requires the SAP DLL for permission enforcement.
//
// Usage:
//   openads_import_dd --source <sap.add> --dest <copy.add>
//                     --user <name> --password <pw>
//                     [--sap-lib <path/to/ace64.dll>]
//                     [--no-copy]   (dest already exists, skip the copy step)
//
// Output: JSON to stdout.
//   { "ok": true,  "memberships": N, "permissions": N, "warnings": [...] }
//   { "ok": false, "error": "...",                      "warnings": [...] }
//
// Called from DA-Web via exec(); the PHP side parses the JSON result.

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#include "engine/data_dict.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Cross-platform dynamic library loader
// ---------------------------------------------------------------------------

#ifdef _WIN32
using lib_handle = HMODULE;
static lib_handle lib_open(const char* path) {
    // Set the DLL search directory to the directory containing ace64.dll so that
    // its dependencies (adsloc64.dll, aicu64.dll, axcws64.dll) are loaded from
    // the same version set.  Without this, Windows finds them via PATH — potentially
    // a mismatched version in "Common Files\Advantage" — and AdsConnect60 fails
    // with error 7078.
    std::string p = path;
    auto sep = p.find_last_of("\\/");
    if (sep != std::string::npos) {
        std::string dir = p.substr(0, sep);
        SetDllDirectoryA(dir.c_str());
    }
    HMODULE h = LoadLibraryA(path);
    SetDllDirectoryA(nullptr);  // restore default search order
    return h;
}
static void*      lib_sym(lib_handle h, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(h, name));
}
static void       lib_close(lib_handle h)       { FreeLibrary(h); }
static std::string lib_error() {
    char buf[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(),
                   0, buf, sizeof(buf) - 1, nullptr);
    auto n = strlen(buf);
    while (n && (buf[n-1]=='\r'||buf[n-1]=='\n')) buf[--n]='\0';
    return buf;
}
#else
using lib_handle = void*;
static lib_handle lib_open(const char* path)    { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void*      lib_sym(lib_handle h, const char* name) { return dlsym(h, name); }
static void       lib_close(lib_handle h)       { dlclose(h); }
static std::string lib_error()                  { const char* e = dlerror(); return e ? e : "unknown"; }
#endif

// ---------------------------------------------------------------------------
// SAP ACE function pointer typedefs
// ---------------------------------------------------------------------------

using UNSIGNED8  = unsigned char;
using UNSIGNED16 = unsigned short;
using UNSIGNED32 = unsigned int;
using SIGNED32   = int;
using ADSHANDLE  = unsigned long long;

// SAP ACE uses __stdcall on Windows x86/x64; on Linux/macOS all functions
// follow the platform ABI and __stdcall is not applicable.
#ifdef _WIN32
#  define ADS_CALL __stdcall
#else
#  define ADS_CALL
#endif

using PFN_Connect    = UNSIGNED32 (ADS_CALL*)(UNSIGNED8* path, UNSIGNED16 srv,
                           UNSIGNED8* user, UNSIGNED8* pw,
                           UNSIGNED32 opts, ADSHANDLE* ph);
using PFN_Disconnect = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h);
using PFN_CreateStmt = UNSIGNED32 (ADS_CALL*)(ADSHANDLE hConn, ADSHANDLE* phStmt);
using PFN_ExecSQL    = UNSIGNED32 (ADS_CALL*)(ADSHANDLE hStmt, UNSIGNED8* sql, ADSHANDLE* phc);
using PFN_Close      = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h);
using PFN_AtEOF      = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h, UNSIGNED16* pb);
using PFN_Skip       = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h, SIGNED32 n);
using PFN_GetField   = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h, UNSIGNED8* name,
                           UNSIGNED8* buf, UNSIGNED32* len, UNSIGNED16 raw);
using PFN_NumFields  = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h, UNSIGNED16* n);
using PFN_FieldName  = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h, UNSIGNED16 idx,
                           UNSIGNED8* buf, UNSIGNED16* len);
using PFN_GetPerms   = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h, UNSIGNED8* grantee,
                           UNSIGNED16 obj_type, UNSIGNED8* obj_name,
                           UNSIGNED8* parent, UNSIGNED16 inherited,
                           UNSIGNED32* out);
using PFN_FindFirst  = UNSIGNED32 (ADS_CALL*)(ADSHANDLE hObj, UNSIGNED16 findType,
                           UNSIGNED8* parent, UNSIGNED8* nameBuf,
                           UNSIGNED16* nameLen, ADSHANDLE* phFind);
using PFN_FindNext   = UNSIGNED32 (ADS_CALL*)(ADSHANDLE hObj, ADSHANDLE hFind,
                           UNSIGNED8* nameBuf, UNSIGNED16* nameLen);
using PFN_FindClose  = UNSIGNED32 (ADS_CALL*)(ADSHANDLE hObj, ADSHANDLE hFind);

struct SapFuncs {
    PFN_Connect    connect    = nullptr;
    PFN_Disconnect disconnect = nullptr;
    PFN_CreateStmt createStmt = nullptr;
    PFN_ExecSQL    execSQL    = nullptr;
    PFN_Close      close      = nullptr;
    PFN_AtEOF      atEOF      = nullptr;
    PFN_Skip       skip       = nullptr;
    PFN_GetField   getField   = nullptr;
    PFN_NumFields  numFields  = nullptr;
    PFN_FieldName  fieldName  = nullptr;
    PFN_GetPerms   getPerms   = nullptr;
    PFN_FindFirst  findFirst  = nullptr;
    PFN_FindNext   findNext   = nullptr;
    PFN_FindClose  findClose  = nullptr;
};

// ---------------------------------------------------------------------------
// JSON helpers (no external dependency)
// ---------------------------------------------------------------------------

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (unsigned char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c < 0x20)  { char buf[8]; std::snprintf(buf,8,"\\u%04X",c); out+=buf; }
        else                out += c;
    }
    return out;
}

static void emit_result(bool ok,
                        int memberships, int permissions,
                        int function_bodies,
                        const std::string& error,
                        const std::vector<std::string>& warnings) {
    std::printf("{\"ok\":%s", ok ? "true" : "false");
    if (ok) {
        std::printf(",\"memberships\":%d,\"permissions\":%d,\"function_bodies\":%d",
                    memberships, permissions, function_bodies);
    } else {
        std::printf(",\"error\":\"%s\"", json_escape(error).c_str());
    }
    std::printf(",\"warnings\":[");
    for (std::size_t i = 0; i < warnings.size(); ++i) {
        if (i) std::printf(",");
        std::printf("\"%s\"", json_escape(warnings[i]).c_str());
    }
    std::printf("]}\n");
}

// ---------------------------------------------------------------------------
// SAP query helpers
// ---------------------------------------------------------------------------

static std::string sap_field(const SapFuncs& f, ADSHANDLE hc, const char* name) {
    char buf[512] = {};
    UNSIGNED32 len = sizeof(buf) - 1;
    f.getField(hc, (UNSIGNED8*)name, (UNSIGNED8*)buf, &len, 0);
    // Trim trailing spaces (SAP CHAR fields are space-padded).
    auto n = strlen(buf);
    while (n && buf[n-1] == ' ') buf[--n] = '\0';
    return buf;
}

// Variant with a larger buffer for potentially long fields (e.g. function bodies).
static std::string sap_field_large(const SapFuncs& f, ADSHANDLE hc,
                                    const char* name, std::size_t maxLen = 8192) {
    std::vector<char> buf(maxLen + 1, '\0');
    UNSIGNED32 len = static_cast<UNSIGNED32>(maxLen);
    f.getField(hc, (UNSIGNED8*)name, (UNSIGNED8*)buf.data(), &len, 0);
    // AdsGetField sets len to the actual byte count filled.
    while (len && buf[len - 1] == ' ') --len;
    return std::string(buf.data(), len);
}


// ---------------------------------------------------------------------------
// Default SAP library search paths
// ---------------------------------------------------------------------------

static std::vector<std::string> default_sap_paths() {
#ifdef _WIN32
    return {
        "f:\\ads11\\ace64.dll",
        "C:\\Program Files (x86)\\Advantage 11.10\\ace64.dll",
        "C:\\Program Files (x86)\\Advantage 10.10\\ace64.dll",
        "ace64.dll",
    };
#elif defined(__APPLE__)
    return { "/usr/local/lib/libace64.dylib", "libace64.dylib" };
#else
    return { "/usr/lib/libace64.so", "/opt/ads/lib/libace64.so", "libace64.so" };
#endif
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    std::string source, dest, user, password, sap_lib_path;
    bool no_copy = false;
    std::vector<std::string> warnings;

    for (int i = 1; i < argc; ++i) {
        auto arg = std::string(argv[i]);
        auto next = [&]() -> std::string {
            return (i + 1 < argc) ? std::string(argv[++i]) : std::string{};
        };
        if      (arg == "--source")   source       = next();
        else if (arg == "--dest")     dest         = next();
        else if (arg == "--user")     user         = next();
        else if (arg == "--password") password     = next();
        else if (arg == "--sap-lib")  sap_lib_path = next();
        else if (arg == "--no-copy")  no_copy      = true;
    }

    if (source.empty() || dest.empty() || user.empty()) {
        emit_result(false, 0, 0, 0,
            "Usage: openads_import_dd --source <sap.add> --dest <copy.add> "
            "--user <name> --password <pw> [--sap-lib <path>] [--no-copy]",
            warnings);
        return 1;
    }

    // ── Step 1: copy source → dest (and companion .am memo file) ───────────────
    if (!no_copy) {
        std::error_code ec;
        fs::copy_file(source, dest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            emit_result(false, 0, 0, 0,
                "Cannot copy " + source + " → " + dest + ": " + ec.message(),
                warnings);
            return 1;
        }
        // Also copy the companion .am memo file (holds SP body continuations).
        auto srcAm = fs::path(source).replace_extension(".am");
        auto dstAm = fs::path(dest).replace_extension(".am");
        if (fs::exists(srcAm)) {
            std::error_code ec2;
            fs::copy_file(srcAm, dstAm, fs::copy_options::overwrite_existing, ec2);
            if (ec2)
                warnings.push_back("Cannot copy .am memo file: " + ec2.message());
        }
    }

    // ── Step 2: load SAP DLL ─────────────────────────────────────────────────
    // If the user gave a directory instead of a .dll path, append the DLL name.
    if (!sap_lib_path.empty() && fs::is_directory(sap_lib_path)) {
        sap_lib_path = (fs::path(sap_lib_path) / "ace64.dll").string();
    }

    lib_handle sap = nullptr;
    if (!sap_lib_path.empty()) {
        sap = lib_open(sap_lib_path.c_str());
    } else {
        for (const auto& p : default_sap_paths()) {
            sap = lib_open(p.c_str());
            if (sap) break;
        }
    }
    if (!sap) {
        emit_result(false, 0, 0, 0,
            "Cannot load SAP ACE library: " + lib_error() +
            ". Specify the path with --sap-lib.",
            warnings);
        return 1;
    }

    // ── Step 3: resolve SAP functions ────────────────────────────────────────
    // All resolved by name on every platform.  SAP ACE exports named symbols
    // in every version's import library and DLL — names are stable across
    // versions; ordinals are not.
    SapFuncs f;
    f.connect    = (PFN_Connect)    lib_sym(sap, "AdsConnect60");
    f.disconnect = (PFN_Disconnect) lib_sym(sap, "AdsDisconnect");
    f.execSQL    = (PFN_ExecSQL)    lib_sym(sap, "AdsExecuteSQLDirect");
    f.close      = (PFN_Close)      lib_sym(sap, "AdsCloseTable");
    f.createStmt = (PFN_CreateStmt) lib_sym(sap, "AdsCreateSQLStatement");
    f.atEOF      = (PFN_AtEOF)     lib_sym(sap, "AdsAtEOF");
    f.skip       = (PFN_Skip)      lib_sym(sap, "AdsSkip");
    f.getField   = (PFN_GetField)  lib_sym(sap, "AdsGetField");
    f.numFields  = (PFN_NumFields) lib_sym(sap, "AdsGetNumFields");
    f.fieldName  = (PFN_FieldName) lib_sym(sap, "AdsGetFieldName");
    f.getPerms   = (PFN_GetPerms)  lib_sym(sap, "AdsDDGetPermissions");
    f.findFirst  = (PFN_FindFirst) lib_sym(sap, "AdsDDFindFirstObject");
    f.findNext   = (PFN_FindNext)  lib_sym(sap, "AdsDDFindNextObject");
    f.findClose  = (PFN_FindClose) lib_sym(sap, "AdsDDFindClose");

    if (!f.connect || !f.disconnect || !f.execSQL || !f.close ||
        !f.createStmt || !f.atEOF || !f.skip || !f.getField) {
        lib_close(sap);
        emit_result(false, 0, 0, 0, "SAP library missing required exports.", warnings);
        return 1;
    }

    // ── Step 4: connect to SOURCE via SAP LOCAL server ────────────────────
    ADSHANDLE hConn = 0;
    UNSIGNED32 rc = f.connect(
        (UNSIGNED8*)source.c_str(), 1,
        (UNSIGNED8*)user.c_str(),
        (UNSIGNED8*)password.c_str(),
        0, &hConn);
    if (rc != 0) {
        lib_close(sap);
        char msg[128];
        std::snprintf(msg, sizeof(msg),
            "SAP connect failed (rc=%u). Check credentials and source path.", rc);
        emit_result(false, 0, 0, 0, msg, warnings);
        return 1;
    }

    ADSHANDLE hStmt = 0;
    rc = f.createStmt(hConn, &hStmt);
    if (rc != 0 || !hStmt) {
        f.disconnect(hConn);
        lib_close(sap);
        emit_result(false, 0, 0, 0, "AdsCreateSQLStatement failed.", warnings);
        return 1;
    }

    // ── Step 5: read all user→group memberships ──────────────────────────────
    // We read every membership from SAP, then re-write them using add_user_to_group
    // (which sets prop_null=false).  clear_sap_permissions() later wipes the original
    // SAP-written records (prop_null=true), so pre-importing ALL memberships is
    // essential — otherwise regular groups (Administrators, Supervisors, etc.) vanish.
    struct Membership { std::string user, group; };
    std::vector<Membership> memberships;
    {
        ADSHANDLE hc = 0;
        rc = f.execSQL(hStmt,
            (UNSIGNED8*)"SELECT User_Name, Group_Name "
                        "FROM system.usergroupmembers "
                        "ORDER BY User_Name, Group_Name",
            &hc);
        if (rc == 0 && hc) {
            UNSIGNED16 eof = 0;
            while (f.atEOF(hc, &eof) == 0 && !eof) {
                auto u = sap_field(f, hc, "User_Name");
                auto g = sap_field(f, hc, "Group_Name");
                if (!u.empty() && !g.empty())
                    memberships.push_back({u, g});
                f.skip(hc, 1);
            }
            f.close(hc);
        } else {
            warnings.push_back("system.usergroupmembers query failed — group memberships skipped.");
        }
    }

    // ── Step 5b: read user-defined function bodies from system.functions ────────
    // SAP stores function bodies in an encrypted binary blob inside the .add
    // record.  system.functions decrypts and returns the readable SQL body.
    // We overwrite the encrypted blobs in the dest .add with a readable lstr
    // layout so proc_body.php can display them without needing the SAP DLL.
    //
    // system.functions column names (SAP 11.x):
    //   Name | Package | Return Type | Input Parameters | Implementation | Comment
    struct FuncBody {
        std::string name, return_type, input_params, body;
    };
    std::vector<FuncBody> func_bodies;
    {
        ADSHANDLE hc = 0;
        rc = f.execSQL(hStmt,
            (UNSIGNED8*)"SELECT Name, [Return Type], [Input Parameters], Implementation "
                        "FROM system.functions ORDER BY Name",
            &hc);
        if (rc == 0 && hc) {
            UNSIGNED16 eof = 0;
            while (f.atEOF(hc, &eof) == 0 && !eof) {
                FuncBody fb;
                fb.name         = sap_field(f, hc, "Name");
                fb.return_type  = sap_field(f, hc, "Return Type");
                fb.input_params = sap_field(f, hc, "Input Parameters");
                fb.body         = sap_field_large(f, hc, "Implementation");
                if (!fb.name.empty())
                    func_bodies.push_back(std::move(fb));
                f.skip(hc, 1);
            }
            f.close(hc);
        } else {
            warnings.push_back("system.functions query failed — function bodies skipped.");
        }
    }

    // ── Step 5c: read stored-procedure input/output params from SAP ──────────
    // SAP stores params in the property zone of each StoredProc record.  OpenADS
    // loses them when it rewrites the record (join_nul truncates at 273 bytes).
    // We query system.storedprocedures while the SAP connection is still open,
    // then patch the dest .am continuation after step 8 so proc_body.php can
    // read the params even though they no longer fit in the inline property area.
    //
    // SAP system.storedprocedures columns (version 11.x):
    //   Name | Proc_Input | Proc_Output | SQL_Script | …
    struct ProcParams {
        std::string name, input_params, output_params;
    };
    std::vector<ProcParams> proc_params;
    {
        ADSHANDLE hc = 0;
        rc = f.execSQL(hStmt,
            (UNSIGNED8*)"SELECT Name, Proc_Input, Proc_Output "
                        "FROM system.storedprocedures ORDER BY Name",
            &hc);
        if (rc == 0 && hc) {
            UNSIGNED16 eof = 0;
            while (f.atEOF(hc, &eof) == 0 && !eof) {
                ProcParams pp;
                pp.name          = sap_field(f, hc, "Name");
                pp.input_params  = sap_field(f, hc, "Proc_Input");
                pp.output_params = sap_field(f, hc, "Proc_Output");
                if (!pp.name.empty() && (!pp.input_params.empty() || !pp.output_params.empty()))
                    proc_params.push_back(std::move(pp));
                f.skip(hc, 1);
            }
            f.close(hc);
        } else {
            warnings.push_back("system.storedprocedures query failed — SP params skipped.");
        }
    }

    // ── Step 5d: read table primary-key and default-index from SAP system.tables ──
    struct TableProps {
        std::string name, primary_key, default_index;
    };
    std::vector<TableProps> table_props_list;
    {
        ADSHANDLE hc = 0;
        rc = f.execSQL(hStmt,
            (UNSIGNED8*)"SELECT Name, Table_Primary_Key, Table_Default_Index "
                        "FROM system.tables ORDER BY Name",
            &hc);
        if (rc == 0 && hc) {
            UNSIGNED16 eof = 0;
            while (f.atEOF(hc, &eof) == 0 && !eof) {
                TableProps tp;
                tp.name          = sap_field(f, hc, "Name");
                tp.primary_key   = sap_field(f, hc, "Table_Primary_Key");
                tp.default_index = sap_field(f, hc, "Table_Default_Index");
                if (!tp.name.empty())
                    table_props_list.push_back(std::move(tp));
                f.skip(hc, 1);
            }
            f.close(hc);
        } else {
            warnings.push_back("system.tables query failed — table primary keys skipped.");
        }
    }

    // ── Step 6: read object permissions via AdsDDGetPermissions ─────────────
    // system.permissions SQL returns 0 rows for SAP binary .add files because
    // the real ACLs are stored in encrypted property blobs.  Use the
    // AdsDDGetPermissions API instead, which decrypts them on the fly.
    // We enumerate all grantees × all objects using AdsDDFindFirstObject/Next
    // (avoids SQL queries to system.* which fail in local-server binary-DD mode).

    // Object type codes from ADS_DD_*_OBJECT constants in ace.h
    static const UNSIGNED16 kObjTable = 1;
    static const UNSIGNED16 kObjView  = 6;
    static const UNSIGNED16 kObjProc  = 10;
    static const UNSIGNED16 kObjFunc  = 18;
    static const UNSIGNED16 kObjUser  = 8;
    static const UNSIGNED16 kObjGroup = 9;

    struct Perm {
        std::string obj_type, obj_name, grantee;
        uint32_t    bitmask;
    };
    std::vector<Perm> perms;

    // Helper: enumerate DD objects of a given type using AdsDDFindFirst/Next.
    auto dd_enum = [&](UNSIGNED16 type) -> std::vector<std::string> {
        std::vector<std::string> result;
        if (!f.findFirst || !f.findNext || !f.findClose) return result;
        char nameBuf[256] = {};
        UNSIGNED16 nameLen = sizeof(nameBuf) - 1;
        ADSHANDLE hFind = 0;
        UNSIGNED32 rc2 = f.findFirst(hConn, type, nullptr,
                                     (UNSIGNED8*)nameBuf, &nameLen, &hFind);
        while (rc2 == 0 && hFind) {
            nameBuf[nameLen] = '\0';
            // Trim trailing spaces
            for (int i = (int)nameLen - 1; i >= 0 && nameBuf[i] == ' '; --i)
                nameBuf[i] = '\0';
            if (nameBuf[0]) result.push_back(nameBuf);
            nameLen = sizeof(nameBuf) - 1;
            nameBuf[0] = '\0';
            rc2 = f.findNext(hConn, hFind, (UNSIGNED8*)nameBuf, &nameLen);
        }
        if (hFind) f.findClose(hConn, hFind);
        return result;
    };

    if (!f.getPerms) {
        warnings.push_back("AdsDDGetPermissions not found in SAP library — ACLs skipped.");
    } else if (!f.findFirst) {
        warnings.push_back("AdsDDFindFirstObject not found in SAP library — ACLs skipped.");
    } else {
        // Collect all grantees: users and groups
        struct Grantee { std::string name; };
        std::vector<Grantee> grantees;
        for (auto& nm : dd_enum(kObjUser))  grantees.push_back({nm});
        for (auto& nm : dd_enum(kObjGroup)) grantees.push_back({nm});

        // Collect all securable objects: tables, views, stored procs, functions
        struct Obj { std::string name, type_name; UNSIGNED16 type_code; };
        std::vector<Obj> objects;
        for (auto& nm : dd_enum(kObjTable)) objects.push_back({nm, "Table",      kObjTable});
        for (auto& nm : dd_enum(kObjView))  objects.push_back({nm, "View",       kObjView});
        for (auto& nm : dd_enum(kObjProc))  objects.push_back({nm, "StoredProc", kObjProc});
        for (auto& nm : dd_enum(kObjFunc))  objects.push_back({nm, "Function",   kObjFunc});

        // Probe each (grantee × object) pair for direct permissions
        for (const auto& g : grantees) {
            for (const auto& o : objects) {
                UNSIGNED32 mask = 0;
                UNSIGNED32 prc = f.getPerms(hConn,
                    (UNSIGNED8*)g.name.c_str(),
                    o.type_code,
                    (UNSIGNED8*)o.name.c_str(),
                    nullptr,
                    0,      // direct permissions only (not inherited)
                    &mask);
                if (prc == 0 && mask != 0)
                    perms.push_back({o.type_name, o.name, g.name, mask});
            }
        }
    }

    f.close(hStmt);
    f.disconnect(hConn);
    lib_close(sap);

    // ── Step 7: open dest with OpenADS and write imported data ───────────────
    // Wrapped in a scope so DataDict (RAII) is destroyed — and the file fully
    // flushed — before we binary-patch function bodies in Step 8.
    int written_memberships = 0;
    int written_perms = 0;
    {
        auto opened = openads::engine::DataDict::open(dest);
        if (!opened) {
            emit_result(false, 0, 0, 0,
                "Cannot open dest DD: " + opened.error().message, warnings);
            return 1;
        }
        auto dd = std::move(opened).value();

        for (const auto& tp : table_props_list) {
            if (!tp.primary_key.empty())
                dd.set_table_property(tp.name, 202, tp.primary_key);
            if (!tp.default_index.empty())
                dd.set_table_property(tp.name, 213, tp.default_index);
        }

        for (const auto& m : memberships) {
            auto r = dd.add_user_to_group(m.user, m.group);
            if (r) ++written_memberships;
            else warnings.push_back("add_user_to_group(" + m.user + "," + m.group + "): " + r.error().message);
        }

        for (const auto& p : perms) {
            auto r = dd.grant_permission(p.obj_type, p.obj_name, p.grantee, p.bitmask);
            if (r) ++written_perms;
            else warnings.push_back("grant_permission(" + p.obj_type + "," + p.obj_name + "," + p.grantee + "): " + r.error().message);
        }

        // Remove all SAP-written encrypted Permission records from the dest file.
        // Even when system.permissions returned 0 rows (SAP stores real ACLs in
        // encrypted blobs not exposed via SQL), the original SAP records must be
        // deleted so OpenADS no longer raises AE_SAP_PERMS_NEED_IMPORT (5174).
        auto rc2 = dd.clear_sap_permissions();
        if (!rc2) warnings.push_back("clear_sap_permissions: " + rc2.error().message);
    } // DataDict destroyed and file flushed here

    // ── Step 8: binary-patch encrypted function bodies in dest .add ──────────
    // SAP stores function bodies as encrypted blobs in the .add property area.
    // OpenADS cannot decrypt them.  We overwrite them with a plain lstr layout:
    //   plen = 0 (so proc_body.php starts reading at offset 0 in property)
    //   property[0..1]   = le16(len(return_type))
    //   property[2..]    = return_type bytes
    //   next le16 + data = input_params
    //   next le16 + data = body
    // This matches the lstr branch in proc_body.php's Function handler.

    int written_func_bodies = 0;
    if (!func_bodies.empty()) {
        auto write_u16_fn = [](std::uint8_t* p, std::uint16_t v) {
            p[0] = static_cast<std::uint8_t>(v & 0xFF);
            p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        };
        auto read_u32_fn = [](const std::uint8_t* p) -> std::uint32_t {
            return static_cast<std::uint32_t>(p[0])
                 | (static_cast<std::uint32_t>(p[1]) <<  8)
                 | (static_cast<std::uint32_t>(p[2]) << 16)
                 | (static_cast<std::uint32_t>(p[3]) << 24);
        };

        // Read dest .add into memory
        std::vector<std::uint8_t> addData;
        bool read_ok = false;
        {
            std::FILE* fh = std::fopen(dest.c_str(), "rb");
            if (!fh) {
                warnings.push_back("Cannot read dest .add for function body patch.");
            } else {
                std::fseek(fh, 0, SEEK_END);
                long sz = std::ftell(fh);
                std::rewind(fh);
                if (sz > 0) {
                    addData.resize(static_cast<std::size_t>(sz));
                    std::fread(addData.data(), 1, addData.size(), fh);
                    read_ok = true;
                }
                std::fclose(fh);
            }
        }

        if (read_ok && addData.size() >= 40) {
            std::uint32_t hdrLen = read_u32_fn(addData.data() + 0x20);
            std::uint32_t recLen = read_u32_fn(addData.data() + 0x24);

            if (recLen > 0 && hdrLen < addData.size()) {
                std::uint32_t total = static_cast<std::uint32_t>(
                    (addData.size() - hdrLen) / recLen);

                for (std::uint32_t i = 0; i < total; ++i) {
                    std::uint8_t* base = addData.data() + hdrLen + i * recLen;
                    if (base[0] != 0x04) continue;

                    // objType: bytes [13..22], 10 chars, space/NUL trimmed
                    char objType[11] = {};
                    std::memcpy(objType, base + 13, 10);
                    for (int k = 9; k >= 0 && (objType[k] == ' ' || objType[k] == '\0'); --k)
                        objType[k] = '\0';
                    if (std::strcmp(objType, "Function") != 0) continue;

                    // objName: bytes [23..222], 200 chars, space/NUL trimmed
                    char objName[201] = {};
                    std::memcpy(objName, base + 23, 200);
                    for (int k = 199; k >= 0 && (objName[k] == ' ' || objName[k] == '\0'); --k)
                        objName[k] = '\0';

                    // Find matching FuncBody (case-insensitive)
                    for (const auto& fb : func_bodies) {
#ifdef _WIN32
                        if (_stricmp(fb.name.c_str(), objName) != 0) continue;
#else
                        if (strcasecmp(fb.name.c_str(), objName) != 0) continue;
#endif
                        // Record layout: plen at [223..224], property at [225..497] (273 bytes).
                        constexpr std::uint32_t PL = 273;
                        write_u16_fn(base + 223, 0);    // plen = 0
                        std::uint8_t* PS = base + 225;
                        std::memset(PS, 0, PL);

                        std::uint32_t pos = 0;
                        auto write_lstr = [&](const std::string& s) {
                            if (PL - pos < 2) return;
                            auto slen = static_cast<std::uint16_t>(
                                (std::min)(s.size(), static_cast<std::size_t>(PL - pos - 2)));
                            write_u16_fn(PS + pos, slen);
                            pos += 2;
                            if (slen) std::memcpy(PS + pos, s.data(), slen);
                            pos += slen;
                        };

                        write_lstr(fb.return_type);
                        write_lstr(fb.input_params);
                        write_lstr(fb.body);
                        ++written_func_bodies;
                        break;
                    }
                }
            }

            // Write patched .add back to disk
            std::FILE* fh = std::fopen(dest.c_str(), "wb");
            if (!fh) {
                warnings.push_back("Cannot write patched .add for function bodies.");
            } else {
                std::fwrite(addData.data(), 1, addData.size(), fh);
                std::fclose(fh);
            }
        }
    }

    // ── Step 8b: patch .am continuation with SP input/output params ──────────
    // After OpenADS rewrites the dest .add, each StoredProc record has plen=273
    // and its .am block contains:
    //   [SQL body continuation] [NUL] [old SAP padding/binary garbage]
    //
    // We overwrite the bytes after the first NUL with:
    //   [input_params] [NUL] [output_params] [NUL]
    //
    // proc_body.php then reads these after finding the first NUL in .am.
    int written_proc_params = 0;
    if (!proc_params.empty()) {
        // Re-read .add (it was modified by DataDict above)
        std::vector<std::uint8_t> addData2;
        bool read_ok2 = false;
        {
            std::FILE* fh = std::fopen(dest.c_str(), "rb");
            if (fh) {
                std::fseek(fh, 0, SEEK_END);
                long sz = std::ftell(fh);
                std::rewind(fh);
                if (sz > 0) {
                    addData2.resize(static_cast<std::size_t>(sz));
                    std::fread(addData2.data(), 1, addData2.size(), fh);
                    read_ok2 = true;
                }
                std::fclose(fh);
            }
        }

        // Build .am path: replace the last extension in dest with .am
        std::string amDest;
        {
            auto sl  = dest.rfind('/');
            auto bs  = dest.rfind('\\');
            std::size_t lastSlash;
            if      (sl == std::string::npos && bs == std::string::npos) lastSlash = std::string::npos;
            else if (sl == std::string::npos) lastSlash = bs;
            else if (bs == std::string::npos) lastSlash = sl;
            else lastSlash = (sl > bs) ? sl : bs;
            auto dot = dest.rfind('.');
            if (dot == std::string::npos || (lastSlash != std::string::npos && dot < lastSlash))
                amDest = dest + ".am";
            else
                amDest = dest.substr(0, dot) + ".am";
        }
        std::vector<std::uint8_t> amData;
        bool am_read_ok = false;
        {
            std::FILE* fh = std::fopen(amDest.c_str(), "rb");
            if (fh) {
                std::fseek(fh, 0, SEEK_END);
                long sz = std::ftell(fh);
                std::rewind(fh);
                if (sz > 0) {
                    amData.resize(static_cast<std::size_t>(sz));
                    std::fread(amData.data(), 1, amData.size(), fh);
                    am_read_ok = true;
                }
                std::fclose(fh);
            }
        }

        if (read_ok2 && am_read_ok && addData2.size() >= 40) {
            auto read_u32_fn = [](const std::uint8_t* p) -> std::uint32_t {
                return static_cast<std::uint32_t>(p[0])
                     | (static_cast<std::uint32_t>(p[1]) <<  8)
                     | (static_cast<std::uint32_t>(p[2]) << 16)
                     | (static_cast<std::uint32_t>(p[3]) << 24);
            };
            auto write_u32_fn = [](std::uint8_t* p, std::uint32_t v) {
                p[0] = static_cast<std::uint8_t>(v & 0xFF);
                p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
                p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
                p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
            };

            std::uint32_t hdrLen = read_u32_fn(addData2.data() + 0x20);
            std::uint32_t recLen = read_u32_fn(addData2.data() + 0x24);
            bool am_modified = false;

            if (recLen > 0 && hdrLen < addData2.size()) {
                std::uint32_t total2 = static_cast<std::uint32_t>(
                    (addData2.size() - hdrLen) / recLen);

                for (std::uint32_t i = 0; i < total2; ++i) {
                    std::uint8_t* base = addData2.data() + hdrLen + i * recLen;
                    if (base[0] != 0x04) continue;

                    char objType[11] = {};
                    std::memcpy(objType, base + 13, 10);
                    for (int k = 9; k >= 0 && (objType[k] == ' ' || objType[k] == '\0'); --k)
                        objType[k] = '\0';
                    if (std::strcmp(objType, "StoredProc") != 0 &&
                        std::strcmp(objType, "Procedure")  != 0) continue;

                    char objName[201] = {};
                    std::memcpy(objName, base + 23, 200);
                    for (int k = 199; k >= 0 && (objName[k] == ' ' || objName[k] == '\0'); --k)
                        objName[k] = '\0';

                    // Find matching ProcParams entry
                    const ProcParams* pp = nullptr;
                    for (const auto& p2 : proc_params) {
#ifdef _WIN32
                        if (_stricmp(p2.name.c_str(), objName) == 0) { pp = &p2; break; }
#else
                        if (strcasecmp(p2.name.c_str(), objName) == 0) { pp = &p2; break; }
#endif
                    }
                    if (!pp) continue;

                    // Read am_block and am_len from more_property [498..505]
                    std::uint32_t amBlock = read_u32_fn(base + 498);
                    std::uint32_t amLen   = read_u32_fn(base + 502);
                    if (amBlock == 0 || amLen == 0) continue;

                    std::size_t amOff = static_cast<std::size_t>(amBlock) * 8;
                    if (amOff >= amData.size()) continue;
                    std::size_t amEnd = (std::min)(amOff + amLen, amData.size());

                    // Find first NUL in the .am block (end of SQL body)
                    std::size_t nulPos = amEnd; // not found sentinel
                    for (std::size_t j = amOff; j < amEnd; ++j) {
                        if (amData[j] == 0) { nulPos = j; break; }
                    }
                    if (nulPos == amEnd) continue; // no NUL → skip

                    // Write input_params\0output_params\0 after the NUL
                    std::string tail;
                    tail += pp->input_params;
                    tail += '\0';
                    tail += pp->output_params;
                    tail += '\0';

                    std::size_t writeOff = nulPos + 1; // byte after the SQL-body NUL
                    std::size_t avail    = amEnd - writeOff;
                    if (tail.size() > avail) {
                        // Params don't fit; extend .am buffer and update am_len
                        std::size_t newEnd = writeOff + tail.size();
                        if (newEnd > amData.size()) amData.resize(newEnd, 0);
                        std::uint32_t newLen = static_cast<std::uint32_t>(
                            (newEnd - amOff));
                        write_u32_fn(base + 502, newLen);
                    }
                    std::memcpy(amData.data() + writeOff, tail.data(), tail.size());
                    am_modified = true;
                    ++written_proc_params;
                }
            }

            if (am_modified) {
                // Write patched .add back (am_len fields may have changed)
                {
                    std::FILE* fh = std::fopen(dest.c_str(), "wb");
                    if (fh) {
                        std::fwrite(addData2.data(), 1, addData2.size(), fh);
                        std::fclose(fh);
                    } else {
                        warnings.push_back("Cannot write patched .add for SP params.");
                    }
                }
                // Write patched .am
                {
                    std::FILE* fh = std::fopen(amDest.c_str(), "wb");
                    if (fh) {
                        std::fwrite(amData.data(), 1, amData.size(), fh);
                        std::fclose(fh);
                    } else {
                        warnings.push_back("Cannot write patched .am for SP params.");
                    }
                }
            }
        } else if (!am_read_ok) {
            warnings.push_back("Cannot read dest .am for SP param patch — params not written.");
        }
    }

    emit_result(true, written_memberships, written_perms, written_func_bodies, "", warnings);
    return 0;
}
