<?php
/**
 * api/user_groups.php — list groups a DD user belongs to.
 * GET ?dd=&user=
 *   Returns { groups: ["Group1", "Group2", ...], allGroups: ["A","B",...] }
 */
header('Content-Type: application/json');
session_start();

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

    // User's current groups
    $groups = [];
    $stmt = $conn->query("SELECT GROUP_NAME FROM system.usergroupmembers");
    while ($row = $stmt->fetchAssoc()) {
        $user = $row['USER_NAME'] ?? '';
        // column order may vary; re-fetch with both columns
    }
    $stmt2 = $conn->query("SELECT GROUP_NAME, USER_NAME FROM system.usergroupmembers");
    while ($row = $stmt2->fetchAssoc()) {
        if (strcasecmp($row['USER_NAME'] ?? '', $userName) === 0) {
            $groups[] = $row['GROUP_NAME'];
        }
    }
    sort($groups);

    // All groups in the DD
    $allGroups = [];
    $stmt3 = $conn->query("SELECT GROUP_NAME FROM system.usergroups ORDER BY GROUP_NAME");
    while ($row = $stmt3->fetchAssoc()) {
        $allGroups[] = $row['GROUP_NAME'];
    }

    $conn->close();
    echo json_encode(['groups' => $groups, 'allGroups' => $allGroups]);
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
}
