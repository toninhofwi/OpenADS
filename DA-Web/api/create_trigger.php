<?php
/**
 * api/create_trigger.php — create a new trigger in the DD.
 *
 * POST { dd, table, name, timing, event, priority }
 *   timing: "BEFORE" | "INSTEAD OF" | "AFTER"
 *   event:  "INSERT" | "UPDATE" | "DELETE"
 *   priority: integer (default 1)
 *
 * Combined ADS type constants:
 *   BEFORE INSERT=0x0001  AFTER INSERT=0x0002  INSTEAD OF INSERT=0x0040
 *   BEFORE UPDATE=0x0004  AFTER UPDATE=0x0008  INSTEAD OF UPDATE=0x0080
 *   BEFORE DELETE=0x0010  AFTER DELETE=0x0020  INSTEAD OF DELETE=0x0100
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$body     = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName   = trim($body['dd']     ?? '');
$table    = trim($body['table']  ?? '');
$name     = trim($body['name']   ?? '');
$timing   = strtoupper(trim($body['timing']   ?? 'BEFORE'));
$event    = strtoupper(trim($body['event']    ?? 'INSERT'));
$priority = max(1, (int)($body['priority'] ?? 1));

if ($table === '' || $name === '') {
    api_error(400, 'table and name are required');
}
api_validate_identifier($table, 'table name');
api_validate_identifier($name, 'trigger name');

// Map (timing, event) → combined ADS type constant
$typeMap = [
    'BEFORE_INSERT'     => 0x0001, 'AFTER_INSERT'     => 0x0002, 'INSTEAD OF_INSERT' => 0x0040,
    'BEFORE_UPDATE'     => 0x0004, 'AFTER_UPDATE'     => 0x0008, 'INSTEAD OF_UPDATE' => 0x0080,
    'BEFORE_DELETE'     => 0x0010, 'AFTER_DELETE'     => 0x0020, 'INSTEAD OF_DELETE' => 0x0100,
];
$typeKey = $timing . '_' . $event;
if (!isset($typeMap[$typeKey])) {
    http_response_code(400);
    echo json_encode(['error' => "Unknown timing/event combination: $typeKey"]);
    exit;
}
$type = $typeMap[$typeKey];

$c = api_require_connection($ddName);
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

try {
    $conn = AdsConnection::connect($opts);
    $dict = AdsDictionary::fromConnection($conn);
    $dict->createTrigger($name, $table, $type, '', '', $priority);
    $conn->close();
    echo json_encode(['created' => $name]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
