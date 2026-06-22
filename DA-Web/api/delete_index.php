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

$c = api_require_connection($ddName);
if ($table === '' || $tag === '') {
    api_error(400, 'table and tag are required');
}
api_validate_identifier($table, 'table name');
api_validate_identifier($tag, 'index tag');

$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

try {
    $conn = AdsConnection::connect($opts);
    $conn->execute('DROP INDEX "' . $tag . '" ON "' . $table . '"');
    $conn->close();
    echo json_encode(['deleted' => $tag]);
} catch (Throwable $e) {
    api_exception(500, $e);
}