<?php
/**
 * api/sql_scripts.php — persist named SQL scripts to config/sql_scripts.json
 * GET                          → returns { name: sql, … }
 * POST { action:'save', name, sql }  → upsert a script
 * POST { action:'delete', name }     → remove a script
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$file = __DIR__ . '/../config/sql_scripts.json';

function loadScripts(string $f): array {
    if (!file_exists($f)) return [];
    return json_decode(file_get_contents($f), true) ?? [];
}
function saveScripts(string $f, array $scripts): void {
    file_put_contents($f, json_encode($scripts, JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE));
}

if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    echo json_encode(loadScripts($file));
    exit;
}

api_require_session();

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$action = trim($body['action'] ?? '');

if ($action === 'save') {
    $name = trim($body['name'] ?? '');
    $sql  = $body['sql']  ?? '';
    if ($name === '') {
        api_error(400, 'name required');
    }
    if (str_contains($name, "\0")) {
        api_error(400, 'invalid script name');
    }
    $scripts = loadScripts($file);
    $scripts[$name] = $sql;
    saveScripts($file, $scripts);
    echo json_encode(['ok' => true]);
    exit;
}
if ($action === 'delete') {
    $name = trim($body['name'] ?? '');
    if ($name === '') {
        api_error(400, 'name required');
    }
    if (str_contains($name, "\0")) {
        api_error(400, 'invalid script name');
    }
    $scripts = loadScripts($file);
    unset($scripts[$name]);
    saveScripts($file, $scripts);
    echo json_encode(['ok' => true]);
    exit;
}

api_error(400, 'unknown action');