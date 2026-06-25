<?php
/**
 * api/save_view.php — save or drop a view in the DD.
 *
 * POST { action: 'save'|'drop', dd, name, sql?, comment? }
 *
 * 'save' updates the view SQL (and comment) via setViewProperty.
 * 'drop' removes the view via dropView.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/openads_stubs.php';

$body    = json_decode(file_get_contents('php://input'), true) ?? [];
$action  = trim($body['action']  ?? '');
$ddName  = trim($body['dd']      ?? '');
$name    = trim($body['name']    ?? '');

if (!in_array($action, ['save', 'drop'], true)) {
    http_response_code(400);
    echo json_encode(['error' => 'action must be save or drop']);
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
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

try {
    $conn = AdsConnection::connect($opts);
    $dict = AdsDictionary::fromConnection($conn);

    if ($action === 'drop') {
        $dict->dropView($name);
        $conn->close();
        echo json_encode(['dropped' => true]);
    } else {
        $sql     = $body['sql']     ?? '';
        $comment = $body['comment'] ?? '';
        $dict->setViewProperty($name, 701, $sql);     // ADS_DD_VIEW_STMT
        $dict->setViewProperty($name, 702, $comment); // ADS_DD_VIEW_COMMENT
        $conn->close();
        echo json_encode(['saved' => true]);
    }
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
}
