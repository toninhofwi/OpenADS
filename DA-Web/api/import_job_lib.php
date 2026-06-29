<?php
/**
 * RCB 06/29/2026: Small file-backed job store for long-running SAP DD imports.
 * The browser starts a job, a CLI PHP worker updates this status file, and the
 * UI polls it without holding an Apache FastCGI request open for minutes.
 */

function import_jobs_dir(): string
{
    $dir = rtrim(sys_get_temp_dir(), DIRECTORY_SEPARATOR) . DIRECTORY_SEPARATOR . 'openads-import-jobs';
    if (!is_dir($dir)) {
        mkdir($dir, 0777, true);
    }
    return $dir;
}

function import_job_assert_id(string $jobId): string
{
    if (!preg_match('/^[a-f0-9]{32}$/', $jobId)) {
        throw new RuntimeException('invalid import job id');
    }
    return $jobId;
}

function import_job_path(string $jobId, string $suffix = '.json'): string
{
    return import_jobs_dir() . DIRECTORY_SEPARATOR . import_job_assert_id($jobId) . $suffix;
}

function import_job_read(string $jobId): array
{
    $path = import_job_path($jobId);
    if (!is_file($path)) {
        throw new RuntimeException('import job not found');
    }
    $raw = file_get_contents($path);
    $data = json_decode($raw === false ? '' : $raw, true);
    if (!is_array($data)) {
        throw new RuntimeException('import job status is corrupt');
    }
    return $data;
}

function import_job_write(string $jobId, array $data): void
{
    $data['updatedAt'] = date(DATE_ATOM);
    file_put_contents(
        import_job_path($jobId),
        json_encode($data, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES),
        LOCK_EX
    );
}

function import_job_update(string $jobId, array $patch): array
{
    $data = import_job_read($jobId);
    $data = array_merge($data, $patch);
    import_job_write($jobId, $data);
    return $data;
}

function import_job_append_log(string $jobId, string $message): void
{
    $data = import_job_read($jobId);
    $log = $data['log'] ?? [];
    if (!is_array($log)) {
        $log = [];
    }
    $log[] = ['at' => date(DATE_ATOM), 'message' => $message];
    if (count($log) > 80) {
        $log = array_slice($log, -80);
    }
    $data['log'] = $log;
    $data['message'] = $message;
    import_job_write($jobId, $data);
}

function import_job_payload_path(string $jobId): string
{
    return import_job_path($jobId, '.payload.json');
}
