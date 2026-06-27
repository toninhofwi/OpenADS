// Generates committed ADT/ADI/ADM fixtures under tests/fixtures/adi/.
// Run from repo root after building:
//   cmake --build build/default --config Release --target generate_adi_fixtures
//   build/default/tests/Release/generate_adi_fixtures.exe
//     [output_dir]   (default: tests/fixtures/adi relative to cwd)

#include "openads/ace.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void die(const char* msg) {
    std::fprintf(stderr, "[generate_adi_fixtures] %s\n", msg);
    std::exit(1);
}

void check(UNSIGNED32 rc, const char* step) {
    if (rc != AE_SUCCESS) {
        std::fprintf(stderr, "[generate_adi_fixtures] %s failed (rc=%u)\n",
                     step, rc);
        std::exit(2);
    }
}

void set_str(ADSHANDLE h, const char* field, const std::string& val) {
    UNSIGNED8 fld[64]{};
    std::strncpy(reinterpret_cast<char*>(fld), field, sizeof(fld) - 1);
    check(AdsSetString(h, fld,
                       reinterpret_cast<UNSIGNED8*>(const_cast<char*>(val.data())),
                       static_cast<UNSIGNED32>(val.size())),
          "AdsSetString");
}

void set_logical(ADSHANDLE h, const char* field, int v) {
    UNSIGNED8 fld[64]{};
    std::strncpy(reinterpret_cast<char*>(fld), field, sizeof(fld) - 1);
    check(AdsSetLogical(h, fld, static_cast<UNSIGNED16>(v)), "AdsSetLogical");
}

void set_short(ADSHANDLE h, const char* field, SIGNED32 v) {
    UNSIGNED8 fld[64]{};
    std::strncpy(reinterpret_cast<char*>(fld), field, sizeof(fld) - 1);
    check(AdsSetDouble(h, fld, static_cast<double>(v)), "AdsSetDouble(ShortInt)");
}

void set_double(ADSHANDLE h, const char* field, double v) {
    UNSIGNED8 fld[64]{};
    std::strncpy(reinterpret_cast<char*>(fld), field, sizeof(fld) - 1);
    check(AdsSetDouble(h, fld, v), "AdsSetDouble");
}

ADSHANDLE connect_dir(const fs::path& dir) {
    const std::string s = dir.string();
    if (s.size() >= 260) die("output path too long");
    UNSIGNED8 srv[260]{};
    std::memcpy(srv, s.c_str(), s.size());
    ADSHANDLE hConn = 0;
    check(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn),
          "AdsConnect60");
    return hConn;
}

void wipe_table_files(const fs::path& dir, const char* base) {
    std::error_code ec;
    fs::remove(dir / (std::string(base) + ".adt"), ec);
    fs::remove(dir / (std::string(base) + ".adi"), ec);
    fs::remove(dir / (std::string(base) + ".adm"), ec);
}

void create_index(ADSHANDLE hTable, const char* adi, const char* tag,
                  const char* expr) {
    UNSIGNED8 idxfile[64]{};
    UNSIGNED8 idxname[64]{};
    UNSIGNED8 idxexpr[64]{};
    std::strncpy(reinterpret_cast<char*>(idxfile), adi, sizeof(idxfile) - 1);
    std::strncpy(reinterpret_cast<char*>(idxname), tag, sizeof(idxname) - 1);
    std::strncpy(reinterpret_cast<char*>(idxexpr), expr, sizeof(idxexpr) - 1);
    ADSHANDLE hIdx = 0;
    UNSIGNED32 rc = AdsCreateIndex61(hTable, idxfile, idxname, idxexpr,
                                     nullptr, nullptr, 0, 0, &hIdx);
    if (rc != AE_SUCCESS) {
        std::fprintf(stderr,
                     "[generate_adi_fixtures] AdsCreateIndex61(%s/%s) failed (rc=%u)\n",
                     adi, tag, rc);
        std::exit(2);
    }
}

void build_landlords(const fs::path& dir) {
    wipe_table_files(dir, "landlords");

    ADSHANDLE hConn = connect_dir(dir);

    UNSIGNED8 tbl[] = "landlords.adt";
    UNSIGNED8 flddef[] =
        "LandLordID,CiCharacter,25;"
        "FName,Character,25;"
        "LName,Character,25;"
        "Tel,Character,15;"
        "address,Character,40;"
        "city,Character,25;"
        "State,Character,2;"
        "ZipCode,Character,10;"
        "email,Character,40;"
        "Notes,Memo;"
        "inactive,Logical;"
        "ManagerID,CiCharacter,13;"
        "Country,Character,25";

    ADSHANDLE hTable = 0;
    check(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                         flddef, &hTable),
          "AdsCreateTable(landlords)");

    struct Row {
        const char* id;
        const char* fname;
        const char* lname;
        const char* notes;
        int         inactive;
    };
    const Row rows[] = {
        {"LL001", "Alice",   "Anderson", "",                  0},
        {"LL002", "Bob",     "Baker",    "",                  0},
        {"LL003", "Carol",   "Clark",    "",                  1},
        {"LL004", "Diana",   "Davis",    "SEC 8 preferred",   0},
        {"LL005", "Edward",  "Evans",    "",                  0},
        {"LL006", "Fiona",   "Foster",   "",                  0},
        {"LL007", "George",  "Green",    "",                  1},
    };

    for (const Row& r : rows) {
        check(AdsAppendRecord(hTable), "AdsAppendRecord(landlords)");
        set_str(hTable, "LandLordID", r.id);
        set_str(hTable, "FName", r.fname);
        set_str(hTable, "LName", r.lname);
        set_str(hTable, "city", "Springfield");
        set_str(hTable, "State", "IL");
        set_str(hTable, "ManagerID", "MGR0000000001");
        if (r.notes[0]) set_str(hTable, "Notes", r.notes);
        set_logical(hTable, "inactive", r.inactive);
        check(AdsWriteRecord(hTable), "AdsWriteRecord(landlords)");
    }

    create_index(hTable, "landlords.adi", "LandLordID", "LandLordID");

    check(AdsCloseTable(hTable), "AdsCloseTable(landlords)");
    check(AdsDisconnect(hConn), "AdsDisconnect(landlords)");
}

