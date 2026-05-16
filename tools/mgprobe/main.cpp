// openads_mgprobe — command-line probe for the AdsMg* management
// telemetry surface. Connects to an OpenADS server (or local mode)
// and prints the same activity / install / comm figures Harbour's
// rddads `manage.prg` sample displays. Used to verify the telemetry
// subsystem end-to-end against a live server.
//
//   openads_mgprobe                 -> local-mode telemetry
//   openads_mgprobe host:port       -> remote server telemetry
//
#include "openads/ace.h"

#include <cstdio>
#include <cstring>

extern "C" {
UNSIGNED32 AdsMgConnect(UNSIGNED8*, UNSIGNED8*, UNSIGNED8*, ADSHANDLE*);
UNSIGNED32 AdsMgDisconnect(ADSHANDLE);
UNSIGNED32 AdsMgGetServerType(ADSHANDLE, UNSIGNED16*);
UNSIGNED32 AdsMgGetInstallInfo(ADSHANDLE, void*, UNSIGNED16*);
UNSIGNED32 AdsMgGetActivityInfo(ADSHANDLE, void*, UNSIGNED16*);
UNSIGNED32 AdsMgGetCommStats(ADSHANDLE, void*, UNSIGNED16*);
UNSIGNED32 AdsMgGetConfigInfo(ADSHANDLE, void*, UNSIGNED16*,
                              void*, UNSIGNED16*);
UNSIGNED32 AdsMgGetUserNames(ADSHANDLE, UNSIGNED8*, void*,
                             UNSIGNED16*, UNSIGNED16*);
}

int main(int argc, char** argv) {
    const char* srv = (argc > 1) ? argv[1] : "local";

    UNSIGNED8 server[256];
    std::strncpy(reinterpret_cast<char*>(server), srv, sizeof(server) - 1);
    server[sizeof(server) - 1] = 0;
    UNSIGNED8 user[2] = "u";
    UNSIGNED8 pwd[2]  = "p";

    ADSHANDLE h = 0;
    UNSIGNED32 rc = AdsMgConnect(server, user, pwd, &h);
    std::printf("AdsMgConnect(\"%s\") -> rc=%u  handle=0x%llX\n",
                srv, rc, static_cast<unsigned long long>(h));
    if (rc != 0) {
        std::printf("  connect failed (server unreachable?)\n");
        return 1;
    }

    UNSIGNED16 stype = 0xFFFF;
    if (AdsMgGetServerType(h, &stype) == 0)
        std::printf("ServerType    : %u (%s)\n", stype,
                    stype == 0 ? "local" : "remote");

    ADS_MGMT_INSTALL_INFO inst;
    UNSIGNED16 sz = sizeof(inst);
    if (AdsMgGetInstallInfo(h, &inst, &sz) == 0)
        std::printf("InstallInfo   : version='%s' owner='%s'\n",
                    inst.aucVersionStr, inst.aucRegisteredOwner);

    ADS_MGMT_ACTIVITY_INFO act;
    sz = sizeof(act);
    if (AdsMgGetActivityInfo(h, &act, &sz) == 0) {
        std::printf("Up Time       : %u d %u h %u m %u s\n",
                    act.stUpTime.usDays, act.stUpTime.usHours,
                    act.stUpTime.usMinutes, act.stUpTime.usSeconds);
        std::printf("Operations    : %u    LoggedErrors: %u\n",
                    act.ulOperations, act.ulLoggedErrors);
        std::printf("Users         : InUse=%u  MaxUsed=%u\n",
                    act.stUsers.ulInUse, act.stUsers.ulMaxUsed);
        std::printf("Connections   : InUse=%u  MaxUsed=%u\n",
                    act.stConnections.ulInUse,
                    act.stConnections.ulMaxUsed);
        std::printf("WorkAreas     : InUse=%u\n",
                    act.stWorkAreas.ulInUse);
        std::printf("Tables        : InUse=%u\n",
                    act.stTables.ulInUse);
        std::printf("Indexes       : InUse=%u\n",
                    act.stIndexes.ulInUse);
        std::printf("Locks         : InUse=%u\n",
                    act.stLocks.ulInUse);
        std::printf("WorkerThreads : InUse=%u\n",
                    act.stWorkerThreads.ulInUse);
    }

    ADS_MGMT_COMM_STATS comm;
    sz = sizeof(comm);
    if (AdsMgGetCommStats(h, &comm, &sz) == 0)
        std::printf("CommStats     : TotalPackets=%u  Disconnects=%u  "
                    "PartialConnects=%u\n",
                    comm.ulTotalPackets, comm.ulDisconnectedUsers,
                    comm.ulPartialConnects);

    ADS_MGMT_CONFIG_PARAMS cfgp;
    ADS_MGMT_CONFIG_MEMORY cfgm;
    UNSIGNED16 szp = sizeof(cfgp);
    UNSIGNED16 szm = sizeof(cfgm);
    if (AdsMgGetConfigInfo(h, &cfgp, &szp, &cfgm, &szm) == 0)
        std::printf("Config        : SendPort=%u  RecvPort=%u  "
                    "TotalMem=%.0f bytes\n",
                    cfgp.usSendIPPort, cfgp.usReceiveIPPort,
                    cfgm.ulTotalConfigMem);

    ADS_MGMT_USER_INFO users[32];
    UNSIGNED16 cnt  = 32;
    UNSIGNED16 usz  = sizeof(ADS_MGMT_USER_INFO);
    if (AdsMgGetUserNames(h, nullptr, users, &cnt, &usz) == 0) {
        std::printf("Connected     : %u user(s)\n", cnt);
        UNSIGNED16 shown = cnt < 32 ? cnt : 32;
        for (UNSIGNED16 i = 0; i < shown; ++i)
            std::printf("  [%u] %s @ %s\n", users[i].usConnNumber,
                        users[i].aucUserName, users[i].aucAddress);
    }

    AdsMgDisconnect(h);
    return 0;
}
