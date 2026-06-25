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
require_once __DIR__ . '/common.php';

api_require_session();

// ── Input ─────────────────────────────────────────────────────────────────────
$body     = json_decode(file_get_contents('php://input'), true) ?? [];
$name     = trim($body['name']     ?? '');
$source   = trim($body['source']   ?? '');
$dest     = trim($body['dest']     ?? '');
$user     = trim($body['user']     ?? '');
$password = $body['password']      ?? '';
$sapLib   = trim($body['sapLib']   ?? '');

if ($name === '' || $source === '' || $dest === '' || $user === '') {
    api_error(400, 'name, source, dest, and user are required');
}
api_validate_identifier($name, 'dictionary name');
api_reject_unsafe_path($source, 'source path');
api_reject_unsafe_path($dest, 'dest path');
if ($sapLib !== '') {
    api_reject_unsafe_path($sapLib, 'sapLib path');
}

$sourceReal = realpath($source);
if ($sourceReal === false || !is_file($sourceReal)) {
    api_error(400, 'source path does not exist or is not a file');
}
$destDir = dirname($dest);
if ($destDir !== '' && $destDir !== '.' && !is_dir($destDir)) {
    api_error(400, 'dest parent directory does not exist');
}
// Resolve sapLib: accept a directory (append ace64.dll) or a direct file path.
// When blank, probe well-known locations so the user can leave the field empty.
if ($sapLib !== '') {
    // If the user supplied a directory, append the DLL name.
    $dllName = (PHP_OS_FAMILY === 'Windows') ? 'ace64.dll' : 'libace64.so';
    if (is_dir($sapLib)) {
        $sapLib = rtrim($sapLib, '/\\') . DIRECTORY_SEPARATOR . $dllName;
    }
    $sapReal = realpath($sapLib);
    if ($sapReal === false || !is_file($sapReal)) {
        api_error(400, 'sapLib path does not exist or is not a file: ' . $sapLib);
    }
    $sapLib = $sapReal;
} else {
    // Auto-detect: check locations where SAP ACE / php_advantage is typically installed.
    $dllName = (PHP_OS_FAMILY === 'Windows') ? 'ace64.dll' : 'libace64.so';
    $probes = [
        'C:\\php\\' . $dllName,
        'C:\\ADS\\' . $dllName,
        'C:\\Program Files\\Advantage Database Server\\' . $dllName,
        'C:\\Program Files (x86)\\Advantage Database Server\\' . $dllName,
    ];
    foreach ($probes as $p) {
        if (is_file($p)) { $sapLib = $p; break; }
    }
    // If still not found, leave empty — import_dd will try its own search.
}
$source = $sourceReal;

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
    api_error(500, 'openads_import_dd binary not found. '
        . 'Set the OPENADS_IMPORT_DD_BIN environment variable to its full path, '
        . 'or copy it to ' . $projectRoot . '/bin/.');
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
    api_error(500, 'Failed to launch openads_import_dd process');
}

fclose($pipes[0]);
$stdout = stream_get_contents($pipes[1]);
fclose($pipes[1]);
fclose($pipes[2]);
$exitCode = proc_close($proc);

// ── Parse tool output ─────────────────────────────────────────────────────────
$toolResult = json_decode($stdout, true);
if (!is_array($toolResult)) {
    api_error(500, 'openads_import_dd produced no parseable JSON output', 0, [
        'exit_code' => $exitCode,
        'raw'       => substr($stdout, 0, 500),
    ]);
}

if (!($toolResult['ok'] ?? false)) {
    api_error(500, $toolResult['error'] ?? 'import failed', 0, $toolResult);
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
