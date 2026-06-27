<?php
/**
 * api/common.php — shared utilities for all API endpoints
 */

/** Maximum SQL payload length accepted by execute_sql (admin console). */
const API_SQL_MAX_LENGTH = 1048576;

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
 * Reject path strings with null bytes or directory traversal segments.
 */
function api_reject_unsafe_path(string $path, string $label = 'path'): void
{
    if ($path === '' || str_contains($path, "\0")) {
        api_error(400, "invalid $label");
    }
    if (preg_match('#(^|[/\\\\])\.\.([/\\\\]|$)#', $path)) {
        api_error(400, "invalid $label");
    }
}

/**
 * Escape a string for single-quoted SQL literals.
 */
function api_sql_quote(string $s): string
{
    return str_replace(["'", "\0"], ["''", ""], $s);
}

/**
 * Build php_openads connection options from a stored/session DD entry.
 *
 * DA-Web opens short-lived backend connections for most API calls, so the
 * selected local/remote mode has to travel with the session credentials.
 */
function api_ads_connect_opts(array $c): array
{
    $path = (string)($c['path'] ?? '');
    $connType = strtolower((string)($c['connType'] ?? 'local'));
    if ($connType !== 'remote') {
        $connType = 'local';
    }
    if ($connType === 'remote' && !preg_match('#^(tcp|tls)://#i', $path)) {
        $path = 'tcp://127.0.0.1:6262/' . $path;
    }

    $opts = ['path' => $path, 'server_type' => $connType, 'connType' => $connType];
    if (($c['username'] ?? '') !== '') {
        $opts['user'] = $c['username'];
    }
    if (($c['password'] ?? '') !== '') {
        $opts['password'] = $c['password'];
    }
    return $opts;
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
 * Validate an AOF (ad-hoc filter) expression before it is spliced into a
 * SQL WHERE clause.
 *
 * AOF (ad-hoc filter) is admin-only, but we still sanitize to prevent
 * injection via CSRF or a compromised session.
 *
 * Valid expressions are simple field-op-value predicates as produced by the
 * DA-Web filter UI, e.g.:
 *   Name = 'Smith'
 *   Age > 30
 *   (Age > 18) AND (Status = 'active')
 *   Name LIKE '%Smith%'
 *
 * Rejected inputs (calls api_error(400, ...) and exits):
 *   - Longer than 1024 characters
 *   - Contains a null byte
 *   - Contains semicolons (statement separators)
 *   - Contains SQL comment markers: -- or /* or *\/
 *   - Contains DML/DDL keywords as standalone words:
 *     INSERT, UPDATE, DELETE, DROP, CREATE, ALTER,
 *     EXEC, EXECUTE, UNION, TRUNCATE
 *
 * Note: this is a defence-in-depth block-list, not a full expression parser.
 * Complex valid expressions (subqueries, stored-proc calls) will also be
 * rejected, but DA-Web's filter UI never generates those.
 *
 * @param string $expr The stripped expression (leading WHERE already removed).
 */
function api_validate_aof_expression(string $expr): void
{
    // Length cap
    if (strlen($expr) > 1024) {
        api_error(400, 'invalid filter expression');
    }

    // Null byte
    if (strpos($expr, "\0") !== false) {
        api_error(400, 'invalid filter expression');
    }

    // Statement separator
    if (strpos($expr, ';') !== false) {
        api_error(400, 'invalid filter expression');
    }

    // SQL comment markers
    if (preg_match('/--|\/\*|\*\//', $expr)) {
        api_error(400, 'invalid filter expression');
    }

    // Dangerous DDL/DML keywords — word-boundary match, case-insensitive
    $keywords = [
        'INSERT', 'UPDATE', 'DELETE', 'DROP', 'CREATE', 'ALTER',
        'EXEC', 'EXECUTE', 'UNION', 'TRUNCATE',
    ];
    $pattern = '/\b(?:' . implode('|', $keywords) . ')\b/i';
    if (preg_match($pattern, $expr)) {
        api_error(400, 'invalid filter expression');
    }
}
