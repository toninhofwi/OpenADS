<?php
/**
 * api/save_trigger.php — save trigger property changes back to the DD.
 *
 * POST { dd, name, event, timing, enabled, body }
 *   event:   1=INSERT  2=UPDATE  3=DELETE
 *   timing:  1=BEFORE  2=INSTEAD OF  4=AFTER
 *   enabled: "Yes"/"No"
 *   body:    SQL text
 *
 * Uses AdsDDSetTriggerProperty:
 *   502 = ADS_DD_TRIGGER_EVENT      (event type string "1"/"2"/"3")
 *   1402 = timing                    (timing string "1"/"2"/"4")
 *   503 = ADS_DD_TRIGGER_CONTAINER  (body text)
 *   505 = ADS_DD_TRIGGER_ENABLED    ("Yes"/"No")
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName = trim($body['dd']      ?? '');
$name   = trim($body['name']    ?? '');
$event  = trim($body['event']   ?? '');   // "1" / "2" / "3"
$timing = trim($body['timing']  ?? '');   // "1" / "2" / "4"
$enabled = trim($body['enabled'] ?? 'Yes');
$sql    = $body['body']  ?? '';           // trigger body SQL

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

// Convert display labels to raw numeric codes
function eventCode(string $s): string {
    return match(strtoupper(trim($s))) {
        'INSERT', '1' => '1',
        'UPDATE', '2' => '2',
        'DELETE', '3' => '3',
        default => $s,
    };
}
function timingCode(string $s): string {
    return match(strtoupper(trim($s))) {
        'BEFORE', '1'         => '1',
        'INSTEAD OF', 'INSTEAD', '2' => '2',
        'AFTER', '4'          => '4',
        default => $s,
    };
}

try {
    $conn = AdsConnection::connect($opts);
    $dict = AdsDictionary::fromConnection($conn);
    $saved = 0;

    // ADS_DD_TRIGGER_EVENT (502) — event type: 1=INSERT 2=UPDATE 3=DELETE
    if ($event !== '') {
        $dict->setTriggerProperty($name, 502, eventCode($event));
        $saved++;
    }

    // Code 1402 — timing: 1=BEFORE 2=INSTEAD_OF 4=AFTER
    if ($timing !== '') {
        $dict->setTriggerProperty($name, 1402, timingCode($timing));
        $saved++;
    }

    // ADS_DD_TRIGGER_ENABLED (505) — pass "Yes"/"No" string
    $dict->setTriggerProperty($name, 505, $enabled);
    $saved++;

    // ADS_DD_TRIGGER_CONTAINER (503) — SQL body
    if ($sql !== '') {
        $dict->setTriggerProperty($name, 503, $sql);
        $saved++;
    }

    $conn->close();
    echo json_encode(['saved' => $saved]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
