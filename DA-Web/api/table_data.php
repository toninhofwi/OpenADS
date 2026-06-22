<?php
/**
 * api/table_data.php — return up to 2000 rows from a table for Tabulator.
 * GET  dd=&table=&orderby=expr&orderdir=ASC|DESC&seek=val&seekfield=f&aof=expr
 *
 * orderby:   index expression like "leaseid" or "propertyID;EndDate"
 *            Semicolons are converted to commas for SQL ORDER BY.
 * orderdir:  ASC (default) or DESC
 * aof:       SQL WHERE expression (Rushmore-style AOF filter).
 *            Appended as WHERE (aof) [AND seekfield >= 'seek'].
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$ddName    = trim($_GET['dd']        ?? '');
$table     = trim($_GET['table']     ?? '');
$orderby   = trim($_GET['orderby']   ?? '');
$orderdir  = strtoupper(trim($_GET['orderdir'] ?? 'ASC')) === 'DESC' ? 'DESC' : 'ASC';
$seek      = $_GET['seek']           ?? '';
$seekfield = trim($_GET['seekfield'] ?? '');
$aof       = trim($_GET['aof']       ?? '');

if ($table === '') {
    api_error(400, 'table is required');
}
api_validate_identifier($table, 'table name');

$c = api_require_connection($ddName);

// Build WHERE conditions array.
$conditions = [];

// AOF filter expression — accepted as-is (user is authenticated; same trust
// level as the SQL console).  Strip leading/trailing WHERE keyword if present.
if ($aof !== '') {
    $aofStripped = preg_replace('/^\s*WHERE\s+/i', '', $aof);
    if ($aofStripped !== '') {
        $conditions[] = '(' . $aofStripped . ')';
    }
}

// Seek condition: seekfield >= 'value'
if ($seek !== '' && preg_match('/^[A-Za-z_][A-Za-z0-9_]*$/', $seekfield)) {
    $escapedSeek  = api_sql_quote((string)$seek);
    $conditions[] = "$seekfield >= '$escapedSeek'";
}

$whereClause = empty($conditions) ? '' : ' WHERE ' . implode(' AND ', $conditions);

// Build ORDER BY clause from index expression.
$orderClause = '';
if ($orderby !== '') {
    $parts = preg_split('/[;,]+/', $orderby);
    $safe  = [];
    foreach ($parts as $part) {
        $p = trim($part);
        if (preg_match('/^[A-Za-z_][A-Za-z0-9_]*$/', $p)) {
            $safe[] = $p;
        }
    }
    if (!empty($safe)) {
        $orderClause = ' ORDER BY ' . implode(', ', $safe) . ' ' . $orderdir;
    }
}

$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

try {
    $conn = AdsConnection::connect($opts);
    $sql  = "SELECT * FROM $table$whereClause$orderClause LIMIT 2000";
    $stmt = $conn->query($sql);
    $rows = $stmt->fetchAll();
    $stmt->close();
    $conn->close();

    $out = json_encode([
        'data'     => $rows,
        'orderby'  => $orderby,
        'orderdir' => $orderdir,
        'aof'      => $aof,
    ], JSON_INVALID_UTF8_SUBSTITUTE | JSON_PARTIAL_OUTPUT_ON_ERROR);
    echo $out !== false ? $out : json_encode(['error' => json_last_error_msg()]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
