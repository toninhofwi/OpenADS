<?php
/**
 * RCB 06/29/2026: CLI worker for async SAP DD imports.
 */

require_once __DIR__ . '/import_job_lib.php';

function worker_fail(string $jobId, string $message, array $extra = []): void
{
    import_job_update($jobId, array_merge([
        'status' => 'failed',
        'phase' => 'failed',
        'message' => $message,
        'finishedAt' => date(DATE_ATOM),
    ], $extra));
}

function worker_reject_unsafe_path(string $path, string $label): void
{
    if ($path === '' || str_contains($path, "\0")) {
        throw new RuntimeException("invalid $label");
    }
    if (preg_match('#(^|[/\\\\])\.\.([/\\\\]|$)#', $path)) {
        throw new RuntimeException("invalid $label");
    }
}

function worker_validate_identifier(string $name, string $label): void
{
    if (!preg_match('/^[A-Za-z_][A-Za-z0-9_ ]*$/', $name)) {
        throw new RuntimeException("invalid $label");
    }
}

function worker_find_import_binary(string $projectRoot): string
{
    $exeSuffix = PHP_OS_FAMILY === 'Windows' ? '.exe' : '';
    $candidates = array_filter([
        getenv('OPENADS_IMPORT_DD_BIN') ?: null,
        PHP_OS_FAMILY === 'Windows' ? 'C:\\php\\openads_import_dd.exe' : null,
        $projectRoot . DIRECTORY_SEPARATOR . 'bin' . DIRECTORY_SEPARATOR . 'openads_import_dd' . $exeSuffix,
        $projectRoot . '/build/msvc-x64/tools/import_dd/Release/openads_import_dd.exe',
        $projectRoot . '/build/msvc-x64/tools/import_dd/Debug/openads_import_dd.exe',
        $projectRoot . '/build/ninja-clang/tools/import_dd/openads_import_dd.exe',
        $projectRoot . '/build/ninja-linux/tools/import_dd/openads_import_dd',
        'openads_import_dd' . $exeSuffix,
    ]);
    $existing = [];
    foreach ($candidates as $c) {
        if ($c === 'openads_import_dd' . $exeSuffix) {
            continue;
        }
        if (is_file($c) && is_executable($c)) {
            $existing[] = $c;
        }
    }
    if ($existing) {
        // RCB 06/29/2026: DA-Web may have Debug, Release, and deployed copies.
        // Pick the newest explicit binary so a stale Release build cannot shadow
        // a freshly rebuilt importer during development.
        usort($existing, static fn($a, $b) => filemtime($b) <=> filemtime($a));
        return $existing[0];
    }
    if (in_array('openads_import_dd' . $exeSuffix, $candidates, true)) {
        return 'openads_import_dd' . $exeSuffix;
    }
    throw new RuntimeException('openads_import_dd binary not found');
}

function worker_resolve_sap_lib(string $sapLib): string
{
    if ($sapLib !== '') {
        $dllName = PHP_OS_FAMILY === 'Windows' ? 'ace64.dll' : 'libace64.so';
        if (is_dir($sapLib)) {
            $sapLib = rtrim($sapLib, '/\\') . DIRECTORY_SEPARATOR . $dllName;
        }
        $sapReal = realpath($sapLib);
        if ($sapReal === false || !is_file($sapReal)) {
            throw new RuntimeException('sapLib path does not exist or is not a file: ' . $sapLib);
        }
        return $sapReal;
    }

    $dllName = PHP_OS_FAMILY === 'Windows' ? 'ace64.dll' : 'libace64.so';
    foreach ([
        'C:\\php\\' . $dllName,
        'C:\\ADS\\' . $dllName,
        'C:\\Program Files\\Advantage Database Server\\' . $dllName,
        'C:\\Program Files (x86)\\Advantage Database Server\\' . $dllName,
    ] as $p) {
        if (is_file($p)) {
            return $p;
        }
    }
    return '';
}

function worker_register_dictionary(string $name, string $dest, string $user): bool
{
    $configFile = __DIR__ . '/../config/dictionaries.json';
    $raw = file_exists($configFile) ? file_get_contents($configFile) : '[]';
    $dicts = json_decode($raw === false ? '[]' : $raw, true);
    if (!is_array($dicts)) {
        $dicts = [];
    }

    foreach ($dicts as $d) {
        if (($d['name'] ?? '') === $name || ($d['path'] ?? '') === $dest) {
            return false;
        }
    }

    $dicts[] = [
        'name' => $name,
        'path' => $dest,
        'username' => $user,
        'connType' => 'local',
        'entryType' => 'dd',
    ];
    file_put_contents(
        $configFile,
        json_encode(array_values($dicts), JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES),
        LOCK_EX
    );
    return true;
}

