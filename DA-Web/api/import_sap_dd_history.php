<?php
/**
 * api/import_sap_dd_history.php - recent SAP DD import warning history.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';
require_once __DIR__ . '/import_job_lib.php';

api_require_session();

function import_history_read_json(string $path): array
{
    $raw = @file_get_contents($path);
    $data = json_decode($raw === false ? '' : $raw, true);
    return is_array($data) ? $data : [];
}

try {
    $limit = (int)($_GET['limit'] ?? 20);
    if ($limit < 1) $limit = 20;
    if ($limit > 100) $limit = 100;

    $files = glob(import_jobs_dir() . DIRECTORY_SEPARATOR . '*.json') ?: [];
    $files = array_values(array_filter($files, static function (string $path): bool {
        return !str_ends_with($path, '.payload.json');
    }));
    usort($files, static fn($a, $b) => filemtime($b) <=> filemtime($a));

    $items = [];
    foreach ($files as $path) {
        if (count($items) >= $limit) break;
        $job = import_history_read_json($path);
        $jobId = (string)($job['id'] ?? basename($path, '.json'));
        if (!preg_match('/^[a-f0-9]{32}$/', $jobId)) continue;

        $payload = [];
        $payloadPath = import_job_payload_path($jobId);
        if (is_file($payloadPath)) {
            $payload = import_history_read_json($payloadPath);
        }

        $result = $job['result'] ?? [];
        if (!is_array($result)) $result = [];
        $warnings = $result['warnings'] ?? ($job['warnings'] ?? []);
        if (!is_array($warnings)) $warnings = [];
        $warnings = array_values(array_map(static fn($w) => (string)$w, $warnings));

        $items[] = [
            'id' => $jobId,
            'status' => (string)($job['status'] ?? ''),
            'phase' => (string)($job['phase'] ?? ''),
            'name' => (string)($payload['name'] ?? ''),
            'source' => (string)($payload['source'] ?? ''),
            'dest' => (string)($payload['dest'] ?? ''),
            'createdAt' => (string)($job['createdAt'] ?? ''),
            'finishedAt' => (string)($job['finishedAt'] ?? ''),
            'updatedAt' => (string)($job['updatedAt'] ?? ''),
            'message' => (string)($job['message'] ?? ''),
            'memberships' => $result['memberships'] ?? null,
            'permissions' => $result['permissions'] ?? null,
            'db_properties' => $result['db_properties'] ?? null,
            'warningCount' => count($warnings),
            'warnings' => $warnings,
        ];
    }

    echo json_encode(['items' => $items], JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE);
} catch (Throwable $e) {
    api_exception(500, $e);
}
