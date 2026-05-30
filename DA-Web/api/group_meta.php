<?php
/**
 * api/group_meta.php — permissions granted to a DD group.
 * GET ?dd=&group=
 *   Returns { data: [{ object, type, canSelect, canInsert, canUpdate, canDelete, canExec }] }
 */
header('Content-Type: application/json');
session_start();

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

function typeLabel(int $code): string {
    return match($code) {
        1  => 'Table',
        3  => 'Database',
        6  => 'Group',
        8  => 'User',
        10 => 'Procedure',
        12 => 'View',
        18 => 'Function',
        default => "Type$code",
    };
}

try {
    $conn = AdsConnection::connect($opts);

    // Use SELECT * to avoid quoting SQL reserved-word column names (SELECT, UPDATE, etc.)
    $stmt = $conn->query('SELECT * FROM system.permissions');
    $rows = [];
    while ($row = $stmt->fetchAssoc()) {
        if (strcasecmp($row['GRANTEE'] ?? '', $groupName) !== 0) continue;
        $rows[] = [
            'object'    => $row['OBJ_NAME'],
            'type'      => typeLabel((int)($row['OBJ_TYPE'] ?? 0)),
            'canSelect' => ($row['SELECT']  ?? '') === '1' ? 'Yes' : 'No',
            'canInsert' => ($row['INSERT']  ?? '') === '1' ? 'Yes' : 'No',
            'canUpdate' => ($row['UPDATE']  ?? '') === '1' ? 'Yes' : 'No',
            'canDelete' => ($row['DELETE']  ?? '') === '1' ? 'Yes' : 'No',
            'canExec'   => ($row['EXECUTE'] ?? '') === '1' ? 'Yes' : 'No',
        ];
    }

    usort($rows, fn($a, $b) => strcmp($a['object'], $b['object']));
    $conn->close();
    echo json_encode(['data' => $rows]);
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
}
