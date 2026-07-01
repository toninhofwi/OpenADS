<?php
/**
 * api/blob_data.php — fetch a single blob/memo field value.
 *
 * GET ?dd=&table=&field=&pk=JSON_encoded_object[&download=1]
 *
 * Without download=1: returns JSON { text: "..." } — for memo/text fields.
 * With    download=1: streams raw bytes as application/octet-stream.
 */
session_start();
require_once __DIR__ . '/common.php';

$ddName   = trim($_GET['dd']       ?? '');
$table    = trim($_GET['table']    ?? '');
$field    = trim($_GET['field']    ?? '');
$pkJson   = $_GET['pk']            ?? '';
$download = ($_GET['download']     ?? '') === '1';

$sendJson = function (int $code, array $body) {
    http_response_code($code);
    header('Content-Type: application/json');
    echo json_encode($body);
    exit;
};

if (!isset($_SESSION['connections'][$ddName]))
    $sendJson(401, ['error' => "Not connected to '$ddName'"]);
if ($ddName === '' || $table === '' || $field === '' || $pkJson === '')
    $sendJson(400, ['error' => 'dd, table, field and pk are required']);
if (!preg_match('/^[A-Za-z_][A-Za-z0-9_ ]*$/', $table) ||
    !preg_match('/^[A-Za-z_][A-Za-z0-9_]*$/', $field))
    $sendJson(400, ['error' => 'invalid table or field name']);

$pk = json_decode($pkJson, true);
if (!is_array($pk) || empty($pk))
    $sendJson(400, ['error' => 'invalid pk parameter']);

$conditions = [];
foreach ($pk as $col => $val) {
    if (!preg_match('/^[A-Za-z_][A-Za-z0-9_]*$/', $col)) continue;
    $qval = str_replace("'", "''", (string)$val);
    $conditions[] = "$col = '$qval'";
}
if (empty($conditions))
    $sendJson(400, ['error' => 'no valid pk conditions']);

$c    = $_SESSION['connections'][$ddName];
$opts = api_ads_connect_opts($c);

try {
    $conn  = AdsConnection::connect($opts);
    $where = implode(' AND ', $conditions);
    $qf    = '"' . str_replace('"', '""', $field) . '"';
    $stmt  = $conn->query("SELECT $qf FROM $table WHERE $where");
    $row   = $stmt->fetchAssoc();
    $stmt->close();
    $conn->close();

    $value = $row ? ($row[$field] ?? $row[strtoupper($field)] ?? null) : null;

    if ($download) {
        $safeName = preg_replace('/[^A-Za-z0-9_\-]/', '_', $field);
        header('Content-Type: application/octet-stream');
        header('Content-Disposition: attachment; filename="' . $safeName . '.bin"');
        header('Cache-Control: no-cache');
        echo $value ?? '';
    } else {
        header('Content-Type: application/json');
        echo json_encode(['text' => (string)($value ?? '')],
                         JSON_INVALID_UTF8_SUBSTITUTE | JSON_PARTIAL_OUTPUT_ON_ERROR);
    }
} catch (Throwable $e) {
    $sendJson(500, ['error' => $e->getMessage(), 'code' => (int)$e->getCode()]);
}
