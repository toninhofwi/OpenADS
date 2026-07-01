<?php
/**
 * api/view_body.php — return a view's SQL body and comment.
 *
 * POST { dd, name }
 * Returns { sql, comment }
 *
 * Property codes (from openads/ace.h):
 *   ADS_DD_VIEW_STMT    = 701
 *   ADS_DD_VIEW_COMMENT = 702
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/openads_stubs.php';

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName = trim($body['dd']   ?? '');
$name   = trim($body['name'] ?? '');

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
    $conn    = AdsConnection::connect($opts);
    $dict    = AdsDictionary::fromConnection($conn);
    $sql     = $dict->getViewProperty($name, 701);   // ADS_DD_VIEW_STMT
    $comment = $dict->getViewProperty($name, 702);   // ADS_DD_VIEW_COMMENT
    $conn->close();

    echo json_encode([
        'sql'     => rtrim($sql),
        'comment' => $comment,
    ], JSON_UNESCAPED_UNICODE | JSON_INVALID_UTF8_SUBSTITUTE);
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
}
