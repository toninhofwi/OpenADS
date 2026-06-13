<?php
/**
 * api/trigger_body.php — read trigger metadata + full SQL body via AdsDictionary API.
 *
 * POST { dd, table }
 *   Returns { triggers: [ { name, timing, event, priority, enabled, body } ] }
 *
 * Uses system.triggers virtual table for list + getTriggerProperty for full body.
 *   Property codes: 1401=event_mask(u32) 1402=timing(u32) 1404=body 1407=options(u32) 1408=table
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/openads_stubs.php';

$req    = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName = trim($req['dd']    ?? '');
$table  = trim($req['table'] ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '' || !preg_match('/^[A-Za-z_][A-Za-z0-9_ ]*$/', $table)) {
    http_response_code(400);
    echo json_encode(['error' => 'invalid parameters']);
    exit;
}

$c = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

function timing_str(int $t): string {
    return match($t) { 1 => 'BEFORE', 2 => 'INSTEAD OF', 4 => 'AFTER', default => '' };
}
function event_str(int $e): string {
    return match($e) { 1 => 'INSERT', 2 => 'UPDATE', 3 => 'DELETE', default => '' };
}

try {
    $conn = AdsConnection::connect($opts);
    $dict = AdsDictionary::fromConnection($conn);

    // Get trigger list filtered by table from system.triggers
    $stmt = $conn->query("SELECT TRIG_NAME FROM system.triggers WHERE TABLE_NAME = '" .
                          addslashes($table) . "'");
    $names = [];
    while ($row = $stmt->fetchAssoc()) {
        $names[] = $row['TRIG_NAME'];
    }
    $stmt->close();

    $triggers = [];
    foreach ($names as $trigName) {
        // event_mask, timing, and options come back as 4-byte LE integers
        $evRaw     = $dict->getTriggerProperty($trigName, 1401);
        $timRaw    = $dict->getTriggerProperty($trigName, 1402);
        $optsRaw   = $dict->getTriggerProperty($trigName, 1407);
        $body      = $dict->getTriggerProperty($trigName, 1404);
        $procBody  = $dict->getTriggerProperty($trigName, 1405);
        // For NUL-delimited pmsys-imported triggers: SQL body is in proc (1405),
        // container (1404) holds only the type code "1".  Use proc if container
        // looks like a type indicator (length ≤ 4 and purely numeric/empty).
        if (strlen(trim($body)) <= 4 && preg_match('/^[0-9]*$/', trim($body)) && strlen($procBody) > 4) {
            $body = $procBody;
        }

        $eventMask = strlen($evRaw)   >= 4 ? unpack('V', substr($evRaw,   0, 4))[1] : 0;
        $timing    = strlen($timRaw)  >= 4 ? unpack('V', substr($timRaw,  0, 4))[1] : 0;
        $options   = strlen($optsRaw) >= 4 ? unpack('V', substr($optsRaw, 0, 4))[1] : 3;

        $triggers[] = [
            'name'       => $trigName,
            'timing'     => timing_str($timing),
            'event'      => event_str($eventMask),
            'priority'   => 1,
            'enabled'    => 'Yes',
            'body'       => rtrim($body),
            'options'    => $options,
            '_rawEvent'  => $eventMask,
            '_rawTiming' => $timing,
        ];
    }

    $conn->close();
    usort($triggers, fn($a, $b) => strcmp($a['name'], $b['name']));
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
    exit;
}

echo json_encode(
    ['triggers' => $triggers],
    JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE
);
