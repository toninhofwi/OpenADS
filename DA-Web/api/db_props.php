<?php
/**
 * api/db_props.php — get or set DD-level database properties.
 *
 * GET  ?dd=   → JSON object with all readable properties
 * POST {...}  → { saved: N }
 *
 * Property IDs (from openads/ace.h):
 *   ADS_DD_COMMENT             =  1  (string)
 *   ADS_DD_DEFAULT_TABLE_PATH  =  3  (string)
 *   ADS_DD_TEMP_TABLE_PATH     =  4  (string)
 *   ADS_DD_LOG_IN_REQUIRED     =  5  (UNSIGNED16 bool)
 *   ADS_DD_VERIFY_ACCESS_RIGHTS=  6  (UNSIGNED16 bool)
 *   ADS_DD_USER_DEFINED_PROP   = 22  (string)
 *   ADS_DD_VERSION_MAJOR       = 24  (UNSIGNED16)
 *   ADS_DD_VERSION_MINOR       = 25  (UNSIGNED16)
 *
 * UNSIGNED16 bool properties are stored as 2-byte little-endian (pack('v',...))
 * to stay compatible with values imported from SAP ACE binary .add files.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';
require_once __DIR__ . '/openads_stubs.php';

$isPost = $_SERVER['REQUEST_METHOD'] === 'POST';
if ($isPost) {
    $body   = json_decode(file_get_contents('php://input'), true) ?? [];
    $ddName = trim($body['dd'] ?? '');
} else {
    $body   = [];
    $ddName = trim($_GET['dd'] ?? '');
}

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

/**
 * Read a string property; return '' on error or not-set.
 */
function readStr(AdsDictionary $dict, int $prop): string {
    try { return (string)$dict->getDatabaseProperty($prop); }
    catch (Throwable) { return ''; }
}

/**
 * Read a UNSIGNED16 property stored as a plain decimal string ("0", "1", "3", …).
 * The import tool decodes SAP's raw little-endian bytes to decimal; the UI also
 * writes decimal strings.  A legacy raw 2-byte value is handled as a fallback.
 */
function readU16(AdsDictionary $dict, int $prop): int {
    try {
        $raw = $dict->getDatabaseProperty($prop);
        if ($raw === '') return 0;
        // Normal case: plain decimal string written by import tool or our UI.
        if (ctype_digit($raw) || ($raw[0] === '-' && ctype_digit(substr($raw, 1))))
            return (int)$raw;
        // Fallback: raw 2-byte little-endian left over from an old import.
        if (strlen($raw) >= 2)
            return unpack('v', substr($raw, 0, 2))[1];
        return (int)$raw;
    } catch (Throwable) { return 0; }
}

/**
 * Write a UNSIGNED16 int as a plain decimal string.
 * This survives the JSON escape round-trip in the OpenADS data_dict layer.
 */
function writeU16(AdsDictionary $dict, int $prop, int $val): void {
    $dict->setDatabaseProperty($prop, (string)max(0, min(65535, $val)));
}

try {
    $conn = AdsConnection::connect($opts);
    $dict = AdsDictionary::fromConnection($conn);

    if ($isPost) {
        $saved = 0;

        // Guard: refuse to enable loginRequired if no user has a stored password.
        // Without a password the DD becomes immediately inaccessible after save
        // (every subsequent AdsConnect60 call would return AE_LOGIN_FAILED).
        $wantLogin = !empty($body['loginRequired']);
        if ($wantLogin) {
            $currentLoginReq = readU16($dict, 5) !== 0;
            // Only need to check when we're ENABLING login (not when it's already on).
            if (!$currentLoginReq) {
                // Query system.users for any user that has a non-empty password
                // stored in prop_1101 (the user property key for passwords).
                // We use getUserProperty(username, 1101) on each user found in
                // system.users.  If none have a password we block the save.
                $hasPassword = false;
                try {
                    $stmt = $conn->query("SELECT user_name FROM system.users");
                    $rows = $stmt->fetchAll();
                    $stmt->close();
                    foreach ($rows as $row) {
                        $uname = $row['user_name'] ?? ($row['USER_NAME'] ?? '');
                        if ($uname === '') continue;
                        try {
                            $pwd = $dict->getUserProperty($uname, 1101);
                            if ($pwd !== '') { $hasPassword = true; break; }
                        } catch (Throwable) {}
                    }
                } catch (Throwable) {}

                if (!$hasPassword) {
                    $conn->close();
                    http_response_code(409);
                    echo json_encode([
                        'error' => 'Cannot enable Login Required: no users have a '
                                 . 'password stored in this dictionary.  Set a '
                                 . 'password for at least one user first, then '
                                 . 're-enable Login Required.',
                        'code'  => 5000,
                    ]);
                    exit;
                }
            }
        }

        // String properties
        try {
            $dict->setDatabaseProperty(1, (string)($body['description'] ?? ''));
            $saved++;
        } catch (Throwable) {}

        try {
            $dict->setDatabaseProperty(3, (string)($body['defaultTablePath'] ?? ''));
            $saved++;
        } catch (Throwable) {}

        try {
            $dict->setDatabaseProperty(4, (string)($body['tempTablePath'] ?? ''));
            $saved++;
        } catch (Throwable) {}

        try {
            $dict->setDatabaseProperty(22, (string)($body['userDefinedProp'] ?? ''));
            $saved++;
        } catch (Throwable) {}

        // UNSIGNED16 bool properties
        try {
            writeU16($dict, 5, $wantLogin ? 1 : 0);
            $saved++;
        } catch (Throwable) {}

        try {
            writeU16($dict, 6, empty($body['verifyAccessRights']) ? 0 : 1);
            $saved++;
        } catch (Throwable) {}

        // UNSIGNED16 version numbers
        try {
            writeU16($dict, 24, (int)($body['versionMajor'] ?? 0));
            $saved++;
        } catch (Throwable) {}

        try {
            writeU16($dict, 25, (int)($body['versionMinor'] ?? 0));
            $saved++;
        } catch (Throwable) {}

        $conn->close();
        echo json_encode(['saved' => $saved]);

    } else {
        $result = [
            'description'        => readStr($dict, 1),
            'defaultTablePath'   => readStr($dict, 3),
            'tempTablePath'      => readStr($dict, 4),
            'loginRequired'      => readU16($dict, 5) !== 0,
            'verifyAccessRights' => readU16($dict, 6) !== 0,
            'userDefinedProp'    => readStr($dict, 22),
            'versionMajor'       => readU16($dict, 24),
            'versionMinor'       => readU16($dict, 25),
        ];
        $conn->close();
        echo json_encode($result);
    }
} catch (Throwable $e) {
    api_exception(500, $e);
}
