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
require_once __DIR__ . '/common.php';
require_once __DIR__ . '/openads_stubs.php';

$req          = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName       = trim($req['dd']           ?? '');
$type         = trim($req['type']         ?? '');
$name         = trim($req['name']         ?? '');
$newBody      = $req['body']              ?? '';
$newInParams  = trim($req['input_params'] ?? '');
$newOutParams = trim($req['output_params'] ?? '');
$newRetType   = trim($req['return_type']  ?? '');

if ($name === '' || !in_array($type, ['proc', 'function'], true)) {
    api_error(400, 'type (proc|function) and name are required');
}
api_validate_identifier($name, 'object name');

$c = api_require_connection($ddName);
$opts = api_ads_connect_opts($c);

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
    api_exception(500, $e);
}

echo json_encode(['ok' => true]);
