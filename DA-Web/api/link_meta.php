<?php
/**
 * api/link_meta.php — return link info from system.links.
 *
 * GET ?dd=&name=
 * Returns { name, path, user }
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$ddName = trim($_GET['dd']   ?? '');
$name   = trim($_GET['name'] ?? '');

if ($ddName === '' || $name === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd and name are required']);
    exit;
}
if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = api_ads_connect_opts($c);

try {
    $conn = AdsConnection::connect($opts);
    $qn   = str_replace("'", "''", $name);
    $stmt = $conn->query("SELECT LINK_NAME, LINK_PATH, LINK_USER FROM system.links WHERE LINK_NAME = '$qn'");
    $row  = $stmt->fetchAssoc();
    $stmt->close();
    $conn->close();

    if (!$row) {
        http_response_code(404);
        echo json_encode(['error' => "Link '$name' not found"]);
        exit;
    }
    echo json_encode([
        'name' => $row['LINK_NAME'],
        'path' => $row['LINK_PATH'],
        'user' => $row['LINK_USER'],
    ]);
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
}
