<?php
/**
 * api/proc_body.php — return full stored-procedure or function SQL body.
 *
 * POST { dd, type, name }
 *   type = "proc" | "function"
 *
 * Returns { body, input_params, output_params, return_type }
 *
 * Uses the AdsDictionary property API — no binary file parsing.
 *   StoredProc: getProcProperty(name, 803)=body  800=input  801=output
 *   Function:   getFunctionProperty(name, 700)=body  701=input  702=return_type
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';
require_once __DIR__ . '/openads_stubs.php';

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName = trim($body['dd']   ?? '');
$type   = trim($body['type'] ?? '');
$name   = trim($body['name'] ?? '');

if ($name === '' || !in_array($type, ['proc', 'function'], true)) {
    api_error(400, 'type (proc|function) and name are required');
}
api_validate_identifier($name, 'object name');

$c = api_require_connection($ddName);
$opts = api_ads_connect_opts($c);

try {
    $conn = AdsConnection::connect($opts);
    $dict = AdsDictionary::fromConnection($conn);

    $sqlBody      = '';
    $inputParams  = '';
    $outputParams = '';
    $returnType   = '';

    if ($type === 'function') {
        $sqlBody     = $dict->getFunctionProperty($name, 700);
        $inputParams = $dict->getFunctionProperty($name, 701);
        $returnType  = $dict->getFunctionProperty($name, 702);
    } else {
        $sqlBody      = $dict->getProcProperty($name, 803);
        $inputParams  = $dict->getProcProperty($name, 800);
        $outputParams = $dict->getProcProperty($name, 801);
    }

    $sqlBody = rtrim($sqlBody);

    if ($sqlBody === '') {
        $paramParts = [];
        if ($inputParams  !== '') $paramParts[] = "IN:  {$inputParams}";
        if ($outputParams !== '') $paramParts[] = "OUT: {$outputParams}";
        $paramNote = $paramParts
            ? "\n-- Parameters:\n--   " . implode("\n--   ", $paramParts) : '';
        $sqlBody = "-- {$name}{$paramNote}\n-- Source is not available as SQL"
                 . " (built-in system procedure or body stored in server binary).";
    }

    $conn->close();
} catch (Throwable $e) {
    api_exception(500, $e);
}

$result = [
    'body'          => $sqlBody,
    'input_params'  => $inputParams,
    'output_params' => $outputParams,
    'return_type'   => $returnType,
];

$json = json_encode($result, JSON_UNESCAPED_UNICODE | JSON_INVALID_UTF8_SUBSTITUTE);
if ($json === false) {
    $result['body'] = preg_replace('/[\x80-\xFF]/', '', $result['body']);
    $json = json_encode($result, JSON_UNESCAPED_UNICODE);
}
echo $json;
