<?php
/**
 * api/ri_meta.php — RI object metadata, table list, and index tags.
 *
 * GET ?dd=&ri=<name>           → { ri: { name, parent, child, parent_tag, child_tag, update_opt, delete_opt } }
 * GET ?dd=&action=tables       → { tables: ["Table1", ...] }
 * GET ?dd=&action=tags&table=  → { tags: ["TagName", ...] }
 */
header('Content-Type: application/json');
session_start();

$ddName = trim($_GET['dd']     ?? '');
$action = trim($_GET['action'] ?? '');
$riName = trim($_GET['ri']     ?? '');
$table  = trim($_GET['table']  ?? '');

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

// Sanitise a string so json_encode never returns false.
function safeStr(mixed $v): string {
    $s = (string)($v ?? '');
    return mb_convert_encoding($s, 'UTF-8', 'UTF-8');
}

try {
    $conn = AdsConnection::connect($opts);

    // ── Return all tables ──────────────────────────────────────────────────
    if ($action === 'tables') {
        $tables = [];
        $stmt = $conn->query("SELECT TABLE_NAME FROM system.tables ORDER BY TABLE_NAME");
        while ($row = $stmt->fetchAssoc()) $tables[] = safeStr($row['TABLE_NAME']);
        $conn->close();
        echo json_encode(['tables' => $tables], JSON_FLAGS);
        exit;
    }

    // ── Return index tags for a table ──────────────────────────────────────
    if ($action === 'tags') {
        $tags = [];
        if ($table !== '') {
            try {
                $tbl = AdsTable::open($conn, $table, 0);
                foreach ($tbl->getIndexTags() as $t) $tags[] = safeStr($t['tag']);
                $tbl->close();
            } catch (Throwable $e) {}
        }
        $conn->close();
        echo json_encode(['tags' => $tags], JSON_FLAGS);
        exit;
    }

    // ── Return RI object by name ───────────────────────────────────────────
    if ($riName === '') {
        http_response_code(400);
        echo json_encode(['error' => 'ri name or action required']);
        exit;
    }

    $ri  = null;
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

    $conn->close();

    if ($ri === null) {
        echo json_encode(['ri' => [
            'name' => $riName, 'parent' => '', 'child' => '',
            'parent_tag' => '', 'child_tag' => '',
            'update_opt' => 'Restrict', 'delete_opt' => 'Restrict', 'fail_table' => '',
        ]], JSON_FLAGS);
    } else {
        echo json_encode(['ri' => $ri], JSON_FLAGS);
    }
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
}
