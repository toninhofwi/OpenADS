<?php
/**
 * api/common.php — shared utilities for all API endpoints
 */

/**
 * Emit a JSON error response and exit.
 * Always includes the engine error code when non-zero so callers
 * (DA-Web frontend or any other client) can branch on specific codes
 * like 5174 (SAP DD needs import) without the API layer special-casing them.
 */
function api_error(int $httpStatus, string $message, int $code = 0, array $extra = []): void
{
    http_response_code($httpStatus);
    $body = ['error' => $message];
    if ($code !== 0) {
        $body['code'] = $code;
    }
    if (!empty($extra)) {
        $body = array_merge($body, $extra);
    }
    echo json_encode($body);
    exit;
}

/**
 * Like api_error() but sources message and code directly from a Throwable.
 */
function api_exception(int $httpStatus, Throwable $e, array $extra = []): void
{
    api_error($httpStatus, $e->getMessage(), (int)$e->getCode(), $extra);
}
