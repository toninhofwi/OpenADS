<?php
/**
 * api/delete_index.php — drop an index tag from a table.
 *
 * POST { dd, table, tag }
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName = trim($body['dd']    ?? '');
$table  = trim($body['table'] ?? '');
$tag    = trim($body['tag']   ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '' || $table === '' || $tag === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd, table and tag are required']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

try {
    $conn = AdsConnection::connect($opts);
    $conn->execute("DROP INDEX $tag ON $table");
    $conn->close();
    echo json_encode(['deleted' => $tag]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
