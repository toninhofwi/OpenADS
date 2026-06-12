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

// Fetch index tag names for a table via AdsTable::getIndexTags().
function fetchTags(AdsConnection $conn, string $table): array {
    if ($table === '') return [];
    $tags = [];
    try {
        $tbl = AdsTable::open($conn, $table, 0);
        foreach ($tbl->getIndexTags() as $t) {
            $tag = trim((string)($t['tag'] ?? ''));
            if ($tag !== '') $tags[] = $tag;
        }
        $tbl->close();
    } catch (Throwable $e) {}
    return $tags;
}

try {
    $conn = AdsConnection::connect($opts);

    // ── Return all tables ──────────────────────────────────────────────────
    if ($action === 'tables') {
        $tables = [];
        $stmt = $conn->query("SELECT TABLE_NAME FROM system.tables ORDER BY TABLE_NAME");
        while ($row = $stmt->fetchAssoc()) {
            $t = safeStr($row['TABLE_NAME']);   // safeStr now trims
            if ($t !== '') $tables[] = $t;
        }
        $conn->close();
        echo json_encode(['tables' => $tables], JSON_FLAGS);
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

    // Embed index tags for both tables so client needs no extra round-trips
    $parentTags = fetchTags($conn, $ri['parent']);
    $childTags  = fetchTags($conn, $ri['child']);

    $conn->close();
    echo json_encode(['ri' => $ri, 'parentTags' => $parentTags, 'childTags' => $childTags], JSON_FLAGS);

} catch (Throwable $e) {
    api_exception(500, $e);
}
