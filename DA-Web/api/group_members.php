<?php
/**
 * api/group_members.php — list members of a DD group.
 * GET ?dd=&group=
 *
 * Returns {
 *   members:   ["User1", "User2", …],   // current members of this group
 *   allUsers:  ["User1", "User3", …],   // every user in the DD (for the add picker)
 * }
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$ddName    = trim($_GET['dd']    ?? '');
$groupName = trim($_GET['group'] ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '' || $groupName === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd and group are required']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

try {
    $conn = AdsConnection::connect($opts);

    $escapedGroup = str_replace("'", "''", $groupName);

    $members = [];
    $stmt = $conn->query(
        "SELECT USER_NAME FROM system.usergroupmembers
          WHERE GROUP_NAME = '$escapedGroup'
          ORDER BY USER_NAME"
    );
    while ($row = $stmt->fetchAssoc()) {
        $u = trim((string)($row['USER_NAME'] ?? ''));
        if ($u !== '') $members[] = $u;
    }
    $stmt->close();

    $allUsers = [];
    $stmt = $conn->query('SELECT USER_NAME FROM system.users ORDER BY USER_NAME');
    while ($row = $stmt->fetchAssoc()) {
        $u = trim((string)($row['USER_NAME'] ?? ''));
        if ($u !== '') $allUsers[] = $u;
    }
    $stmt->close();

    $conn->close();
    echo json_encode(['members' => $members, 'allUsers' => $allUsers]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
