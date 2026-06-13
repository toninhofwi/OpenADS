<?php
/**
 * api/save_proc.php — save stored-procedure or function back to the DD.
 *
 * POST {
 *   dd          : string   – dictionary name (must be connected)
 *   type        : string   – "proc" | "function"
 *   name        : string   – object name (case-insensitive)
 *   body        : string   – full SQL body
 *   input_params : string  – serialised "Name1,TYPE1;Name2,TYPE2;" (procs) or "name TYPE, ..." (funcs)
 *   output_params: string  – serialised "Name1,TYPE1;" (stored procs only)
 *   return_type  : string  – function return type (functions only)
 * }
 *
 * Uses AdsDictionary API — no binary file parsing.
 *   StoredProc: createProcedure(name, container='', body, input, output)
 *   Function:   createFunction(name, container='', body, return_type, input)
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/openads_stubs.php';

$req          = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName       = trim($req['dd']           ?? '');
$type         = trim($req['type']         ?? '');
$name         = trim($req['name']         ?? '');
$newBody      = $req['body']              ?? '';
$newInParams  = trim($req['input_params'] ?? '');
$newOutParams = trim($req['output_params'] ?? '');
$newRetType   = trim($req['return_type']  ?? '');

if ($ddName === '' || $name === '' || !in_array($type, ['proc', 'function'], true)) {
    http_response_code(400);
    echo json_encode(['error' => 'dd, type (proc|function), and name are required']);
    exit;
}
if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}

$c = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

try {
    $conn = AdsConnection::connect($opts);
    $dict = AdsDictionary::fromConnection($conn);

    if ($type === 'function') {
        $dict->createFunction($name, '', $newBody, $newRetType, $newInParams);
    } else {
        // Normalise params: ensure trailing semicolon
        $inNorm  = $newInParams  !== '' ? rtrim($newInParams,  ';') . ';' : '';
        $outNorm = $newOutParams !== '' ? rtrim($newOutParams, ';') . ';' : '';
        $dict->createProcedure($name, '', $newBody, $inNorm, $outNorm);
    }

    $conn->close();
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
    exit;
}

echo json_encode(['ok' => true]);
