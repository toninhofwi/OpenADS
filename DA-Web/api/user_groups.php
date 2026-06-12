<?php
/**
 * api/user_groups.php — list groups a DD user belongs to.
 * GET ?dd=&user=
 *
 * Returns {
 *   groups:        ["Group1", "Group2", ...],   // editable regular groups
 *   builtinGroups: ["DB:Public", ...],           // SAP built-in groups (read-only)
 *   allGroups:     ["A","B",...],                // all regular groups in DD (no DB: prefix)
 *   dbGroupNote:   string                        // explanation of DB: group limitations
 * }
 *
 * Built-in SAP groups (DB:Public, DB:Admin, DB:Backup, DB:Debug) are split out
 * into builtinGroups because:
 *   - DB:Public is hardcoded for all users (OpenADS cannot remove it).
 *   - DB:Admin/Backup/Debug are stored with a per-user cipher that OpenADS
 *     cannot decode from the binary .add file without the SAP DLL; these will
 *     not appear here even if the user is a member.
 *   - Attempting to add/remove DB: groups via OpenADS has no persistent effect.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$ddName   = trim($_GET['dd']   ?? '');
$userName = trim($_GET['user'] ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '' || $userName === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd and user are required']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

try {
    $conn = AdsConnection::connect($opts);

    // User's current groups — split DB: built-ins from regular groups
    $groups        = [];
    $builtinGroups = [];
    $escapedUser   = str_replace("'", "''", $userName);
    $stmt = $conn->query("SELECT GROUP_NAME FROM system.usergroupmembers WHERE USER_NAME = '$escapedUser'");
    while ($row = $stmt->fetchAssoc()) {
        $g = $row['GROUP_NAME'] ?? '';
        if ($g === '') continue;
        if (strncasecmp($g, 'DB:', 3) === 0) {
            $builtinGroups[] = $g;   // DB:Public, DB:Admin, DB:Backup, DB:Debug
        } else {
            $groups[] = $g;
        }
    }
    sort($groups);
    sort($builtinGroups);

    // All regular groups in the DD (DB: groups have no Group record → not listed here)
    $allGroups = [];
    $stmt3 = $conn->query("SELECT GROUP_NAME FROM system.usergroups ORDER BY GROUP_NAME");
    while ($row = $stmt3->fetchAssoc()) {
        $allGroups[] = $row['GROUP_NAME'];
    }

    $conn->close();
    echo json_encode([
        'groups'        => $groups,
        'builtinGroups' => $builtinGroups,
        'allGroups'     => $allGroups,
        'dbGroupNote'   => 'DB:Admin, DB:Backup and DB:Debug memberships are encoded with a '
                         . 'per-user cipher in the binary .add file that OpenADS cannot decode '
                         . 'without the SAP DLL. They will not appear here even if the user '
                         . 'is a member. DB:Public is always shown (all users are members).',
    ]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
