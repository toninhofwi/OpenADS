<?php
/**
 * api/save_group_members.php — update the members of a DD group.
 * POST { dd, group, members: ["User1", "User2"] }
 *
 * Diffs vs current membership and calls
 * sp_AddUserToGroup / sp_RemoveUserFromGroup for each change.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$body       = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName     = trim($body['dd']    ?? '');
$groupName  = trim($body['group'] ?? '');
$newMembers = array_map('trim', $body['members'] ?? []);
$newMembers = array_unique(array_filter($newMembers, fn($u) => $u !== ''));

if ($groupName === '') {
    api_error(400, 'group is required');
}
if (str_contains($groupName, "\0")) {
    api_error(400, 'invalid group name');
}
foreach ($newMembers as $u) {
    if (str_contains($u, "\0")) {
        api_error(400, 'invalid user name');
    }
}

$c = api_require_connection($ddName);
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

try {
    $conn = AdsConnection::connect($opts);

    $qGroup = api_sql_quote($groupName);

    // Current members
    $current = [];
    $stmt = $conn->query(
        "SELECT USER_NAME FROM system.usergroupmembers
          WHERE GROUP_NAME = '$qGroup'
          ORDER BY USER_NAME"
    );
    while ($row = $stmt->fetchAssoc()) {
        $u = trim((string)($row['USER_NAME'] ?? ''));
        if ($u !== '') $current[] = $u;
    }
    $stmt->close();

    $currentSet = array_map('strtoupper', $current);
    $newSet     = array_map('strtoupper', $newMembers);

    $toAdd    = array_diff($newSet, $currentSet);
    $toRemove = array_diff($currentSet, $newSet);

    // Map uppercase back to original case
    $newMap     = array_combine($newSet,     $newMembers);
    $currentMap = array_combine($currentSet, $current);

    $errs = [];
    foreach ($toAdd as $u) {
        $uname = $newMap[$u] ?? $u;
        try {
            $conn->execute("EXECUTE PROCEDURE sp_AddUserToGroup('"
                . api_sql_quote($uname) . "', '$qGroup')");
        } catch (Throwable $e) { $errs[] = "Add $uname: " . $e->getMessage(); }
    }
    foreach ($toRemove as $u) {
        $uname = $currentMap[$u] ?? $u;
        try {
            $conn->execute("EXECUTE PROCEDURE sp_RemoveUserFromGroup('"
                . api_sql_quote($uname) . "', '$qGroup')");
        } catch (Throwable $e) { $errs[] = "Remove $uname: " . $e->getMessage(); }
    }

    $conn->close();
    echo json_encode([
        'saved'   => true,
        'added'   => count($toAdd),
        'removed' => count($toRemove),
        'errors'  => $errs,
    ]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
