<?php
/**
 * api/save_trigger.php — save trigger property changes back to the DD.
 *
 * POST { dd, name, event, timing, enabled, body, options }
 *   event:   1=INSERT  2=UPDATE  3=DELETE
 *   timing:  1=BEFORE  2=INSTEAD OF  4=AFTER
 *   enabled: "Yes"/"No"
 *   body:    SQL text
 *   options: bitmask — 0x01=WANT_VALUES 0x02=WANT_MEMOS_AND_BLOBS 0x04=NO_TRANSACTION
 *
 * Uses AdsDDSetTriggerProperty:
 *   502 = ADS_DD_TRIGGER_EVENT      (event type string "1"/"2"/"3")
 *   1402 = timing                    (timing string "1"/"2"/"4")
 *   503 = ADS_DD_TRIGGER_CONTAINER  (body text)
 *   505 = ADS_DD_TRIGGER_ENABLED    ("Yes"/"No")
 *   1407 = ADS_DD_TRIG_OPTIONS      (options bitmask string)
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName  = trim($body['dd']      ?? '');
$name    = trim($body['name']    ?? '');
$table   = trim($body['table']   ?? '');   // parent table — used for disambiguation
$event    = trim($body['event']    ?? '');   // "1" / "2" / "3"
$timing   = trim($body['timing']   ?? '');   // "1" / "2" / "4"
$enabled  = trim($body['enabled']  ?? 'Yes');
$priority = isset($body['priority']) ? (int)$body['priority'] : null;
$sql      = $body['body']    ?? '';          // trigger body SQL
$options  = isset($body['options']) ? (int)$body['options'] : null; // bitmask or null = don't update

// Build composite key for disambiguation when same trigger name exists on multiple tables.
$trigKey = ($table !== '') ? "$table::$name" : $name;

if ($name === '') {
    api_error(400, 'name is required');
}
if ($table !== '') {
    api_validate_identifier($table, 'table name');
}

$c = api_require_connection($ddName);
$opts = api_ads_connect_opts($c);

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
        $dict->setTriggerProperty($trigKey, 502, eventCode($event));
        $saved++;
    }

    // Code 1402 — timing: 1=BEFORE 2=INSTEAD_OF 4=AFTER
    if ($timing !== '') {
        $dict->setTriggerProperty($trigKey, 1402, timingCode($timing));
        $saved++;
    }

    // ADS_DD_TRIGGER_ENABLED (505) — pass "Yes"/"No" string
    $dict->setTriggerProperty($trigKey, 505, $enabled);
    $saved++;

    // ADS_DD_TRIGGER_PRIORITY (506)
    if ($priority !== null) {
        $dict->setTriggerProperty($trigKey, 506, (string)$priority);
        $saved++;
    }

    // ADS_DD_TRIGGER_CONTAINER (503) — SQL body
    if ($sql !== '') {
        $dict->setTriggerProperty($trigKey, 503, $sql);
        $saved++;
    }

    // Code 1407 — trigger options bitmask
    if ($options !== null) {
        $dict->setTriggerProperty($trigKey, 1407, (string)$options);
        $saved++;
    }

    $conn->close();
    echo json_encode(['saved' => $saved]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
