<?php
/**
 * api/server_info.php — server activity / connection info via AdsMg* API.
 *
 * GET  ?dd=<name>
 *   Returns:
 *     { ok:true, users:[...], tables:[...], activity:{...} }
 *
 * Calls AdsMgConnect + AdsMgGetActivityInfo + AdsMgGetUserNames +
 * AdsMgGetOpenTables via PHP FFI against the local openace64.dll.
 *
 * Requires: php.ini  ffi.enable=true  (or ffi.enable=preload)
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

api_require_session();

$ddName = trim($_GET['dd'] ?? '');
if ($ddName === '' || !isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}

// ── Locate openace64.dll ──────────────────────────────────────────────────────
if (!extension_loaded('ffi')) {
    http_response_code(501);
    echo json_encode(['error' => 'PHP FFI extension is not available. Enable ffi.enable=true in php.ini.']);
    exit;
}

$dllName  = (PHP_OS_FAMILY === 'Windows') ? 'openace64.dll' : 'libopenace64.so';
$dllPaths = [
    getenv('OPENADS_DLL') ?: null,
    'C:\\php\\' . $dllName,
    'C:\\php\\ext\\' . $dllName,
    '/usr/lib/' . $dllName,
    '/usr/local/lib/' . $dllName,
];
$dllPath = null;
foreach ($dllPaths as $p) {
    if ($p && is_file($p)) { $dllPath = $p; break; }
}
if (!$dllPath) {
    http_response_code(500);
    echo json_encode(['error' => 'openace64.dll not found. Set OPENADS_DLL env var or copy to C:\\php\\']);
    exit;
}

// ── FFI declarations ─────────────────────────────────────────────────────────
$cdef = '
typedef unsigned long long ADSHANDLE;
typedef unsigned short UNSIGNED16;
typedef unsigned int   UNSIGNED32;
typedef unsigned char  UNSIGNED8;

typedef struct {
    UNSIGNED16 usDays;
    UNSIGNED16 usHours;
    UNSIGNED16 usMinutes;
    UNSIGNED16 usSeconds;
} ADS_MGMT_TIME_STRUCT;

typedef struct {
    UNSIGNED32 ulInUse;
    UNSIGNED32 ulMaxUsed;
    UNSIGNED32 ulRejected;
} ADS_MGMT_USAGE_STRUCT;

typedef struct {
    UNSIGNED32            ulOperations;
    UNSIGNED32            ulLoggedErrors;
    ADS_MGMT_TIME_STRUCT  stUpTime;
    ADS_MGMT_USAGE_STRUCT stUsers;
    ADS_MGMT_USAGE_STRUCT stConnections;
    ADS_MGMT_USAGE_STRUCT stWorkAreas;
    ADS_MGMT_USAGE_STRUCT stTables;
    ADS_MGMT_USAGE_STRUCT stIndexes;
    ADS_MGMT_USAGE_STRUCT stLocks;
    ADS_MGMT_USAGE_STRUCT stTpsHeaderElems;
    ADS_MGMT_USAGE_STRUCT stTpsVisElems;
    ADS_MGMT_USAGE_STRUCT stTpsMemoElems;
    ADS_MGMT_USAGE_STRUCT stWorkerThreads;
} ADS_MGMT_ACTIVITY_INFO;

typedef struct {
    UNSIGNED8  aucUserName        [32];
    UNSIGNED16 usConnNumber;
    UNSIGNED8  aucAddress         [64];
    UNSIGNED8  aucTSAddress       [64];
    UNSIGNED8  aucOSUserLoginName [64];
    UNSIGNED8  aucAuthUserName    [64];
} ADS_MGMT_USER_INFO;

typedef struct {
    UNSIGNED8  aucTableName [256];
    UNSIGNED8  aucUserName  [32];
    UNSIGNED16 usConnNumber;
    UNSIGNED16 usOpenMode;
    UNSIGNED16 usLockType;
} ADS_MGMT_TABLE_INFO;

UNSIGNED32 AdsMgConnect(UNSIGNED8* pucServer, UNSIGNED8* pucUser,
                        UNSIGNED8* pucPassword, ADSHANDLE* phMgmt);
UNSIGNED32 AdsMgDisconnect(ADSHANDLE hMgmt);
UNSIGNED32 AdsMgGetActivityInfo(ADSHANDLE hMgmt, ADS_MGMT_ACTIVITY_INFO* pstActivity,
                                UNSIGNED16* pusStructSize);
UNSIGNED32 AdsMgGetUserNames(ADSHANDLE hMgmt, UNSIGNED8* pucFileName,
                             ADS_MGMT_USER_INFO* pstUserInfo,
                             UNSIGNED16* pusArrayLen, UNSIGNED16* pusStructSize);
UNSIGNED32 AdsMgGetOpenTables(ADSHANDLE hMgmt, UNSIGNED8* pucUserName,
                              UNSIGNED16 usConnNumber,
                              ADS_MGMT_TABLE_INFO* pstTableInfo,
                              UNSIGNED16* pusArrayLen, UNSIGNED16* pusStructSize);
';

try {
    $ffi = FFI::cdef($cdef, $dllPath);
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => 'FFI init failed: ' . $e->getMessage()]);
    exit;
}

// Read a null-terminated C UNSIGNED8 array as a PHP string.
function ffiStr(FFI\CData $arr, int $maxLen): string {
    $bytes = [];
    for ($i = 0; $i < $maxLen; $i++) {
        $b = (int)$arr[$i];
        if ($b === 0) break;
        $bytes[] = $b;
    }
    return empty($bytes) ? '' : pack('C*', ...$bytes);
}

// ── Connect to management interface ──────────────────────────────────────────
// Empty server string → local-mode management (reports this process's state).
// For a remote DD hosted on a TCP server, we would extract the host from the
// path; for now only local-mode is supported.
$hMgmt = $ffi->new('ADSHANDLE');
$hMgmt->cdata = 0;
$rc = $ffi->AdsMgConnect(null, null, null, FFI::addr($hMgmt));
if ($rc !== 0) {
    http_response_code(500);
    echo json_encode(['error' => "AdsMgConnect failed (rc=$rc)"]);
    exit;
}
$h = $hMgmt->cdata;

try {
    // ── Activity info (counts) ────────────────────────────────────────────────
    $actInfo  = $ffi->new('ADS_MGMT_ACTIVITY_INFO');
    $actSize  = $ffi->new('UNSIGNED16');
    $actSize->cdata = (int)FFI::sizeof($actInfo);
    $ffi->AdsMgGetActivityInfo($h, FFI::addr($actInfo), FFI::addr($actSize));

    $activity = [
        'operations'    => (int)$actInfo->ulOperations,
        'loggedErrors'  => (int)$actInfo->ulLoggedErrors,
        'upTime'        => [
            'days'    => (int)$actInfo->stUpTime->usDays,
            'hours'   => (int)$actInfo->stUpTime->usHours,
            'minutes' => (int)$actInfo->stUpTime->usMinutes,
            'seconds' => (int)$actInfo->stUpTime->usSeconds,
        ],
        'users'         => ['inUse' => (int)$actInfo->stUsers->ulInUse,    'maxUsed' => (int)$actInfo->stUsers->ulMaxUsed],
        'connections'   => ['inUse' => (int)$actInfo->stConnections->ulInUse, 'maxUsed' => (int)$actInfo->stConnections->ulMaxUsed],
        'tables'        => ['inUse' => (int)$actInfo->stTables->ulInUse,   'maxUsed' => (int)$actInfo->stTables->ulMaxUsed],
        'indexes'       => ['inUse' => (int)$actInfo->stIndexes->ulInUse,  'maxUsed' => (int)$actInfo->stIndexes->ulMaxUsed],
        'locks'         => ['inUse' => (int)$actInfo->stLocks->ulInUse,    'maxUsed' => (int)$actInfo->stLocks->ulMaxUsed],
        'workerThreads' => ['inUse' => (int)$actInfo->stWorkerThreads->ulInUse, 'maxUsed' => (int)$actInfo->stWorkerThreads->ulMaxUsed],
    ];

    // ── User list ─────────────────────────────────────────────────────────────
    $maxUsers  = 256;
    $userArr   = $ffi->new("ADS_MGMT_USER_INFO[$maxUsers]");
    $userCount = $ffi->new('UNSIGNED16');
    $userCount->cdata = $maxUsers;
    $userSize  = $ffi->new('UNSIGNED16');
    $userSize->cdata  = (int)FFI::sizeof($ffi->new('ADS_MGMT_USER_INFO'));
    $ffi->AdsMgGetUserNames($h, null, $userArr, FFI::addr($userCount), FFI::addr($userSize));

    $users = [];
    $n = (int)$userCount->cdata;
    for ($i = 0; $i < $n && $i < $maxUsers; $i++) {
        $u = $userArr[$i];
        $users[] = [
            'name'     => ffiStr($u->aucUserName,         32),
            'address'  => ffiStr($u->aucAddress,          64),
            'authUser' => ffiStr($u->aucAuthUserName,     64),
            'connNo'   => (int)$u->usConnNumber,
        ];
    }

    // ── Open tables ──────────────────────────────────────────────────────────
    $maxTbls  = 512;
    $tblArr   = $ffi->new("ADS_MGMT_TABLE_INFO[$maxTbls]");
    $tblCount = $ffi->new('UNSIGNED16');
    $tblCount->cdata = $maxTbls;
    $tblSize  = $ffi->new('UNSIGNED16');
    $tblSize->cdata  = (int)FFI::sizeof($ffi->new('ADS_MGMT_TABLE_INFO'));
    $ffi->AdsMgGetOpenTables($h, null, 0, $tblArr, FFI::addr($tblCount), FFI::addr($tblSize));

    $tables = [];
    $nt = (int)$tblCount->cdata;
    for ($i = 0; $i < $nt && $i < $maxTbls; $i++) {
        $t = $tblArr[$i];
        $tables[] = [
            'name'   => ffiStr($t->aucTableName, 256),
            'user'   => ffiStr($t->aucUserName,   32),
            'connNo' => (int)$t->usConnNumber,
        ];
    }

    echo json_encode([
        'ok'       => true,
        'activity' => $activity,
        'users'    => $users,
        'tables'   => $tables,
    ]);

} finally {
    $ffi->AdsMgDisconnect($h);
}
