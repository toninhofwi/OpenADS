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

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '' || $name === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd and name are required']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
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
