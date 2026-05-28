<?php
/**
 * api/row_ops.php — INSERT / DELETE a single row via AdsTable native API.
 * POST { action, dd, table, row }   action=insert  → append + write record
 * POST { action, dd, table, orig }  action=delete  → scan + delete matching record
 */
header('Content-Type: application/json');
session_start();

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$action = trim($body['action'] ?? '');
$ddName = trim($body['dd']     ?? '');
$table  = trim($body['table']  ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if (!preg_match('/^[A-Za-z_][A-Za-z0-9_]*$/', $table)) {
    http_response_code(400);
    echo json_encode(['error' => 'invalid table name']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

try {
    $conn = AdsConnection::connect($opts);
    // 0 = ADS_DEFAULT: let ACE infer table type from the DD / file extension
    $tbl  = AdsTable::open($conn, $table, 0);

    if ($action === 'insert') {
        $row = $body['row'] ?? [];
        $tbl->appendRecord();
        foreach ($row as $field => $value) {
            if (!preg_match('/^[A-Za-z_][A-Za-z0-9_]*$/', $field)) continue;
            if ($value === null || $value === '') continue;
            try {
                if (is_bool($value))        $tbl->setLogical($field, $value);
                elseif (is_int($value))     $tbl->setLong($field, $value);
                elseif (is_float($value))   $tbl->setDouble($field, $value);
                else                        $tbl->setString($field, (string)$value);
            } catch (AdsException) {
                // skip unwritable fields (auto-increment, read-only, etc.)
            }
        }
        $tbl->writeRecord();
        $tbl->close();
        $conn->close();
        echo json_encode(['ok' => true]);

    } elseif ($action === 'delete') {
        $orig = $body['orig'] ?? [];
        // Build a normalised comparison map: lowercase field → trimmed string value
        $cmp = [];
        foreach ($orig as $k => $v) {
            $cmp[strtolower($k)] = ($v === null) ? null : rtrim((string)$v);
        }

        $tbl->gotoTop();
        $found = false;
        while (!$tbl->atEOF()) {
            $rec = $tbl->getRecord();
            $match = true;
            foreach ($cmp as $lk => $cv) {
                // find the actual field name (case-insensitive)
                $found_key = null;
                foreach ($rec as $rk => $rv) {
                    if (strtolower($rk) === $lk) { $found_key = $rk; break; }
                }
                if ($found_key === null) { $match = false; break; }
                $rv = ($rec[$found_key] === null) ? null : rtrim((string)$rec[$found_key]);
                if ($cv !== $rv) { $match = false; break; }
            }
            if ($match) {
                $tbl->deleteRecord();
                $found = true;
                break;
            }
            $tbl->skip(1);
        }
        $tbl->close();
        $conn->close();
        if ($found) echo json_encode(['ok' => true]);
        else        echo json_encode(['error' => 'Matching record not found']);

    } else {
        http_response_code(400);
        echo json_encode(['error' => 'unknown action']);
    }
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
}