function worker_run_import(string $jobId, array $body): void
{
    $name     = trim($body['name']     ?? '');
    $source   = trim($body['source']   ?? '');
    $dest     = trim($body['dest']     ?? '');
    $user     = trim($body['user']     ?? '');
    $password = (string)($body['password'] ?? '');
    $sapLib   = trim($body['sapLib']   ?? '');

    if ($name === '' || $source === '' || $dest === '' || $user === '') {
        throw new RuntimeException('name, source, dest, and user are required');
    }
    worker_validate_identifier($name, 'dictionary name');
    worker_reject_unsafe_path($source, 'source path');
    worker_reject_unsafe_path($dest, 'dest path');
    if ($sapLib !== '') {
        worker_reject_unsafe_path($sapLib, 'sapLib path');
    }

    import_job_append_log($jobId, 'Validating import paths');
    $sourceReal = realpath($source);
    if ($sourceReal === false || !is_file($sourceReal)) {
        throw new RuntimeException('source path does not exist or is not a file');
    }
    $destDir = dirname($dest);
    if ($destDir !== '' && $destDir !== '.' && !is_dir($destDir)) {
        throw new RuntimeException('dest parent directory does not exist');
    }

    import_job_append_log($jobId, 'Locating SAP ACE runtime');
    $sapLib = worker_resolve_sap_lib($sapLib);
    $projectRoot = realpath(__DIR__ . '/../../');
    if ($projectRoot === false) {
        throw new RuntimeException('project root not found');
    }

    import_job_append_log($jobId, 'Locating OpenADS import tool');
    $importBin = worker_find_import_binary($projectRoot);

    $cmd = [
        $importBin,
        '--source', $sourceReal,
        '--dest', $dest,
        '--user', $user,
        '--password', $password,
    ];
    if ($sapLib !== '') {
        $cmd[] = '--sap-lib';
        $cmd[] = $sapLib;
    }

    import_job_update($jobId, [
        'status' => 'running',
        'phase' => 'native_import',
        'message' => 'Running native SAP DD importer',
        'tool' => $importBin,
    ]);

    $descriptors = [
        0 => ['pipe', 'r'],
        1 => ['pipe', 'w'],
        2 => ['pipe', 'w'],
    ];
    $proc = proc_open($cmd, $descriptors, $pipes);
    if (!is_resource($proc)) {
        throw new RuntimeException('Failed to launch openads_import_dd process');
    }
    fclose($pipes[0]);
    stream_set_blocking($pipes[1], false);
    stream_set_blocking($pipes[2], false);

    $stdout = '';
    $stderr = '';
    $started = time();
    $last = 0;
    $exitCode = null;
    while (true) {
        $stdout .= stream_get_contents($pipes[1]);
        $stderr .= stream_get_contents($pipes[2]);
        $status = proc_get_status($proc);
        if (!$status['running']) {
            $exitCode = $status['exitcode'];
            break;
        }
        if (time() - $last >= 5) {
            $elapsed = time() - $started;
            import_job_update($jobId, [
                'elapsed' => $elapsed,
                'message' => 'Native importer still running (' . $elapsed . 's elapsed)',
            ]);
            $last = time();
        }
        usleep(200000);
    }
    $stdout .= stream_get_contents($pipes[1]);
    $stderr .= stream_get_contents($pipes[2]);
    fclose($pipes[1]);
    fclose($pipes[2]);
    $closeCode = proc_close($proc);
    if ($exitCode === null || $exitCode < 0) {
        $exitCode = $closeCode;
    }

    import_job_append_log($jobId, 'Parsing import result');
    $toolResult = json_decode($stdout, true);
    if (!is_array($toolResult)) {
        throw new RuntimeException('openads_import_dd produced no parseable JSON output');
    }
    if (!($toolResult['ok'] ?? false)) {
        worker_fail($jobId, $toolResult['error'] ?? 'import failed', $toolResult);
        return;
    }

    import_job_append_log($jobId, 'Registering imported dictionary');
    $registered = worker_register_dictionary($name, $dest, $user);
    $result = array_merge($toolResult, ['registered' => $registered]);
    import_job_update($jobId, [
        'status' => 'complete',
        'phase' => 'complete',
        'message' => 'Import complete',
        'finishedAt' => date(DATE_ATOM),
        'result' => $result,
        'elapsed' => time() - $started,
        'exit_code' => $exitCode,
        'stderr' => substr($stderr, 0, 2000),
    ]);
}

try {
    if (PHP_SAPI !== 'cli') {
        throw new RuntimeException('worker must run from CLI');
    }
    $jobId = $argv[1] ?? '';
    import_job_assert_id($jobId);
    $payloadRaw = file_get_contents(import_job_payload_path($jobId));
    $payload = json_decode($payloadRaw === false ? '' : $payloadRaw, true);
    if (!is_array($payload)) {
        throw new RuntimeException('import payload is corrupt');
    }
    import_job_update($jobId, [
        'status' => 'running',
        'phase' => 'starting',
        'message' => 'Starting import worker',
        'startedAt' => date(DATE_ATOM),
    ]);
    worker_run_import($jobId, $payload);
} catch (Throwable $e) {
    $jobId = $jobId ?? '';
    if (is_string($jobId) && preg_match('/^[a-f0-9]{32}$/', $jobId)) {
        worker_fail($jobId, $e->getMessage(), ['code' => (int)$e->getCode()]);
    }
    fwrite(STDERR, $e->getMessage() . PHP_EOL);
    exit(1);
}
