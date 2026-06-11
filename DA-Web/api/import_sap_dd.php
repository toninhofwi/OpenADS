<?php
/**
 * api/import_sap_dd.php — import a SAP ADS data dictionary into OpenADS format
 *
 * POST {
 *   name:     string  display name for the imported DD (registered in dictionaries.json)
 *   source:   string  path to the original SAP .add file (read-only)
 *   dest:     string  path for the new OpenADS copy
 *   user:     string  SAP administrator username
 *   password: string  SAP password (may be empty)
 *   sapLib:   string  (optional) explicit path to ace64.dll / libace64.so
 * }
 *
 * Locates openads_import_dd, runs it via proc_open (no shell injection risk),
 * parses the JSON result, and on success registers the destination DD in
 * config/dictionaries.json.
 *
 * The tool binary is found by trying, in order:
 *   1. OPENADS_IMPORT_DD_BIN environment variable
 *   2. <project-root>/bin/openads_import_dd[.exe]
 *   3. <project-root>/build/msvc-x64/tools/import_dd/Release/openads_import_dd.exe  (Win dev)
 *   4. <project-root>/build/msvc-x64/tools/import_dd/Debug/openads_import_dd.exe   (Win dev)
 *   5. <project-root>/build/ninja-linux/tools/import_dd/openads_import_dd           (Linux dev)
 *   6. openads_import_dd[.exe] in PATH
 */
header('Content-Type: application/json');
session_start();

// ── Input ─────────────────────────────────────────────────────────────────────
$body     = json_decode(file_get_contents('php://input'), true) ?? [];
$name     = trim($body['name']     ?? '');
$source   = trim($body['source']   ?? '');
$dest     = trim($body['dest']     ?? '');
$user     = trim($body['user']     ?? '');
$password = $body['password']      ?? '';
$sapLib   = trim($body['sapLib']   ?? '');

if ($name === '' || $source === '' || $dest === '' || $user === '') {
    http_response_code(400);
    echo json_encode(['error' => 'name, source, dest, and user are required']);
    exit;
}

// ── Find the import binary ────────────────────────────────────────────────────
$projectRoot = realpath(__DIR__ . '/../../');
$isWindows   = (PHP_OS_FAMILY === 'Windows');
$exeSuffix   = $isWindows ? '.exe' : '';

$candidates = array_filter([
    getenv('OPENADS_IMPORT_DD_BIN') ?: null,
    $projectRoot . DIRECTORY_SEPARATOR . 'bin' . DIRECTORY_SEPARATOR . 'openads_import_dd' . $exeSuffix,
    $projectRoot . '/build/msvc-x64/tools/import_dd/Release/openads_import_dd.exe',
    $projectRoot . '/build/msvc-x64/tools/import_dd/Debug/openads_import_dd.exe',
    $projectRoot . '/build/ninja-linux/tools/import_dd/openads_import_dd',
    'openads_import_dd' . $exeSuffix,  // PATH fallback
]);

$importBin = null;
foreach ($candidates as $c) {
    if ($c === 'openads_import_dd' . $exeSuffix) {
        // PATH fallback — assume available
        $importBin = $c;
        break;
    }
    if (is_file($c) && is_executable($c)) {
        $importBin = $c;
        break;
    }
}

if ($importBin === null) {
    http_response_code(500);
    echo json_encode([
        'error' => 'openads_import_dd binary not found. '
                 . 'Set the OPENADS_IMPORT_DD_BIN environment variable to its full path, '
                 . 'or copy it to ' . $projectRoot . '/bin/.',
    ]);
    exit;
}

// ── Build argument list (no shell — proc_open with array is injection-safe) ───
$cmd = [
    $importBin,
    '--source',   $source,
    '--dest',     $dest,
    '--user',     $user,
    '--password', $password,
];
if ($sapLib !== '') {
    $cmd[] = '--sap-lib';
    $cmd[] = $sapLib;
}

// ── Run the tool ──────────────────────────────────────────────────────────────
$descriptors = [
    0 => ['pipe', 'r'],   // stdin  (not used)
    1 => ['pipe', 'w'],   // stdout — JSON result
    2 => ['pipe', 'w'],   // stderr — captured but not forwarded
];

$proc = proc_open($cmd, $descriptors, $pipes);
if (!is_resource($proc)) {
    http_response_code(500);
    echo json_encode(['error' => 'Failed to launch openads_import_dd process']);
    exit;
}

fclose($pipes[0]);
$stdout = stream_get_contents($pipes[1]);
fclose($pipes[1]);
fclose($pipes[2]);
$exitCode = proc_close($proc);

// ── Parse tool output ─────────────────────────────────────────────────────────
$toolResult = json_decode($stdout, true);
if (!is_array($toolResult)) {
    http_response_code(500);
    echo json_encode([
        'error'     => 'openads_import_dd produced no parseable JSON output',
        'exit_code' => $exitCode,
        'raw'       => substr($stdout, 0, 500),
    ]);
    exit;
}

if (!($toolResult['ok'] ?? false)) {
    http_response_code(500);
    echo json_encode($toolResult);
    exit;
}

// ── Register the imported DD in dictionaries.json ─────────────────────────────
$configFile = __DIR__ . '/../config/dictionaries.json';
$raw        = file_exists($configFile) ? file_get_contents($configFile) : '[]';
$dicts      = json_decode($raw, true) ?? [];

$alreadyRegistered = false;
foreach ($dicts as $d) {
    if ($d['name'] === $name || $d['path'] === $dest) {
        $alreadyRegistered = true;
        break;
    }
}

if (!$alreadyRegistered) {
    $dicts[] = [
        'name'      => $name,
        'path'      => $dest,
        'username'  => $user,
        'connType'  => 'local',
        'entryType' => 'dd',
    ];
    file_put_contents(
        $configFile,
        json_encode(array_values($dicts), JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES)
    );
}

// ── Return combined result ────────────────────────────────────────────────────
echo json_encode(array_merge($toolResult, ['registered' => !$alreadyRegistered]));
