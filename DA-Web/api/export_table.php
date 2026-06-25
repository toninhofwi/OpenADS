<?php
/**
 * api/export_table.php — stream all rows from a table as CSV.
 * GET ?dd=&table=&orderby=&orderdir=ASC|DESC&aof=
 *
 * Streams rows directly so large tables do not blow the PHP memory limit.
 * Binary fields that are not valid UTF-8 are replaced with [binary].
 */
session_start();
require_once __DIR__ . '/common.php';

$ddName   = trim($_GET['dd']       ?? '');
$table    = trim($_GET['table']    ?? '');
$orderby  = trim($_GET['orderby']  ?? '');
$orderdir = strtoupper(trim($_GET['orderdir'] ?? 'ASC')) === 'DESC' ? 'DESC' : 'ASC';
$aof      = trim($_GET['aof']      ?? '');

$sendErr = function (int $code, string $msg) {
    http_response_code($code);
    header('Content-Type: application/json');
    echo json_encode(['error' => $msg]);
    exit;
};

if ($ddName === '' || $table === '') $sendErr(400, 'dd and table are required');
if (!isset($_SESSION['connections'][$ddName]))
    $sendErr(401, "Not connected to '$ddName'");
if (!preg_match('/^[A-Za-z_][A-Za-z0-9_ ]*$/', $table))
    $sendErr(400, 'invalid table name');

// Build WHERE
$conditions = [];
if ($aof !== '') {
    $stripped = preg_replace('/^\s*WHERE\s+/i', '', $aof);
    if ($stripped !== '') $conditions[] = '(' . $stripped . ')';
}
$whereClause = empty($conditions) ? '' : ' WHERE ' . implode(' AND ', $conditions);

// Build ORDER BY
$orderClause = '';
if ($orderby !== '') {
    $parts = preg_split('/[;,]+/', $orderby);
    $safe  = [];
    foreach ($parts as $p) {
        $p = trim($p);
        if (preg_match('/^[A-Za-z_][A-Za-z0-9_]*$/', $p)) $safe[] = $p;
    }
    if (!empty($safe)) $orderClause = ' ORDER BY ' . implode(', ', $safe) . ' ' . $orderdir;
}

$c    = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

try {
    $conn = AdsConnection::connect($opts);
    $sql  = "SELECT * FROM $table$whereClause$orderClause";
    $stmt = $conn->query($sql);

    $safeName = preg_replace('/[^A-Za-z0-9_\-]/', '_', $table);
    header('Content-Type: text/csv; charset=UTF-8');
    header('Content-Disposition: attachment; filename="' . $safeName . '_export.csv"');
    header('Cache-Control: no-cache');
    // UTF-8 BOM so Excel opens with correct encoding
    echo "\xEF\xBB\xBF";

    $out           = fopen('php://output', 'w');
    $headerWritten = false;

    while ($row = $stmt->fetchAssoc()) {
        if (!$headerWritten) {
            fputcsv($out, array_keys($row));
            $headerWritten = true;
        }
        $safe = [];
        foreach ($row as $v) {
            if ($v === null) {
                $safe[] = '';
            } elseif (is_string($v) && !mb_check_encoding($v, 'UTF-8')) {
                $safe[] = '[binary]';
            } else {
                $safe[] = $v;
            }
        }
        fputcsv($out, $safe);
    }
    fclose($out);
    $stmt->close();
    $conn->close();
} catch (Throwable $e) {
    $sendErr(500, $e->getMessage());
}
