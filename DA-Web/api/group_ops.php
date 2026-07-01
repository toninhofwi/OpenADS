<?php
/**
 * api/group_ops.php — create or delete a DD group.
 * POST { action: 'create'|'delete', dd, group }
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$action = trim($body['action'] ?? '');
$ddName = trim($body['dd']     ?? '');
$group  = trim($body['group']  ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '' || $group === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd and group are required']);
    exit;
}
if (!in_array($action, ['create', 'delete'], true)) {
    http_response_code(400);
    echo json_encode(['error' => 'action must be create or delete']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = api_ads_connect_opts($c);

try {
    $conn = AdsConnection::connect($opts);
    $qg   = str_replace("'", "''", $group);

    if ($action === 'delete') {
        $conn->execute("EXECUTE PROCEDURE sp_DropGroup('$qg')");
        $conn->close();
        echo json_encode(['deleted' => true]);
    } else {
        $conn->execute("EXECUTE PROCEDURE sp_CreateGroup('$qg')");
        $conn->close();
        echo json_encode(['created' => true]);
    }
} catch (Throwable $e) {
    api_exception(500, $e);
}
