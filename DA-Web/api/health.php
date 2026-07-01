<?php
/**
 * api/health.php - DD health checks for administrator review.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';
require_once __DIR__ . '/openads_stubs.php';

$ddName = trim($_GET['dd'] ?? '');
$c = api_require_connection($ddName);
$opts = api_ads_connect_opts($c);
$isRemote = strtolower((string)($c['connType'] ?? 'local')) === 'remote';
$rootPath = (string)($c['path'] ?? '');
$rootDir = $rootPath;
if (!$isRemote && preg_match('/\.(add|am)$/i', $rootPath)) {
    $rootDir = dirname($rootPath);
}

function health_rows(AdsConnection $conn, string $sql): array
{
    $stmt = $conn->query($sql);
    $rows = [];
    while ($row = $stmt->fetchAssoc()) {
        $rows[] = $row;
    }
    $stmt->close();
    return $rows;
}

function health_value(array $row, array $names): string
{
    $ciRow = [];
    foreach ($row as $key => $value) {
        $ciRow[strtolower((string)$key)] = $value;
    }
    foreach ($names as $name) {
        if (array_key_exists($name, $row)) return trim((string)$row[$name]);
        $key = strtolower($name);
        if (array_key_exists($key, $ciRow)) return trim((string)$ciRow[$key]);
    }
    return '';
}

function health_add(array &$checks, string $severity, string $area,
                    string $object, string $message, string $detail = ''): void
{
    $checks[] = [
        'severity' => $severity,
        'area' => $area,
        'object' => $object,
        'message' => $message,
        'detail' => $detail,
    ];
}

function health_ci(string $s): string
{
    return strtolower($s);
}

function health_split_index_expression(string $expr): ?array
{
    $expr = trim($expr);
    if ($expr === '') return [];

    $parts = preg_split('/[;+,\s]+/', $expr, -1, PREG_SPLIT_NO_EMPTY);
    if ($parts === false) return null;

    $fields = [];
    foreach ($parts as $part) {
        $part = trim($part, " \t\r\n'\"[]");
        if ($part === '') continue;
        if (!preg_match('/^[A-Za-z_][A-Za-z0-9_]*$/', $part)) return null;
        $fields[] = $part;
    }
    return $fields;
}

function health_field_signature(array $field): string
{
    return strtoupper((string)($field['type'] ?? '')) . ':'
        . (int)($field['len'] ?? 0) . ':'
        . (int)($field['dec'] ?? 0);
}

try {
    $perf = api_perf_start();
    $conn = AdsConnection::connect($opts);
    $dict = AdsDictionary::fromConnection($conn);

    $checks = [];
    $tables = [];
    $tableFields = [];
    $tableTags = [];
    $objects = ['database' => ['database' => true]];
    $users = [];
    $groups = [];

    foreach (health_rows($conn, 'SELECT * FROM system.tables') as $row) {
        $name = health_value($row, ['NAME', 'TABLE_NAME']);
        if ($name === '') continue;
        $path = health_value($row, ['TABLE_RELATIVE_PATH', 'TABLE_PATH', 'PATH']);
        $tables[health_ci($name)] = ['name' => $name, 'path' => $path];
        $objects[health_ci($name)] = true;
    }
    api_perf_mark($perf, 'tables');

    foreach (health_rows($conn, 'SELECT TABLE_NAME, COL_NAME, COL_TYPE, COL_LEN, COL_DEC FROM system.columns') as $row) {
        $table = health_value($row, ['TABLE_NAME']);
        $field = health_value($row, ['COL_NAME']);
        if ($table === '' || $field === '') continue;
        $tableFields[health_ci($table)][health_ci($field)] = [
            'name' => $field,
            'type' => health_value($row, ['COL_TYPE']),
            'len' => (int)health_value($row, ['COL_LEN']),
            'dec' => (int)health_value($row, ['COL_DEC']),
        ];
    }
    api_perf_mark($perf, 'fields');

    $relations = health_rows($conn, 'SELECT * FROM system.relations');
    $riTableKeys = [];
    foreach ($relations as $row) {
        $parent = health_value($row, ['PARENT', 'PARENT_TABLE']);
        $child = health_value($row, ['CHILD', 'CHILD_TABLE']);
        if ($parent !== '') $riTableKeys[health_ci($parent)] = true;
        if ($child !== '') $riTableKeys[health_ci($child)] = true;
    }
    api_perf_mark($perf, 'relations');

    foreach ($riTableKeys as $tableKey => $_) {
        if (!isset($tables[$tableKey])) continue;
        $entry = $tables[$tableKey];
        try {
            $tbl = AdsTable::open($conn, $entry['name'], 0);
            foreach ($tbl->getIndexTags() as $tag) {
                $tagName = trim((string)($tag['tag'] ?? ''));
                if ($tagName === '') continue;
                $tableTags[$tableKey][health_ci($tagName)] = [
                    'name' => $tagName,
                    'expression' => trim((string)($tag['expression'] ?? '')),
                ];
            }
            $tbl->close();
        } catch (Throwable $e) {
            health_add($checks, 'warning', 'Indexes', $entry['name'],
                'Unable to open table to inspect index tags.', $e->getMessage());
        }
    }
    api_perf_mark($perf, 'tags');

    foreach (health_rows($conn, 'SELECT VIEW_NAME FROM system.views') as $row) {
        $name = health_value($row, ['VIEW_NAME']);
        if ($name !== '') $objects[health_ci($name)] = true;
    }
    foreach (health_rows($conn, 'SELECT PROC_NAME FROM system.storedprocedures') as $row) {
        $name = health_value($row, ['PROC_NAME']);
        if ($name !== '') $objects[health_ci($name)] = true;
    }
    foreach (health_rows($conn, 'SELECT FUNC_NAME FROM system.functions') as $row) {
        $name = health_value($row, ['FUNC_NAME']);
        if ($name !== '') $objects[health_ci($name)] = true;
    }
    api_perf_mark($perf, 'objects');

    foreach (health_rows($conn, 'SELECT USER_NAME FROM system.users') as $row) {
        $name = health_value($row, ['USER_NAME']);
        if ($name !== '') $users[health_ci($name)][] = $name;
    }
    foreach (health_rows($conn, 'SELECT GROUP_NAME FROM system.usergroups') as $row) {
        $name = health_value($row, ['GROUP_NAME']);
        if ($name !== '') $groups[health_ci($name)][] = $name;
    }
    foreach (['DB:Admin', 'DB:Backup', 'DB:Debug', 'DB:Public'] as $builtin) {
        $groups[health_ci($builtin)][] = $builtin;
    }
    foreach ($users as $ci => $names) {
        $unique = array_values(array_unique($names));
        if (count($unique) > 1) {
            health_add($checks, 'warning', 'Users', implode(', ', $unique),
                'Mixed-case duplicate user names compare equal case-insensitively.');
        }
    }
    foreach ($groups as $ci => $names) {
        $unique = array_values(array_unique($names));
        if (count($unique) > 1) {
            health_add($checks, 'warning', 'Groups', implode(', ', $unique),
                'Mixed-case duplicate group names compare equal case-insensitively.');
        }
    }
    api_perf_mark($perf, 'principals');

    if ($isRemote) {
        health_add($checks, 'info', 'Files', $ddName,
            'Filesystem checks skipped for remote DD connections.',
            'Server-side paths are not guaranteed to exist on the PHP host.');
    } else {
        foreach ($tables as $entry) {
            $path = $entry['path'];
            if ($path === '') continue;
            $real = api_resolve_path_under_root($path, $rootDir);
            if ($real === null) {
                health_add($checks, 'error', 'Tables', $entry['name'],
                    'Registered table file is missing or outside the DD root.', $path);
            }
        }

        foreach (health_rows($conn, 'SELECT TABLE_NAME, INDEX_FILE FROM system.indexes') as $row) {
            $table = health_value($row, ['TABLE_NAME']);
            $idx = health_value($row, ['INDEX_FILE']);
            if ($idx === '') continue;
            $real = api_resolve_path_under_root($idx, $rootDir);
            if ($real === null) {
                health_add($checks, 'error', 'Indexes', $table,
                    'Registered index file is missing or outside the DD root.', $idx);
            }
        }

        $tempPath = '';
        try { $tempPath = trim((string)$dict->getDatabaseProperty(12)); } catch (Throwable) {}
        if ($tempPath !== '') {
            $realTemp = realpath($tempPath);
            if ($realTemp === false || !is_dir($realTemp)) {
                health_add($checks, 'warning', 'Database', 'TEMP_TABLE_PATH',
                    'Temp table path does not exist or is not a directory.', $tempPath);
            }
        }
    }
    api_perf_mark($perf, 'files');

    foreach ($relations as $row) {
        $name = health_value($row, ['RI_NAME']);
        $parent = health_value($row, ['PARENT', 'PARENT_TABLE']);
        $child = health_value($row, ['CHILD', 'CHILD_TABLE']);
        $parentTag = health_value($row, ['PARENT_TAG']);
        $childTag = health_value($row, ['CHILD_TAG']);
        $parentKey = health_ci($parent);
        $childKey = health_ci($child);
        $parentFields = null;
        $childFields = null;

        if ($parent !== '' && !isset($tables[health_ci($parent)])) {
            health_add($checks, 'error', 'RI', $name,
                'RI parent table is not registered in the DD.', $parent);
        }
        if ($child !== '' && !isset($tables[health_ci($child)])) {
            health_add($checks, 'error', 'RI', $name,
                'RI child table is not registered in the DD.', $child);
        }

        if ($parent !== '' && isset($tables[$parentKey])) {
            if ($parentTag === '') {
                health_add($checks, 'error', 'RI', $name,
                    'RI parent tag is blank.', $parent);
            } elseif (!isset($tableTags[$parentKey][health_ci($parentTag)])) {
                health_add($checks, 'error', 'RI', $name,
                    'RI parent tag is not present on the parent table.', $parent . '.' . $parentTag);
            } else {
                $expr = $tableTags[$parentKey][health_ci($parentTag)]['expression'];
                $parentFields = health_split_index_expression($expr);
                if ($parentFields === null) {
                    health_add($checks, 'info', 'RI', $name,
                        'RI parent tag uses a calculated expression that was not field-validated.',
                        $parent . '.' . $parentTag . ': ' . $expr);
                } else {
                    foreach ($parentFields as $field) {
                        if (!isset($tableFields[$parentKey][health_ci($field)])) {
                            health_add($checks, 'error', 'RI', $name,
                                'RI parent tag references a field that is not registered on the parent table.',
                                $parent . '.' . $field);
                        }
                    }
                }
            }
        }

        if ($child !== '' && isset($tables[$childKey])) {
            if ($childTag === '') {
                health_add($checks, 'error', 'RI', $name,
                    'RI child tag is blank.', $child);
            } elseif (!isset($tableTags[$childKey][health_ci($childTag)])) {
                health_add($checks, 'error', 'RI', $name,
                    'RI child tag is not present on the child table.', $child . '.' . $childTag);
            } else {
                $expr = $tableTags[$childKey][health_ci($childTag)]['expression'];
                $childFields = health_split_index_expression($expr);
                if ($childFields === null) {
                    health_add($checks, 'info', 'RI', $name,
                        'RI child tag uses a calculated expression that was not field-validated.',
                        $child . '.' . $childTag . ': ' . $expr);
                } else {
                    foreach ($childFields as $field) {
                        if (!isset($tableFields[$childKey][health_ci($field)])) {
                            health_add($checks, 'error', 'RI', $name,
                                'RI child tag references a field that is not registered on the child table.',
                                $child . '.' . $field);
                        }
                    }
                }
            }
        }

        if (is_array($parentFields) && is_array($childFields)) {
            if (count($parentFields) !== count($childFields)) {
                health_add($checks, 'error', 'RI', $name,
                    'RI parent and child tags do not contain the same number of fields.',
                    $parentTag . ' has ' . count($parentFields) . '; ' . $childTag . ' has ' . count($childFields));
            } else {
                foreach ($parentFields as $i => $parentField) {
                    $childField = $childFields[$i] ?? '';
                    $pf = $tableFields[$parentKey][health_ci($parentField)] ?? null;
                    $cf = $tableFields[$childKey][health_ci($childField)] ?? null;
                    if ($pf === null || $cf === null) continue;
                    if (health_field_signature($pf) !== health_field_signature($cf)) {
                        health_add($checks, 'warning', 'RI', $name,
                            'RI parent and child fields differ in type, length, or decimals.',
                            $parent . '.' . $pf['name'] . ' (' . health_field_signature($pf) . ') vs '
                                . $child . '.' . $cf['name'] . ' (' . health_field_signature($cf) . ')');
                    }
                }
            }
        }
    }

    foreach (health_rows($conn, 'SELECT TRIG_NAME, TABLE_NAME FROM system.triggers') as $row) {
        $name = health_value($row, ['TRIG_NAME']);
        $table = health_value($row, ['TABLE_NAME']);
        if ($table !== '' && !isset($tables[health_ci($table)])) {
            health_add($checks, 'error', 'Triggers', $name,
                'Trigger references a table that is not registered in the DD.', $table);
        }
    }
    api_perf_mark($perf, 'dependencies');

    foreach (health_rows($conn, 'SELECT * FROM system.permission_issues') as $row) {
        health_add(
            $checks,
            health_value($row, ['SEVERITY']) ?: 'warning',
            health_value($row, ['AREA']) ?: 'Permissions',
            health_value($row, ['OBJECT']),
            health_value($row, ['MESSAGE']),
            health_value($row, ['DETAIL'])
        );
    }
    api_perf_mark($perf, 'permissions');

    $summary = ['error' => 0, 'warning' => 0, 'info' => 0, 'ok' => 0];
    foreach ($checks as $check) {
        $sev = $check['severity'];
        if (!isset($summary[$sev])) $summary[$sev] = 0;
        $summary[$sev]++;
    }
    if (empty($checks)) {
        health_add($checks, 'ok', 'Dictionary', $ddName,
            'No issues found by the current health checks.');
        $summary['ok'] = 1;
    }

    $conn->close();
    echo json_encode([
        'dd' => $ddName,
        'remote' => $isRemote,
        'summary' => $summary,
        'checks' => $checks,
        'perf' => api_perf_finish($perf),
    ]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
