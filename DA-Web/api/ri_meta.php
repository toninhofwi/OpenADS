<?php
/**
 * api/ri_meta.php — RI object metadata and table list.
 *
 * GET ?dd=&ri=<name>   → { ri:{…}, parentTags:[…], childTags:[…] }
 * GET ?dd=&action=tables → { tables: ["Table1", …] }
 *
 * Index tags are now embedded in the main RI response so the client
 * does not need separate tag-fetch round-trips.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$ddName = trim($_GET['dd']     ?? '');
$action = trim($_GET['action'] ?? '');
$riName = trim($_GET['ri']     ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd is required']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

const JSON_FLAGS = JSON_UNESCAPED_UNICODE | JSON_INVALID_UTF8_SUBSTITUTE | JSON_PARTIAL_OUTPUT_ON_ERROR;

function ruleLabel(string $v): string {
    $v = trim($v);
    return match(true) {
        $v === '1' || strcasecmp($v, 'Restrict') === 0 => 'Restrict',
        $v === '2' || strcasecmp($v, 'Cascade')  === 0 => 'Cascade',
        $v === '3' || strcasecmp($v, 'SetNull')  === 0 => 'SetNull',
        default => $v ?: 'Restrict',
    };
}

// Trim + UTF-8 sanitise a DB string (fixed-width char fields carry trailing spaces).
function safeStr(mixed $v): string {
    $s = trim((string)($v ?? ''));
    return mb_convert_encoding($s, 'UTF-8', 'UTF-8');
}

// Read index tag names for a table from the binary .add file.
// Walks Table → Index → Key record hierarchy; returns sorted tag names.
function fetchTagsFromBinary(string $addPath, string $tableName): array {
    $bin = @file_get_contents($addPath);
    if ($bin === false || strlen($bin) < 40) return [];

    $hdrLen = unpack('V', substr($bin, 0x20, 4))[1];
    $recLen = unpack('V', substr($bin, 0x24, 4))[1];
    if ($recLen === 0) return [];
    $total = intdiv(strlen($bin) - $hdrLen, $recLen);

    $idName   = [];   // obj_id  → name
    $idType   = [];   // obj_id  → obj_type string
    $idParent = [];   // obj_id  → parent_id

    for ($i = 0; $i < $total; $i++) {
        $base = $hdrLen + $i * $recLen;
        if ($base + $recLen > strlen($bin)) break;
        if (ord($bin[$base]) !== 0x04) continue;          // 0x04 = active
        $oid = unpack('V', substr($bin, $base + 5, 4))[1];
        $pid = unpack('V', substr($bin, $base + 9, 4))[1];
        $ot  = rtrim(substr($bin, $base + 13, 10), " \0");
        $nm  = rtrim(substr($bin, $base + 23, 200), " \0");
        if ($nm !== '') $idName[$oid] = $nm;
        $idType[$oid]   = $ot;
        $idParent[$oid] = $pid;
    }

    // Find table obj_id by name (case-insensitive)
    $tableId = null;
    foreach ($idType as $oid => $ot) {
        if ($ot === 'Table' && isset($idName[$oid]) &&
            strcasecmp($idName[$oid], $tableName) === 0) {
            $tableId = $oid;
            break;
        }
    }
    if ($tableId === null) return [];

    // Collect Index obj_ids whose parent = table
    $indexIds = [];
    foreach ($idParent as $oid => $pid) {
        if ($pid === $tableId && ($idType[$oid] ?? '') === 'Index') {
            $indexIds[$oid] = true;
        }
    }

    // Collect Key names whose parent = one of those Index records
    $tags = [];
    foreach ($idParent as $oid => $pid) {
        if (isset($indexIds[$pid]) && ($idType[$oid] ?? '') === 'Key') {
            $tag = $idName[$oid] ?? '';
            if ($tag !== '') $tags[] = $tag;
        }
    }
    sort($tags);
    return $tags;
}

try {
    $conn = AdsConnection::connect($opts);

    $addPath = $c['path'];   // path to the .add file

    // ── Return all tables ──────────────────────────────────────────────────
    if ($action === 'tables') {
        $tables = [];
        $stmt = $conn->query("SELECT TABLE_NAME FROM system.tables ORDER BY TABLE_NAME");
        while ($row = $stmt->fetchAssoc()) {
            $t = safeStr($row['TABLE_NAME']);
            if ($t !== '') $tables[] = $t;
        }
        $conn->close();
        echo json_encode(['tables' => $tables], JSON_FLAGS);
        exit;
    }

    // ── Return tag names for a table (used when user changes table select) ──
    if ($action === 'tags') {
        $table = trim($_GET['table'] ?? '');
        $conn->close();
        echo json_encode(['tags' => fetchTagsFromBinary($addPath, $table)], JSON_FLAGS);
        exit;
    }

    // ── Return RI object by name, with embedded index tags ─────────────────
    if ($riName === '') {
        http_response_code(400);
        echo json_encode(['error' => 'ri name or action required']);
        exit;
    }

    $ri = null;
    $stmt = $conn->query("SELECT * FROM system.relations");
    while ($row = $stmt->fetchAssoc()) {
        if (strcasecmp(safeStr($row['RI_NAME'] ?? ''), $riName) === 0) {
            $ri = [
                'name'       => safeStr($row['RI_NAME']    ?? ''),
                'parent'     => safeStr($row['PARENT']     ?? ''),
                'child'      => safeStr($row['CHILD']      ?? ''),
                'parent_tag' => safeStr($row['PARENT_TAG'] ?? ''),
                'child_tag'  => safeStr($row['CHILD_TAG']  ?? ''),
                'update_opt' => ruleLabel(safeStr($row['UPDATE_OPT'] ?? '')),
                'delete_opt' => ruleLabel(safeStr($row['DELETE_OPT'] ?? '')),
                'fail_table' => safeStr($row['FAIL_TABLE'] ?? ''),
            ];
            break;
        }
    }

    if ($ri === null) {
        $ri = [
            'name' => $riName, 'parent' => '', 'child' => '',
            'parent_tag' => '', 'child_tag' => '',
            'update_opt' => 'Restrict', 'delete_opt' => 'Restrict', 'fail_table' => '',
        ];
    }

    // Embed index tags for both tables from the binary .add file
    $parentTags = fetchTagsFromBinary($addPath, $ri['parent']);
    $childTags  = fetchTagsFromBinary($addPath, $ri['child']);

    $conn->close();
    echo json_encode(['ri' => $ri, 'parentTags' => $parentTags, 'childTags' => $childTags], JSON_FLAGS);

} catch (Throwable $e) {
    api_exception(500, $e);
}
