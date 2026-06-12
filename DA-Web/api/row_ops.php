<?php
/**
 * api/row_ops.php — INSERT / UPDATE / DELETE a single row.
 * POST { action, dd, table, row }                        action=insert → append + write
 * POST { action, dd, table, orig }                       action=delete → scan + delete
 * POST { action, dd, table, orig, row, pkFields:[…] }    action=update → SQL UPDATE
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

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

    } elseif ($action === 'update') {
        $orig     = $body['orig']     ?? [];
        $newVals  = $body['row']      ?? [];
        $pkFields = array_map('strtoupper', $body['pkFields'] ?? []);

        if (empty($pkFields)) {
            $tbl->close(); $conn->close();
            http_response_code(400);
            echo json_encode(['error' => 'pkFields required for update']);
            exit;
        }

        $esc = fn($v) => str_replace("'", "''", (string)$v);

        // SET — all fields that changed, excluding PK columns
        $setParts = [];
        foreach ($newVals as $field => $value) {
            if (!preg_match('/^[A-Za-z_][A-Za-z0-9_]*$/', $field)) continue;
            if (in_array(strtoupper($field), $pkFields, true)) continue;
            $setParts[] = $value === null ? "$field = NULL" : "$field = '{$esc($value)}'";
        }

        if (empty($setParts)) {
            $tbl->close(); $conn->close();
            echo json_encode(['ok' => true]); // nothing changed
            exit;
        }

        // WHERE — PK columns from original row
        $whereParts = [];
        foreach ($pkFields as $pk) {
            $val = null;
            foreach ($orig as $k => $v) {
                if (strtoupper($k) === $pk) { $val = $v; break; }
            }
            $whereParts[] = $val === null ? "$pk IS NULL" : "$pk = '{$esc($val)}'";
        }

        $tbl->close(); // release AdsTable before issuing SQL on same connection
        $sql = "UPDATE $table SET " . implode(', ', $setParts)
             . " WHERE "            . implode(' AND ', $whereParts);
        $conn->execute($sql);
        $conn->close();
        echo json_encode(['ok' => true]);

    } else {
        http_response_code(400);
        echo json_encode(['error' => 'unknown action']);
    }
} catch (Throwable $e) {
    api_exception(500, $e);
}
