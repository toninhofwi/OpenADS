<?php
/**
 * api/save_group_meta.php — save permission changes for a DD group.
 * POST { dd, group, rows: [{ object, type, parent, canSelect, canInsert, canUpdate, canDelete, canExec }] }
 *
 * Object type codes:
 *   1  = Table      → GRANT SELECT/INSERT/UPDATE/DELETE/EXECUTE ON "obj" TO "group"
 *   4  = Field      → derived from table permissions; skipped (not independently saveable)
 *   6  = View       → GRANT SELECT/INSERT/UPDATE/DELETE/EXECUTE ON "obj" TO "group"
 *   10 = StoredProc → GRANT EXECUTE ON "obj" TO "group"
 *   18 = Function   → GRANT EXECUTE ON "obj" TO "group"
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName = trim($body['dd']    ?? '');
$group  = trim($body['group'] ?? '');
$rows   = $body['rows'] ?? [];

if ($group === '' || !is_array($rows)) {
    api_error(400, 'group and rows are required');
}
if (str_contains($group, "\0")) {
    api_error(400, 'invalid group name');
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

        // Type 4 (Field): field permissions are derived from their parent table.
        // Changing individual field permissions requires table-level grants.
        // Skip these rows — the user should edit the parent table row instead.
        if ($type === 4) continue;

        // Type 10 (Stored Proc) and 18 (Function): EXECUTE only.
        $isExecOnly = ($type === 10 || $type === 18);

        if ($isExecOnly) {
            $permMap = ['EXECUTE' => 'canExec'];
        } else {
            // Tables (1) and Views (6): DML permissions.
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

        $qobj = '"' . str_replace('"', '""', $obj) . '"';
        $qgrp = '"' . str_replace('"', '""', $group) . '"';

        if (!empty($grants)) {
            $sql = 'GRANT ' . implode(', ', $grants) . " ON $qobj TO $qgrp";
            try { $conn->execute($sql); $saved++; } catch (Throwable $e) { $errs[] = "GRANT on $obj: " . $e->getMessage(); }
        }
        if (!empty($revokes)) {
            $sql = 'REVOKE ' . implode(', ', $revokes) . " ON $qobj FROM $qgrp";
            try { $conn->execute($sql); } catch (Throwable $e) { /* revoke of ungranted perm may fail silently */ }
        }
    }

    $conn->close();
    echo json_encode(['saved' => $saved, 'errors' => $errs]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
