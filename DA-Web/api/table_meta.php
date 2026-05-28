<?php
/**
 * api/table_meta.php — return field or index metadata for a table.
 * GET ?dd=&table=&kind=fields|indexes
 */
header('Content-Type: application/json');
session_start();

$ddName = trim($_GET['dd']    ?? '');
$table  = trim($_GET['table'] ?? '');
$kind   = trim($_GET['kind']  ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if (!preg_match('/^[A-Za-z_][A-Za-z0-9_]*$/', $table) || !in_array($kind, ['fields', 'indexes'], true)) {
    http_response_code(400);
    echo json_encode(['error' => 'invalid parameters']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

function fieldTypeLabel(string $raw, int $len, int $dec): string {
    // ASCII letter codes — DBF/CDX/NTX tables
    switch ($raw) {
        case 'C': return "Character($len)";
        case 'N': return $dec > 0 ? "Numeric($len,$dec)" : "Numeric($len)";
        case 'F': return $dec > 0 ? "Float($len,$dec)"   : "Float($len)";
        case 'L': return 'Logical';
        case 'D': return 'Date';
        case 'M': return 'Memo';
        case 'I': return 'Integer';
        case 'Y': return 'Currency';
        case 'B': return 'Double';
        case 'V': return "Varchar($len)";
        case 'Q': return "Varbinary($len)";
        case 'T': return 'DateTime';
        case '@': return 'Timestamp';
        case '+': return 'AutoIncrement';
        case 'W': return 'Blob';
        case 'G': return 'General';
    }
    // Numeric codes — ADT tables store type as static_cast<char>(code & 0xFF)
    switch (ord(substr($raw, 0, 1))) {
        case  1: return 'Logical';
        case  3: return 'Date';
        case  4: return "Character($len)";
        case  5: return 'Memo';
        case  6: return 'Binary';
        case 10: return 'Double';
        case 11: return 'Integer';
        case 12: return 'ShortInt';
        case 13: return 'Time';
        case 14: return 'Timestamp';
        case 15: return 'AutoIncrement';
        case 18: return 'Money';
        case 20: return "Character($len) CI";
        default: return $raw ?: '?';
    }
}

try {
    $conn = AdsConnection::connect($opts);
    $rows = [];

    if ($kind === 'fields') {
        $stmt = $conn->query("SELECT TABLE_NAME, COL_NAME, COL_NUM, COL_TYPE, COL_LEN, COL_DEC FROM system.columns");
        while ($row = $stmt->fetchAssoc()) {
            if (strcasecmp($row['TABLE_NAME'], $table) !== 0) continue;
            $len = (int)$row['COL_LEN'];
            $dec = (int)$row['COL_DEC'];
            $rows[] = [
                'Order' => (int)$row['COL_NUM'],
                'Field' => $row['COL_NAME'],
                'Type'  => fieldTypeLabel(trim($row['COL_TYPE']), $len, $dec),
            ];
        }
        usort($rows, fn($a, $b) => $a['Order'] <=> $b['Order']);
    } else {
        // Open the table to enumerate index tags via AdsGetNumIndexes / AdsGetIndexHandleByOrder
        $tbl  = AdsTable::open($conn, $table, 0);
        $tags = $tbl->getIndexTags();
        $tbl->close();
        foreach ($tags as $t) {
            $rows[] = [
                'Tag'        => $t['tag'],
                'Expression' => $t['expression'],
                'Order'      => $t['descending'] ? 'Descending' : 'Ascending',
            ];
        }
    }

    $conn->close();
    echo json_encode(['data' => $rows]);
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
}
