<?php
/**
 * api/common.php — shared utilities for all API endpoints
 */

/**
 * Emit a JSON error response and exit.
 * Always includes the engine error code when non-zero so callers
 * (DA-Web frontend or any other client) can branch on specific codes
 * like 5174 (DD needs import) without the API layer special-casing them.
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

/**
 * Require an active PHP session (blocks unauthenticated config mutation).
 */
function api_require_session(): void
{
    if (session_status() !== PHP_SESSION_ACTIVE) {
        api_error(401, 'session required');
    }
}

/**
 * Require a registered DD/free connection in the current session.
 *
 * @return array{path:string,username?:string,password?:string,entryType?:string}
 */
function api_require_connection(string $ddName): array
{
    api_require_session();
    if ($ddName === '') {
        api_error(400, 'dd is required');
    }
    if (!isset($_SESSION['connections'][$ddName])) {
        api_error(401, "Not connected to '$ddName'");
    }
    return $_SESSION['connections'][$ddName];
}

/**
 * Validate SQL object identifiers (table, index tag, RI name, etc.).
 */
function api_validate_identifier(string $name, string $label = 'identifier'): void
{
    if (!preg_match('/^[A-Za-z_][A-Za-z0-9_ ]*$/', $name)) {
        api_error(400, "invalid $label");
    }
}

/**
 * Resolve $candidate to an absolute path that exists and lies under $root.
 * Returns null when the path escapes the root, contains traversal, or is missing.
 */
function api_resolve_path_under_root(string $candidate, string $root): ?string
{
    if ($candidate === '' || $root === '') {
        return null;
    }
    if (str_contains($candidate, "\0")) {
        return null;
    }

    $rootReal = realpath($root);
    if ($rootReal === false) {
        return null;
    }

    $isAbs = (bool)preg_match('/^([a-zA-Z]:[\\\\\/]|\\\\\\\\|\/)/', $candidate);
    $combined = $isAbs
        ? $candidate
        : rtrim($rootReal, '/\\') . DIRECTORY_SEPARATOR . $candidate;

    if (preg_match('#(^|[/\\\\])\.\.([/\\\\]|$)#', $combined)) {
        return null;
    }

    $real = realpath($combined);
    if ($real === false) {
        return null;
    }

    $norm = strtolower(str_replace('\\', '/', $real));
    $rootNorm = strtolower(str_replace('\\', '/', $rootReal));
    $rootNormDir = rtrim($rootNorm, '/') . '/';
    if ($norm !== $rootNorm && !str_starts_with($norm, $rootNormDir)) {
        return null;
    }

    return $real;
}

/**
 * Escape a string for single-quoted SQL literals.
 */
function api_sql_quote(string $s): string
{
    return str_replace(["'", "\0"], ["''", ""], $s);
}
