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
