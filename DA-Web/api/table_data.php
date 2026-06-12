<?php
/**
 * api/table_data.php — return up to 2000 rows from a table for Tabulator.
 * GET  dd=&table=&orderby=expr&orderdir=ASC|DESC
 *
 * orderby: index expression like "leaseid" or "propertyID;EndDate"
 *          Semicolons are converted to commas for SQL ORDER BY.
 * orderdir: ASC (default) or DESC
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

if ($ddName === '' || $table === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd and table are required']);
    exit;
}
if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}

// Whitelist table name
if (!preg_match('/^[A-Za-z_][A-Za-z0-9_ ]*$/', $table)) {
    http_response_code(400);
    echo json_encode(['error' => 'invalid table name']);
    exit;
}

// Build server-side seek clause (WHERE seekfield >= 'value').
// seekfield must be a safe identifier; seek value is escaped by doubling single quotes.
$seekClause = '';
if ($seek !== '' && preg_match('/^[A-Za-z_][A-Za-z0-9_]*$/', $seekfield)) {
    $escapedSeek = str_replace("'", "''", $seek);
    $seekClause  = " WHERE $seekfield >= '$escapedSeek'";
}

// Build ORDER BY clause from index expression.
// Index expressions use semicolon as field separator; convert to comma for SQL.
// Each field must be a safe identifier (alphanumeric + underscore).
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

$c    = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

try {
    $conn = AdsConnection::connect($opts);
    $sql  = "SELECT * FROM $table$seekClause$orderClause LIMIT 2000";
    $stmt = $conn->query($sql);
    $rows = $stmt->fetchAll();
    $stmt->close();
    $conn->close();

    $out = json_encode(['data' => $rows, 'orderby' => $orderby, 'orderdir' => $orderdir],
                       JSON_INVALID_UTF8_SUBSTITUTE | JSON_PARTIAL_OUTPUT_ON_ERROR);
    echo $out !== false ? $out : json_encode(['error' => json_last_error_msg()]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