void build_leases(const fs::path& dir) {
    wipe_table_files(dir, "leases");

    ADSHANDLE hConn = connect_dir(dir);

    UNSIGNED8 tbl[] = "leases.adt";
    UNSIGNED8 flddef[] =
        "leaseid,CiCharacter,13;"
        "propertyID,CiCharacter,13;"
        "LandLordID,CiCharacter,13;"
        "ManagerID,CiCharacter,13;"
        "TenantName,Character,40;"
        "EndDate,Date;"
        "Rent,Money;"
        "LateFee,Money;"
        "created,Timestamp";

    ADSHANDLE hTable = 0;
    check(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                         flddef, &hTable),
          "AdsCreateTable(leases)");

    constexpr int kN = 245;
    for (int i = 1; i <= kN; ++i) {
        char leaseid[16];
        char propid[16];
        char llid[16];
        char mgrid[16];
        char tenant[32];
        char enddate[16];
        char created[24];
        std::snprintf(leaseid, sizeof(leaseid), "L%012d", i);
        std::snprintf(propid,  sizeof(propid),  "P%012d", (i % 50) + 1);
        std::snprintf(llid,    sizeof(llid),    "LL%011d", (i % 7) + 1);
        std::snprintf(mgrid,   sizeof(mgrid),   "MGR%010d", (i % 3) + 1);
        std::snprintf(tenant,  sizeof(tenant),  "Tenant %03d", i);
        std::snprintf(enddate, sizeof(enddate), "2026%02d15",
                      ((i - 1) % 12) + 1);
        std::snprintf(created, sizeof(created), "202501%02d120000",
                      ((i - 1) % 28) + 1);

        check(AdsAppendRecord(hTable), "AdsAppendRecord(leases)");
        set_str(hTable, "leaseid", leaseid);
        set_str(hTable, "propertyID", propid);
        set_str(hTable, "LandLordID", llid);
        set_str(hTable, "ManagerID", mgrid);
        set_str(hTable, "TenantName", tenant);
        set_str(hTable, "EndDate", enddate);
        set_double(hTable, "Rent", 800.0 + (i % 120) * 12.5);
        set_double(hTable, "LateFee", 50.0 + (i % 5) * 10.0);
        set_str(hTable, "created", created);
        check(AdsWriteRecord(hTable), "AdsWriteRecord(leases)");
    }

    create_index(hTable, "leases.adi", "leaseid", "leaseid");
    create_index(hTable, "leases.adi", "propertyID", "propertyID");
    create_index(hTable, "leases.adi", "LandLordID", "LandLordID");
    create_index(hTable, "leases.adi", "ManagerID", "ManagerID");

    check(AdsCloseTable(hTable), "AdsCloseTable(leases)");
    check(AdsDisconnect(hConn), "AdsDisconnect(leases)");
}

void build_properties(const fs::path& dir) {
    wipe_table_files(dir, "properties");

    ADSHANDLE hConn = connect_dir(dir);

    UNSIGNED8 tbl[] = "properties.adt";
    UNSIGNED8 flddef[] =
        "propertyID,CiCharacter,13;"
        "Address,Character,40;"
        "TotalUnits,ShortInt;"
        "PurchaseDate,Date";

    ADSHANDLE hTable = 0;
    check(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                         flddef, &hTable),
          "AdsCreateTable(properties)");

    struct Row { int units; const char* purchased; };
    const Row rows[] = {
        {1, "20150315"},
        {1, "20180601"},
        {6, "20200110"},
        {4, "20191120"},
        {2, "20220704"},
    };
    for (std::size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        char propid[16];
        std::snprintf(propid, sizeof(propid), "P%012zu", i + 1);
        check(AdsAppendRecord(hTable), "AdsAppendRecord(properties)");
        set_str(hTable, "propertyID", propid);
        set_str(hTable, "Address", "100 Main St");
        set_short(hTable, "TotalUnits", rows[i].units);
        set_str(hTable, "PurchaseDate", rows[i].purchased);
        check(AdsWriteRecord(hTable), "AdsWriteRecord(properties)");
    }

    check(AdsCloseTable(hTable), "AdsCloseTable(properties)");
    check(AdsDisconnect(hConn), "AdsDisconnect(properties)");
}

} // namespace

int main(int argc, char* argv[]) {
    fs::path out = (argc > 1)
        ? fs::path(argv[1])
        : fs::path("tests/fixtures/adi");

    std::error_code ec;
    fs::create_directories(out, ec);

    std::printf("[generate_adi_fixtures] writing to %s\n", out.string().c_str());

    build_landlords(out);
    build_leases(out);
    build_properties(out);

    std::printf("[generate_adi_fixtures] done: landlords, leases, properties\n");
    return 0;
}