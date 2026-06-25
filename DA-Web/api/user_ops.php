<?php
/**
 * api/user_ops.php — create or delete a DD user.
 * POST { action: 'create'|'delete', dd, user, password? }
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$action = trim($body['action'] ?? '');
$ddName = trim($body['dd']     ?? '');
$user   = trim($body['user']   ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '' || $user === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd and user are required']);
    exit;
}
if (!in_array($action, ['create', 'delete'], true)) {
    http_response_code(400);
    echo json_encode(['error' => 'action must be create or delete']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

try {
    $conn = AdsConnection::connect($opts);
    $qu   = str_replace("'", "''", $user);

    if ($action === 'delete') {
        $conn->execute("EXECUTE PROCEDURE sp_DropUser('$qu')");
        $conn->close();
        echo json_encode(['deleted' => true]);
    } else {
        $password = (string)($body['password'] ?? '');
        $qp       = str_replace("'", "''", $password);
        $conn->execute("EXECUTE PROCEDURE sp_CreateUser('$qu', '$qp')");
        $conn->close();
        echo json_encode(['created' => true]);
    }
} catch (Throwable $e) {
    api_exception(500, $e);
}
