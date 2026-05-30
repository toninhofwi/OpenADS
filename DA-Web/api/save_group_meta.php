<?php
/**
 * api/save_group_meta.php — save permission changes for a DD group.
 * POST { dd, group, rows: [{ object, canSelect, canInsert, canUpdate, canDelete, canExec }] }
 *
 * Builds and executes GRANT/REVOKE statements for each changed row.
 */
header('Content-Type: application/json');
session_start();

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName = trim($body['dd']    ?? '');
$group  = trim($body['group'] ?? '');
$rows   = $body['rows'] ?? [];

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '' || $group === '' || !is_array($rows)) {
    http_response_code(400);
    echo json_encode(['error' => 'dd, group and rows are required']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

function perm_flag(mixed $v): bool {
    return strcasecmp((string)($v ?? ''), 'Yes') === 0 || (string)($v ?? '') === '1';
}

try {
    $conn  = AdsConnection::connect($opts);
    $saved = 0;
    $errs  = [];

    foreach ($rows as $row) {
        $obj = trim($row['object'] ?? '');
        if ($obj === '') continue;

        // Build GRANT list
        $grants  = [];
        $revokes = [];

        $permMap = [
            'SELECT'  => 'canSelect',
            'INSERT'  => 'canInsert',
            'UPDATE'  => 'canUpdate',
            'DELETE'  => 'canDelete',
            'EXECUTE' => 'canExec',
        ];

        foreach ($permMap as $perm => $field) {
            if (perm_flag($row[$field] ?? '')) {
                $grants[] = $perm;
            } else {
                $revokes[] = $perm;
            }
        }

        if (!empty($grants)) {
            $sql = 'GRANT ' . implode(', ', $grants) . " ON \"$obj\" TO \"$group\"";
            try { $conn->execute($sql); $saved++; } catch (Throwable $e) { $errs[] = "GRANT on $obj: " . $e->getMessage(); }
        }
        if (!empty($revokes)) {
            $sql = 'REVOKE ' . implode(', ', $revokes) . " ON \"$obj\" FROM \"$group\"";
            try { $conn->execute($sql); } catch (Throwable $e) { /* ignore: revoke of ungranted perm may fail */ }
        }
    }

    $conn->close();
    echo json_encode(['saved' => $saved, 'errors' => $errs]);
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
}
