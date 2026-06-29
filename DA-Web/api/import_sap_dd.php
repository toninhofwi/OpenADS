<?php
/**
 * api/import_sap_dd.php - start an async SAP ADS DD import.
 *
 * RCB 06/29/2026: Long imports should not hold a FastCGI request open.  This
 * endpoint writes a job payload, starts a CLI worker, and returns a job id for
 * api/import_sap_dd_status.php polling.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';
require_once __DIR__ . '/import_job_lib.php';

api_require_session();

$body = json_decode(file_get_contents('php://input'), true) ?? [];
$name = trim($body['name'] ?? '');
$source = trim($body['source'] ?? '');
$dest = trim($body['dest'] ?? '');
$user = trim($body['user'] ?? '');
$sapLib = trim($body['sapLib'] ?? '');

if ($name === '' || $source === '' || $dest === '' || $user === '') {
    api_error(400, 'name, source, dest, and user are required');
}
api_validate_identifier($name, 'dictionary name');
api_reject_unsafe_path($source, 'source path');
api_reject_unsafe_path($dest, 'dest path');
if ($sapLib !== '') {
    api_reject_unsafe_path($sapLib, 'sapLib path');
}

try {
    $jobId = bin2hex(random_bytes(16));
    $now = date(DATE_ATOM);
    import_job_write($jobId, [
        'id' => $jobId,
        'status' => 'queued',
        'phase' => 'queued',
        'message' => 'Queued import',
        'createdAt' => $now,
        'updatedAt' => $now,
        'elapsed' => 0,
        'log' => [['at' => $now, 'message' => 'Queued import']],
    ]);
    file_put_contents(
        import_job_payload_path($jobId),
        json_encode($body, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES),
        LOCK_EX
    );

    $php = PHP_BINDIR . DIRECTORY_SEPARATOR . (PHP_OS_FAMILY === 'Windows' ? 'php.exe' : 'php');
    if (!is_file($php)) {
        $php = PHP_BINARY;
    }
    $worker = __DIR__ . DIRECTORY_SEPARATOR . 'import_sap_dd_worker.php';

    if (PHP_OS_FAMILY === 'Windows') {
        $cmd = 'start "" /B ' . escapeshellarg($php) . ' ' .
               '-n ' . escapeshellarg($worker) . ' ' . escapeshellarg($jobId);
        $h = popen($cmd, 'r');
        if ($h === false) {
            throw new RuntimeException('failed to start import worker');
        }
        pclose($h);
    } else {
        $cmd = escapeshellarg($php) . ' -n ' . escapeshellarg($worker) . ' ' .
               escapeshellarg($jobId) . ' > /dev/null 2>&1 &';
        exec($cmd);
    }

    echo json_encode([
        'ok' => true,
        'jobId' => $jobId,
        'status' => 'queued',
        'message' => 'Import started',
    ]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
