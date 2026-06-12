<?php
/**
 * api/execute_sql.php — execute arbitrary SQL in a connected DD
 * POST { dd, sql }
 * Returns { columns[], data[] } for SELECT, or { affected } for DML
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName = trim($body['dd']  ?? '');
$sql    = trim($body['sql'] ?? '');

if ($ddName === '' || $sql === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd and sql are required']);
    exit;
}

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}

$c = $_SESSION['connections'][$ddName];

try {
    $opts = ['path' => $c['path']];
    if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
    if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];
    $conn = AdsConnection::connect($opts);

    $upperSql = ltrim(strtoupper($sql));
    $isSelect = str_starts_with($upperSql, 'SELECT') || str_starts_with($upperSql, 'WITH');

    if ($isSelect) {
        $stmt    = $conn->query($sql);
        $rows    = $stmt->fetchAll();
        $columns = [];
        if (!empty($rows)) {
            foreach (array_keys($rows[0]) as $col) {
                $columns[] = ['title' => $col, 'field' => $col];
            }
        }
        $stmt->close();
        $conn->close();
        echo json_encode(['columns' => $columns, 'data' => $rows]);
    } else {
        $conn->execute($sql);
        $conn->close();
        echo json_encode(['affected' => true, 'message' => 'Statement executed successfully']);
    }
} catch (AdsException $e) {
    api_exception(500, $e);
}
