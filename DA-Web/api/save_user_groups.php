<?php
/**
 * api/save_user_groups.php — update a user's group memberships.
 * POST { dd, user, groups: ["Group1", "Group2"] }
 *
 * Computes the diff vs current memberships and calls
 * sp_AddUserToGroup / sp_RemoveUserFromGroup for each change.
 */
header('Content-Type: application/json');
session_start();

$body     = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName   = trim($body['dd']   ?? '');
$userName = trim($body['user'] ?? '');
$newGroups = array_map('trim', $body['groups'] ?? []);
$newGroups = array_unique(array_filter($newGroups, fn($g) => $g !== ''));

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

    // Current groups
    $current = [];
    $escapedUser = str_replace("'", "''", $userName);
    $stmt = $conn->query("SELECT GROUP_NAME FROM system.usergroupmembers WHERE USER_NAME = '$escapedUser'");
    while ($row = $stmt->fetchAssoc()) {
        $g = $row['GROUP_NAME'] ?? '';
        if ($g !== '') $current[] = $g;
    }

    // Filter out DB: built-in groups — they are managed by SAP's per-user cipher
    // and cannot be persistently changed via OpenADS (no XOR token encoding support).
    $isBuiltin   = fn($g) => strncasecmp($g, 'DB:', 3) === 0;
    $current     = array_values(array_filter($current,   fn($g) => !$isBuiltin($g)));
    $newGroups   = array_values(array_filter($newGroups, fn($g) => !$isBuiltin($g)));

    $currentSet = array_map('strtoupper', $current);
    $newSet     = array_map('strtoupper', $newGroups);

    $toAdd    = array_diff($newSet, $currentSet);
    $toRemove = array_diff($currentSet, $newSet);

    // Map uppercase back to original case for the stored procedure
    $newMap     = array_combine($newSet, $newGroups);
    $currentMap = array_combine($currentSet, $current);

    $errs = [];
    foreach ($toAdd as $g) {
        $gname = $newMap[$g] ?? $g;
        try {
            $conn->execute("EXECUTE PROCEDURE sp_AddUserToGroup('$userName', '$gname')");
        } catch (Throwable $e) { $errs[] = "Add to $gname: " . $e->getMessage(); }
    }
    foreach ($toRemove as $g) {
        $gname = $currentMap[$g] ?? $g;
        try {
            $conn->execute("EXECUTE PROCEDURE sp_RemoveUserFromGroup('$userName', '$gname')");
        } catch (Throwable $e) { $errs[] = "Remove from $gname: " . $e->getMessage(); }
    }

    $conn->close();
    echo json_encode(['saved' => true, 'added' => count($toAdd), 'removed' => count($toRemove), 'errors' => $errs]);
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
}
