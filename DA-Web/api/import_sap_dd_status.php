<?php
/**
 * RCB 06/29/2026: Poll status for an async SAP DD import job.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';
require_once __DIR__ . '/import_job_lib.php';

api_require_session();

$jobId = trim($_GET['jobId'] ?? '');
try {
    import_job_assert_id($jobId);
    echo json_encode(import_job_read($jobId), JSON_UNESCAPED_SLASHES);
} catch (Throwable $e) {
    api_exception(404, $e);
}
