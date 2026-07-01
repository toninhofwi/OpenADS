<?php
/**
 * api/link_ops.php — drop a cross-dictionary link.
 *
 * POST { action: 'drop', dd, name }
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/openads_stubs.php';

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$action = trim($body['action'] ?? '');
$ddName = trim($body['dd']     ?? '');
$name   = trim($body['name']   ?? '');

if ($action !== 'drop') {
    http_response_code(400);
    echo json_encode(['error' => 'action must be drop']);
    exit;
}
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
    $dict = AdsDictionary::fromConnection($conn);
    $dict->dropLink($name);
    $conn->close();
    echo json_encode(['dropped' => true]);
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
}
