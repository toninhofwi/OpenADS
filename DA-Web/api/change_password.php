<?php
/**
 * api/change_password.php — set a DD user's password
 * POST { dd, user, password }
 *
 * Executes sp_ModifyUserProperty to store the new password.
 * Pass an empty password string to clear it (user can then connect with no password).
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$body     = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName   = trim($body['dd']       ?? '');
$user     = trim($body['user']     ?? '');
$password = $body['password']      ?? '';   // may be empty string (clears password)

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

$c    = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

try {
    $conn = AdsConnection::connect($opts);
    $qu   = str_replace("'", "''", $user);
    $qp   = str_replace("'", "''", $password);
    $conn->execute("EXECUTE PROCEDURE sp_ModifyUserProperty('$qu', 'USER_PASSWORD', '$qp')");
    $conn->close();
    echo json_encode(['ok' => true]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
