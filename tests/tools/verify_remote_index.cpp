// verify_remote_index.cpp — client/server check: index auto-open + order walk.
#include "openads/ace.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void die(const char* msg, UNSIGNED32 rc = 0) {
    std::fprintf(stderr, "FAIL: %s", msg);
    if (rc) std::fprintf(stderr, " (rc=%u)", rc);
    std::fputc('\n', stderr);
    std::exit(1);
}

static std::string get_field(ADSHANDLE h, std::uint16_t ord) {
    UNSIGNED8 buf[256] = {0};
    UNSIGNED32 cap = sizeof(buf) - 1;
    auto p = reinterpret_cast<UNSIGNED8*>(static_cast<std::uintptr_t>(ord));
    if (AdsGetField(h, p, buf, &cap, 0) != 0)
        return {};
    return std::string(reinterpret_cast<char*>(buf), cap);
}

static std::string get_field_name(ADSHANDLE h, const char* name) {
    UNSIGNED8 buf[256] = {0};
    UNSIGNED32 cap = sizeof(buf) - 1;
    if (AdsGetField(h, reinterpret_cast<UNSIGNED8*>(const_cast<char*>(name)),
                    buf, &cap, 0) != 0)
        return {};
    return std::string(reinterpret_cast<char*>(buf), cap);
}

static std::vector<std::string> walk_names(ADSHANDLE h, int max_rows) {
    std::vector<std::string> out;
    AdsGotoTop(h);
    for (int i = 0; i < max_rows; ++i) {
        UNSIGNED16 eof = 0;
        AdsAtEOF(h, &eof);
        if (eof) break;
        UNSIGNED32 rec = 0;
        AdsGetRecordNum(h, 0, &rec);
        std::string nm = get_field_name(h, "NAME");
        if (nm.empty()) nm = get_field(h, 2);
        out.push_back(nm);
        std::printf("  rec=%u name=[%s]\n", rec, nm.c_str());
        AdsSkip(h, 1);
    }
    return out;
}

static bool names_sorted(const std::vector<std::string>& v) {
    for (std::size_t i = 1; i < v.size(); ++i) {
        if (v[i] < v[i - 1]) return false;
    }
    return true;
}

int main(int argc, char** argv) {
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? std::atoi(argv[2]) : 16262;
    const char* data = (argc > 3) ? argv[3]
                                  : "C:/OpenADS/testdata/invoices";

    char uri[512];
    std::snprintf(uri, sizeof(uri), "tcp://%s:%d/%s", host, port, data);
    std::printf("URI: %s\n", uri);

    ADSHANDLE hConn = 0, hT = 0;
    UNSIGNED8 uuri[256];
    std::snprintf(reinterpret_cast<char*>(uuri), sizeof(uuri), "%s", uri);
    UNSIGNED32 crc = AdsConnect60(uuri, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn);
    if (crc != 0)
        die("AdsConnect60", crc);

    UNSIGNED8 tname[] = "customer.dbf";
    if (AdsOpenTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, &hT) != 0)
        die("AdsOpenTable");

    UNSIGNED16 nidx = 0;
    AdsGetNumIndexes(hT, &nidx);
    std::printf("Indexes auto-opened: %u\n", static_cast<unsigned>(nidx));
    if (nidx < 2) die("expected >= 2 indexes");

    for (UNSIGNED16 ord = 1; ord <= nidx; ++ord) {
        ADSHANDLE hIx = 0;
        AdsGetIndexHandleByOrder(hT, ord, &hIx);
        char tag[64] = {};
        UNSIGNED16 tlen = sizeof(tag) - 1;
        AdsGetIndexName(hIx, reinterpret_cast<UNSIGNED8*>(tag), &tlen);
        std::printf("  order %u tag='%s'\n", ord, tag);
    }

    std::printf("\nNatural order (first 8 NAME values):\n");
    // Clear active index order (natural recno walk).
    UNSIGNED8 empty_tag[] = "";
    AdsSetIndexOrder(hT, empty_tag);
    auto natural = walk_names(hT, 8);

    ADSHANDLE hIx2 = 0;
    AdsGetIndexHandleByOrder(hT, 2, &hIx2);
    AdsSetIndexOrderByHandle(hT, hIx2);
    std::printf("\nTag 2 order (first 8 NAME values — should be A..Z):\n");
    auto ordered = walk_names(hT, 8);

    if (!names_sorted(ordered))
        die("tag-2 walk is NOT sorted by NAME");

    if (ordered == natural)
        std::printf("\nWARN: ordered walk identical to natural (index may not reorder)\n");
    else
        std::printf("\nOK: index order differs from natural — CDX navigation works\n");

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    std::printf("PASS\n");
    return 0;
}