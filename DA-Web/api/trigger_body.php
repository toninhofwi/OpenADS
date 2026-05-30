<?php
/**
 * api/trigger_body.php — read full trigger body + metadata from binary .add/.am.
 *
 * POST { dd, table }
 *   Returns { triggers: [ { name, timing, event, priority, enabled, body } ] }
 *
 * SAP binary .add property area layout for Trigger records (273 bytes at base+225):
 *   [0..3]   event  LE uint32: 1=INSERT  2=UPDATE  3=DELETE
 *   [4..5]   0x04 (constant)
 *   [6..7]   timing LE uint16: 1=BEFORE  2=INSTEAD OF  4=AFTER
 *   [8..9]   0x00 (no-memos flag / reserved)
 *   [10..11] 0x04 (constant)
 *   [12..15] constant block
 *   [16..17] inline body length LE uint16
 *   [18..]   inline body SQL text (up to 255 bytes)
 * More Property (9 bytes at base+498):
 *   [0..3]  am_block LE uint32  → .am byte offset = am_block * 8
 *   [4..7]  am_len   LE uint32
 */
header('Content-Type: application/json');
session_start();

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName = trim($body['dd']    ?? '');
$table  = trim($body['table'] ?? '');

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

$addPath = $_SESSION['connections'][$ddName]['path'];
$amPath  = preg_replace('/\.[^.\/\\\\]+$/', '.am', $addPath);

$data = @file_get_contents($addPath);
if ($data === false) {
    http_response_code(500);
    echo json_encode(['error' => 'Cannot read dictionary file']);
    exit;
}

$am = is_file($amPath) ? (@file_get_contents($amPath) ?: '') : '';

function le16(string $d, int $o): int { return unpack('v', substr($d, $o, 2))[1]; }
function le32(string $d, int $o): int { return unpack('V', substr($d, $o, 4))[1]; }

function timing_str(int $t): string {
    return match($t) { 1 => 'BEFORE', 2 => 'INSTEAD OF', 4 => 'AFTER', default => '' };
}
function event_str(int $e): string {
    return match($e) { 1 => 'INSERT', 2 => 'UPDATE', 3 => 'DELETE', default => '' };
}

// Read .add header
$hdrLen = le32($data, 0x20);
$recLen = le32($data, 0x24);
if ($recLen === 0) {
    http_response_code(500);
    echo json_encode(['error' => 'Corrupt dictionary header']);
    exit;
}

$total = intdiv(strlen($data) - $hdrLen, $recLen);

// Build id→name map for parent lookups
$idToName = [];
for ($i = 0; $i < $total; $i++) {
    $base   = $hdrLen + $i * $recLen;
    $status = ord($data[$base]);
    if ($status !== 0x04) continue;
    $oid  = le32($data, $base + 5);
    $name = rtrim(substr($data, $base + 23, 200), " \0");
    if ($name !== '') $idToName[$oid] = $name;
}

$triggers = [];

for ($i = 0; $i < $total; $i++) {
    $base   = $hdrLen + $i * $recLen;
    $status = ord($data[$base]);
    if ($status !== 0x04) continue;

    $objType = rtrim(substr($data, $base + 13, 10), " \0");
    if ($objType !== 'Trigger') continue;

    $trigName = rtrim(substr($data, $base + 23, 200), " \0");
    $parentId = le32($data, $base + 9);
    $tableAlias = $idToName[$parentId] ?? '';

    // Filter by table name
    if (strcasecmp($tableAlias, $table) !== 0) continue;

    // Property area starts at base+225 (273 bytes)
    $PA = $base + 225;

    $event  = le32($data, $PA + 0);   // bytes 0-3: event type
    $timing = le16($data, $PA + 6);   // bytes 6-7: timing

    // Inline body: bytes 18+ of the property area, up to byte 272
    $inlineStart = $PA + 18;
    $inlineEnd   = $PA + 273;
    if ($inlineEnd > strlen($data)) $inlineEnd = strlen($data);
    $inline = substr($data, $inlineStart, $inlineEnd - $inlineStart);
    $nul = strpos($inline, "\0");
    if ($nul !== false) $inline = substr($inline, 0, $nul);

    // .am continuation
    $amBlock = le32($data, $base + 498);
    $amLen   = le32($data, $base + 502);
    $amBody  = '';
    if ($amBlock > 0 && $amLen > 0 && $am !== '') {
        $amOff  = $amBlock * 8;
        $amBody = substr($am, $amOff, $amLen);
        // Strip binary padding from end
        $len = strlen($amBody);
        while ($len > 0) {
            $b = ord($amBody[$len - 1]);
            if (($b >= 0x20 && $b <= 0x7E) || $b === 0x09 || $b === 0x0A || $b === 0x0D) break;
            $len--;
        }
        $amBody = substr($amBody, 0, $len);
    }

    $fullBody = rtrim($inline . $amBody);

    // info1 at base+507 (used for some flags; priority defaults to 1 for SQL triggers)
    $info1 = le32($data, $base + 507);
    $priority = 1;  // SAP SQL triggers default to priority 1

    // enabled: always true in SAP binary (disabled stored differently)
    $enabled = true;

    $triggers[] = [
        'name'     => $trigName,
        'timing'   => timing_str($timing),
        'event'    => event_str($event),
        'priority' => $priority,
        'enabled'  => $enabled ? 'Yes' : 'No',
        'body'     => $fullBody,
        '_rawEvent'  => $event,
        '_rawTiming' => $timing,
    ];
}

// Sort by name
usort($triggers, fn($a, $b) => strcmp($a['name'], $b['name']));

$json = json_encode(
    ['triggers' => $triggers],
    JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE
);
echo $json;
