<?php
/**
 * api/table_ops.php — create or drop a table in the DD.
 *
 * POST { action: 'create', dd, table, tableType: 'ADT'|'DBF', charType: 'OEM'|'ANSI' }
 * POST { action: 'drop',   dd, table }
 *
 * 'create' calls sp_AddTableToDatabase; the file is created in the same
 * directory as the .add file with the table name as filename.
 *
 * 'drop' calls sp_RemoveTableFromDatabase without deleting the physical file.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$action = trim($body['action'] ?? '');
$ddName = trim($body['dd']     ?? '');
$table  = trim($body['table']  ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '' || $table === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd and table are required']);
    exit;
}
if (!preg_match('/^[A-Za-z_][A-Za-z0-9_ ]*$/', $table)) {
    http_response_code(400);
    echo json_encode(['error' => 'invalid table name']);
    exit;
}
if (!in_array($action, ['create', 'drop'], true)) {
    http_response_code(400);
    echo json_encode(['error' => 'action must be create or drop']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = api_ads_connect_opts($c);

try {
    $conn = AdsConnection::connect($opts);
    $qt   = str_replace("'", "''", $table);

    if ($action === 'drop') {
        // FALSE = do not delete the physical file
        $conn->execute("EXECUTE PROCEDURE sp_RemoveTableFromDatabase('$qt', 'FALSE')");
        $conn->close();
        echo json_encode(['dropped' => true]);
    } else {
        $tableType = strtoupper(trim($body['tableType'] ?? 'ADT'));
        $charType  = strtoupper(trim($body['charType']  ?? 'ANSI'));
        // ADS table type: ADT=3, DBF=1
        $typeCode  = $tableType === 'DBF' ? 1 : 3;
        // ADS char type: OEM=1, ANSI=2
        $charCode  = $charType === 'OEM' ? 1 : 2;
        $ext       = $tableType === 'DBF' ? '.dbf' : '.adt';
        $filename  = $table . $ext;
        $qf        = str_replace("'", "''", $filename);
        $conn->execute("EXECUTE PROCEDURE sp_AddTableToDatabase('$qt', '$qf', $typeCode, $charCode)");
        $conn->close();
        echo json_encode(['created' => true]);
    }
} catch (Throwable $e) {
    api_exception(500, $e);
}
