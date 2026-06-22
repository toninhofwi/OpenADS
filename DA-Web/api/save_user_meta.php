<?php
/**
 * api/save_user_meta.php — save direct permission changes for a DD user.
 * POST { dd, user, rows: [{ object, type, parent, canSelect, canInsert, canUpdate, canDelete, canExec }] }
 *
 * Object type codes:
 *   1  = Table      → GRANT SELECT/INSERT/UPDATE/DELETE ON "obj" TO "user"
 *   4  = Field      → derived from table permissions; skipped
 *   6  = View       → GRANT SELECT/INSERT/UPDATE/DELETE ON "obj" TO "user"
 *   10 = StoredProc → GRANT EXECUTE ON "obj" TO "user"
 *   18 = Function   → GRANT EXECUTE ON "obj" TO "user"
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName = trim($body['dd']   ?? '');
$user   = trim($body['user'] ?? '');
$rows   = $body['rows'] ?? [];

if ($user === '' || !is_array($rows)) {
    api_error(400, 'user and rows are required');
}
if (str_contains($user, "\0")) {
    api_error(400, 'invalid user name');
}

$c = api_require_connection($ddName);
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

function perm_flag(mixed $v): bool {
    return strcasecmp((string)($v ?? ''), 'Yes') === 0 || (string)($v ?? '') === '1';
}

try {
    $conn  = AdsConnection::connect($opts);
    $saved = 0;
    $errs  = [];

    foreach ($rows as $row) {
        $obj  = trim($row['object'] ?? '');
        $type = (int)($row['type'] ?? 0);
        if ($obj === '') continue;

        // Field rows (type 4) are derived from table permissions; skip.
        if ($type === 4) continue;

        $isExecOnly = ($type === 10 || $type === 18);

        if ($isExecOnly) {
            $permMap = ['EXECUTE' => 'canExec'];
        } else {
            $permMap = [
                'SELECT'  => 'canSelect',
                'INSERT'  => 'canInsert',
                'UPDATE'  => 'canUpdate',
                'DELETE'  => 'canDelete',
            ];
        }

        $grants  = [];
        $revokes = [];
        foreach ($permMap as $perm => $field) {
            if (perm_flag($row[$field] ?? '')) {
                $grants[] = $perm;
            } else {
                $revokes[] = $perm;
            }
        }

        $qobj  = '"' . str_replace('"', '""', $obj)  . '"';
        $qusr  = '"' . str_replace('"', '""', $user) . '"';

        if (!empty($grants)) {
            $sql = 'GRANT ' . implode(', ', $grants) . " ON $qobj TO $qusr";
            try { $conn->execute($sql); $saved++; } catch (Throwable $e) { $errs[] = "GRANT on $obj: " . $e->getMessage(); }
        }
        if (!empty($revokes)) {
            $sql = 'REVOKE ' . implode(', ', $revokes) . " ON $qobj FROM $qusr";
            try { $conn->execute($sql); } catch (Throwable $e) { /* revoke of ungranted perm may fail silently */ }
        }
    }

    $conn->close();
    echo json_encode(['saved' => $saved, 'errors' => $errs]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
