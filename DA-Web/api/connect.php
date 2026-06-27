<?php
/**
 * api/connect.php — open / close a DD connection
 * POST action=connect  → credentials stored in session, connection tested
 * POST action=disconnect → session credentials removed
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

if (!extension_loaded('openads')) {
    http_response_code(500);
    echo json_encode(['error' => "php_openads extension not loaded (check extension=php_openads in php.ini)"]);
    exit;
}

$body = json_decode(file_get_contents('php://input'), true) ?? [];
$action = $body['action'] ?? '';

if ($action === 'connect') {
    $name     = trim($body['name']     ?? '');
    $path     = trim($body['path']     ?? '');
    $username = trim($body['username'] ?? '');
    $password = trim($body['password'] ?? '');
    $connType = strtolower(trim($body['connType'] ?? 'local'));
    if (!in_array($connType, ['local', 'remote'], true)) $connType = 'local';

    if ($name === '' || $path === '') {
        http_response_code(400);
        echo json_encode(['error' => 'name and path are required']);
        exit;
    }

    try {
        $opts = api_ads_connect_opts([
            'path' => $path,
            'username' => $username,
            'password' => $password,
            'connType' => $connType,
        ]);
        $conn = AdsConnection::connect($opts);
        $conn->close();
    } catch (AdsException $e) {
        $ext = strtolower(pathinfo($path, PATHINFO_EXTENSION));
        $needs_import = $e->getCode() === 5174 ||
            ($e->getCode() === 5103 && $ext === 'add');
        api_exception(401, $e, $needs_import ? ['path' => $path] : []);
    }

    if (!isset($_SESSION['connections'])) {
        $_SESSION['connections'] = [];
    }
    $_SESSION['connections'][$name] = [
        'path'     => $opts['path'] ?? $path,
        'sourcePath' => $path,
        'username' => $username,
        'password' => $password,
        'connType' => $connType,
    ];

    echo json_encode(['ok' => true]);
    exit;
}

if ($action === 'disconnect') {
    $name = trim($body['name'] ?? '');
    if (isset($_SESSION['connections'][$name])) {
        unset($_SESSION['connections'][$name]);
    }
    echo json_encode(['ok' => true]);
    exit;
}

http_response_code(400);
echo json_encode(['error' => 'unknown action']);
