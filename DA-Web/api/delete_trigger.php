<?php
/**
 * api/delete_trigger.php — delete a trigger from the DD.
 *
 * POST { dd, table, name }
 *   table: used to form composite key "table::name" for unambiguous resolution.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName = trim($body['dd']    ?? '');
$table  = trim($body['table'] ?? '');
$name   = trim($body['name']  ?? '');

if ($name === '') {
    api_error(400, 'name is required');
}
if ($table !== '') {
    api_validate_identifier($table, 'table name');
}

$c = api_require_connection($ddName);
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

// Use composite key for unambiguous resolution when table is known
$trigKey = ($table !== '') ? "$table::$name" : $name;

try {
    $conn = AdsConnection::connect($opts);
    $dict = AdsDictionary::fromConnection($conn);
    $dict->dropTrigger($trigKey);
    $conn->close();
    echo json_encode(['deleted' => $name]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
