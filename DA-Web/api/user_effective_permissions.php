<?php
/**
 * api/user_effective_permissions.php — effective permissions for a DD user.
 * GET ?dd=&user=
 *
 * Effective permissions = direct grants to the user
 *                         UNION (if canInherit) grants to each group the user belongs to.
 *
 * Returns {
 *   data: [ row, … ],
 *   canInherit: bool,
 *   groups: [ "Group1", … ]   // groups whose permissions were merged in
 * }
 *
 * Each row:
 *   { object, type, parent,
 *     canSelect, canInsert, canUpdate, canDelete, canExec, canAlter, canDrop,
 *     srcSelect, srcInsert, srcUpdate, srcDelete, srcExec, srcAlter, srcDrop }
 *
 * srcXxx values:
 *   ""               — not granted by anyone
 *   "Direct"         — directly granted to the user
 *   "GroupName"      — granted via that group
 *   "Direct+Group"   — both
 *   "Group1+Group2"  — multiple groups
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$ddName   = trim($_GET['dd']   ?? '');
$userName = trim($_GET['user'] ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '' || $userName === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd and user are required']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = api_ads_connect_opts($c);

try {
    $conn = AdsConnection::connect($opts);

    $PERM_COLS = ['SELECT','INSERT','UPDATE','DELETE','EXECUTE','ALTER','DROP'];

    // ── Step 1: get the user's groups ────────────────────────────────────────────
    $userGroups  = [];
    try {
        $stmt = $conn->query("SELECT GROUP_NAME, USER_NAME FROM system.usergroupmembers");
        while ($row = $stmt->fetchAssoc()) {
            $u = trim((string)($row['USER_NAME'] ?? ''));
            if (strcasecmp($u, $userName) !== 0) continue;
            $g = trim((string)($row['GROUP_NAME'] ?? ''));
            if ($g !== '') $userGroups[] = $g;
        }
        $stmt->close();
    } catch (Throwable $e) {}

    // ── Step 2: single pass over system.permissions (filtered to user + groups) ─────
    // Collect rows for the user and all their groups in one filtered scan.
    // permMaps[grantee_lc][key] = permission row
    $userLc      = strtolower($userName);
    $groupsLcSet = [];
    foreach ($userGroups as $g) $groupsLcSet[strtolower($g)] = $g;

    $permMaps   = [];   // lc_grantee => [ key => row ]
    $canInherit = false;

    // Scan and filter in PHP so imported mixed-case group names still match
    // memberships such as General/general or DB:Admin/db:Admin.
    $stmt = $conn->query("SELECT * FROM system.permissions");
    while ($row = $stmt->fetchAssoc()) {
        $granteeLc = strtolower(trim((string)($row['GRANTEE'] ?? '')));
        $isUser    = ($granteeLc === $userLc);
        $isGroup   = isset($groupsLcSet[$granteeLc]);
        if (!$isUser && !$isGroup) continue;

        $type   = (string)(int)($row['OBJ_TYPE'] ?? 0);
        $name   = strtolower(trim((string)($row['OBJ_NAME'] ?? '')));
        $parent = strtolower(trim((string)($row['PARENT']   ?? '')));
        $key    = $type === '4' ? "4\0{$parent}\0{$name}" : "{$type}\0{$name}";

        if (!isset($permMaps[$granteeLc])) $permMaps[$granteeLc] = [];
        $permMaps[$granteeLc][$key] = $row;

        if ($isUser && !$canInherit && ($row['INHERIT'] ?? '0') === '1') {
            $canInherit = true;
        }
    }
    $stmt->close();
    $canInherit = $canInherit || count($userGroups) > 0;

    $directMap = $permMaps[$userLc] ?? [];

    // ── Step 3: merge helper — per-key compute sources ────────────────────────────
    $effectiveSources = function (string $key) use (
        $directMap, $permMaps, $groupsLcSet, $PERM_COLS, $canInherit
    ): array {
        $sources = array_fill_keys($PERM_COLS, []);
        // Direct grants
        if (isset($directMap[$key])) {
            $p = $directMap[$key];
            foreach ($PERM_COLS as $col) {
                if (($p[$col] ?? '0') !== '0') $sources[$col][] = 'Direct';
            }
        }
        // Inherited group grants (only when canInherit is true)
        if ($canInherit) {
            foreach ($groupsLcSet as $glc => $gname) {
                if (!isset($permMaps[$glc][$key])) continue;
                $p = $permMaps[$glc][$key];
                foreach ($PERM_COLS as $col) {
                    if (($p[$col] ?? '0') !== '0') $sources[$col][] = $gname;
                }
            }
        }
        return $sources;
    };

    // ── Step 4: build output rows ─────────────────────────────────────────────────
    $buildRow = function (string $name, string $type, string $parent)
                use ($effectiveSources): array {
        $key  = $type === '4' ? "4\0" . strtolower($parent) . "\0" . strtolower($name)
                              : "{$type}\0" . strtolower($name);
        $srcs = $effectiveSources($key);
        $yn   = fn(array $s): string => count($s) > 0 ? 'Yes' : 'No';
        $src  = fn(array $s): string => implode('+', $s);
        return [
            'object'    => $name,
            'type'      => (int)$type,
            'parent'    => $parent,
            'canSelect' => $yn($srcs['SELECT']),
            'canInsert' => $yn($srcs['INSERT']),
            'canUpdate' => $yn($srcs['UPDATE']),
            'canDelete' => $yn($srcs['DELETE']),
            'canExec'   => $yn($srcs['EXECUTE']),
            'canAlter'  => $yn($srcs['ALTER']),
            'canDrop'   => $yn($srcs['DROP']),
            'srcSelect' => $src($srcs['SELECT']),
            'srcInsert' => $src($srcs['INSERT']),
            'srcUpdate' => $src($srcs['UPDATE']),
            'srcDelete' => $src($srcs['DELETE']),
            'srcExec'   => $src($srcs['EXECUTE']),
            'srcAlter'  => $src($srcs['ALTER']),
            'srcDrop'   => $src($srcs['DROP']),
        ];
    };

    // Collect all field names seen across user + group permission maps
    $fieldsByTable = [];
    foreach ($permMaps as $pm) {
        foreach ($pm as $key => $p) {
            if ((string)($p['OBJ_TYPE'] ?? '') !== '4') continue;
            $tbl = strtolower(trim((string)($p['PARENT']   ?? '')));
            $fn  = trim((string)($p['OBJ_NAME'] ?? ''));
            if ($tbl !== '' && $fn !== '') {
                $fieldsByTable[$tbl][strtolower($fn)] = $fn;
            }
        }
    }

    $rows = [];

    // Tables
    $stmt = $conn->query('SELECT Name FROM system.tables ORDER BY Name');
    while ($row = $stmt->fetchAssoc()) {
        $tbl    = trim($row['Name']);
        $rows[] = $buildRow($tbl, '1', '');
        $tlower = strtolower($tbl);
        if (!empty($fieldsByTable[$tlower])) {
            $fnames = array_values($fieldsByTable[$tlower]);
            sort($fnames);
            foreach ($fnames as $fn) {
                $rows[] = $buildRow($fn, '4', $tbl);
            }
        }
    }
    $stmt->close();

    // Views
    try {
        $stmt = $conn->query('SELECT VIEW_NAME FROM system.views ORDER BY VIEW_NAME');
        while ($row = $stmt->fetchAssoc()) $rows[] = $buildRow(trim($row['VIEW_NAME']), '6', '');
        $stmt->close();
    } catch (Throwable $e) {}

    // Stored Procedures
    try {
        $stmt = $conn->query('SELECT PROC_NAME FROM system.storedprocedures ORDER BY PROC_NAME');
        while ($row = $stmt->fetchAssoc()) $rows[] = $buildRow(trim($row['PROC_NAME']), '10', '');
        $stmt->close();
    } catch (Throwable $e) {}

    // Functions
    try {
        $stmt = $conn->query('SELECT FUNC_NAME FROM system.functions ORDER BY FUNC_NAME');
        while ($row = $stmt->fetchAssoc()) $rows[] = $buildRow(trim($row['FUNC_NAME']), '18', '');
        $stmt->close();
    } catch (Throwable $e) {}

    $conn->close();
    echo json_encode([
        'data'       => $rows,
        'canInherit' => $canInherit,
        'groups'     => $userGroups,
    ]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
