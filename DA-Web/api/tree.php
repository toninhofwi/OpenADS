<?php
/**
 * api/tree.php — jsTree lazy-load provider
 *
 * Actions:
 *   roots               → all configured DDs (connected/disconnected)
 *   dd_children         → category nodes for a connected DD
 *   category_children   → leaf nodes for a category (tables, users, etc.)
 *   table_children      → table sub-nodes (fields, indexes)
 */
header('Content-Type: application/json');
session_start();

$configFile = __DIR__ . '/../config/dictionaries.json';

function loadDicts(string $file): array {
    if (!file_exists($file)) return [];
    return json_decode(file_get_contents($file), true) ?? [];
}

function getConn(string $ddName): ?AdsConnection {
    if (!isset($_SESSION['connections'][$ddName])) return null;
    $c = $_SESSION['connections'][$ddName];
    $opts = ['path' => $c['path']];
    if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
    if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];
    return AdsConnection::connect($opts);
}

$action = $_GET['action'] ?? '';
$ddName = $_GET['dd']       ?? '';
$cat    = $_GET['cat']      ?? '';
$table  = $_GET['table']    ?? '';

// ─── roots ───────────────────────────────────────────────────────────────────
if ($action === 'roots') {
    $dicts = loadDicts($configFile);
    $open  = array_keys($_SESSION['connections'] ?? []);
    $nodes = [];
    foreach ($dicts as $d) {
        $connected  = in_array($d['name'], $open, true);
        $entryType  = $d['entryType'] ?? 'dd';
        $isFree     = ($entryType === 'free');
        $icon = $isFree
            ? ($connected ? 'jstree-icon-free-open' : 'jstree-icon-free-closed')
            : ($connected ? 'jstree-icon-dd-open'   : 'jstree-icon-dd-closed');
        $nodes[] = [
            'id'       => 'dd_' . $d['name'],
            'text'     => $d['name'] . ($isFree ? ' 📂' : ''),
            'icon'     => $icon,
            'children' => $connected,
            'state'    => ['opened' => false],
            'li_attr'  => ['data-dd' => $d['name'], 'data-type' => 'dd'],
            'a_attr'   => ['data-dd'        => $d['name'],
                           'data-type'      => 'dd',
                           'data-entry'     => $entryType,
                           'data-connected' => $connected ? 'true' : 'false'],
        ];
    }
    echo json_encode($nodes);
    exit;
}

// ─── dd_children ─────────────────────────────────────────────────────────────
if ($action === 'dd_children') {
    if ($ddName === '') { echo json_encode([]); exit; }

    // Free-tables directories expand directly to a flat table list
    $dicts = loadDicts($configFile);
    foreach ($dicts as $d) {
        if ($d['name'] === $ddName && ($d['entryType'] ?? 'dd') === 'free') {
            // Reuse category_children logic for 'tables' category
            $_GET['cat'] = 'tables';
            $cat = 'tables';
            break;
        }
    }
    // If this was a free-tables entry, fall through to category_children handler
    if (isset($cat) && $cat === 'tables') {
        $conn = getConn($ddName);
        if ($conn === null) { echo json_encode([]); exit; }
        $nodes = [];
        try {
            $stmt = $conn->query("SELECT TABLE_NAME FROM system.tables ORDER BY TABLE_NAME");
            while ($row = $stmt->fetchAssoc()) {
                $t = $row['TABLE_NAME'];
                $nodes[] = [
                    'id'      => "tbl_{$ddName}_{$t}",
                    'text'    => $t,
                    'icon'    => 'jstree-icon-table',
                    'children'=> true,
                    'a_attr'  => ['data-dd' => $ddName, 'data-type' => 'table', 'data-table' => $t],
                ];
            }
        } catch (AdsException $e) {
            // empty
        } finally {
            $conn->close();
        }
        echo json_encode($nodes);
        exit;
    }

    $categories = [
        ['id' => "cat_{$ddName}_tables",     'text' => 'Tables',            'icon' => 'jstree-icon-tables'],
        ['id' => "cat_{$ddName}_views",      'text' => 'Views',             'icon' => 'jstree-icon-views'],
        ['id' => "cat_{$ddName}_procs",      'text' => 'Stored Procedures', 'icon' => 'jstree-icon-procs'],
        ['id' => "cat_{$ddName}_functions",  'text' => 'Functions',         'icon' => 'jstree-icon-procs'],
        ['id' => "cat_{$ddName}_triggers",   'text' => 'Triggers',          'icon' => 'jstree-icon-triggers'],
        ['id' => "cat_{$ddName}_users",      'text' => 'Users',             'icon' => 'jstree-icon-users'],
        ['id' => "cat_{$ddName}_groups",     'text' => 'Groups',            'icon' => 'jstree-icon-groups'],
        ['id' => "cat_{$ddName}_ri",         'text' => 'RI Objects',        'icon' => 'jstree-icon-ri'],
        ['id' => "cat_{$ddName}_links",      'text' => 'Links',             'icon' => 'jstree-icon-links'],
    ];
    foreach ($categories as &$c) {
        $c['children'] = true;
        $c['a_attr']   = ['data-dd' => $ddName, 'data-type' => 'category',
                          'data-cat' => substr($c['id'], strlen("cat_{$ddName}_"))];
    }
    unset($c);
    echo json_encode($categories);
    exit;
}

// ─── category_children ───────────────────────────────────────────────────────
if ($action === 'category_children') {
    if ($ddName === '' || $cat === '') { echo json_encode([]); exit; }

    $conn = getConn($ddName);
    if ($conn === null) { echo json_encode([]); exit; }

    $nodes = [];
    try {
        switch ($cat) {
            case 'tables':
                $stmt = $conn->query("SELECT TABLE_NAME FROM system.tables ORDER BY TABLE_NAME");
                while ($row = $stmt->fetchAssoc()) {
                    $t = $row['TABLE_NAME'];
                    $nodes[] = [
                        'id'      => "tbl_{$ddName}_{$t}",
                        'text'    => $t,
                        'icon'    => 'jstree-icon-table',
                        'children'=> true,
                        'a_attr'  => ['data-dd' => $ddName, 'data-type' => 'table', 'data-table' => $t],
                    ];
                }
                break;

            case 'views':
                $stmt = $conn->query("SELECT VIEW_NAME FROM system.views ORDER BY VIEW_NAME");
                while ($row = $stmt->fetchAssoc()) {
                    $v = $row['VIEW_NAME'];
                    $nodes[] = ['id' => "view_{$ddName}_{$v}", 'text' => $v,
                                'icon' => 'jstree-icon-view', 'children' => false,
                                'a_attr' => ['data-dd' => $ddName, 'data-type' => 'view', 'data-name' => $v]];
                }
                break;

            case 'procs':
            case 'functions':
                $stmt = $conn->query("SELECT PROC_NAME FROM system.storedprocedures");
                $pnames = [];
                while ($row = $stmt->fetchAssoc()) { $pnames[] = $row['PROC_NAME']; }
                sort($pnames);
                $itype = ($cat === 'functions') ? 'function' : 'proc';
                $iprefix = ($cat === 'functions') ? 'fn' : 'proc';
                foreach ($pnames as $p) {
                    $nodes[] = ['id' => "{$iprefix}_{$ddName}_{$p}", 'text' => $p,
                                'icon' => 'jstree-icon-proc', 'children' => false,
                                'a_attr' => ['data-dd' => $ddName, 'data-type' => $itype, 'data-name' => $p]];
                }
                break;

            case 'triggers':
                $stmt = $conn->query("SELECT TRIG_NAME FROM system.triggers ORDER BY TRIG_NAME");
                while ($row = $stmt->fetchAssoc()) {
                    $tr = $row['TRIG_NAME'];
                    $nodes[] = ['id' => "trg_{$ddName}_{$tr}", 'text' => $tr,
                                'icon' => 'jstree-icon-trigger', 'children' => false,
                                'a_attr' => ['data-dd' => $ddName, 'data-type' => 'trigger', 'data-name' => $tr]];
                }
                break;

            case 'users':
                $stmt = $conn->query("SELECT USER_NAME FROM system.users ORDER BY USER_NAME");
                while ($row = $stmt->fetchAssoc()) {
                    $u = $row['USER_NAME'];
                    $nodes[] = ['id' => "usr_{$ddName}_{$u}", 'text' => $u,
                                'icon' => 'jstree-icon-user', 'children' => false,
                                'a_attr' => ['data-dd' => $ddName, 'data-type' => 'user', 'data-name' => $u]];
                }
                break;

            case 'groups':
                $stmt = $conn->query("SELECT GROUP_NAME FROM system.usergroups ORDER BY GROUP_NAME");
                $seen = [];
                while ($row = $stmt->fetchAssoc()) {
                    $g = $row['GROUP_NAME'];
                    if (isset($seen[$g])) continue;
                    $seen[$g] = true;
                    $nodes[] = ['id' => "grp_{$ddName}_{$g}", 'text' => $g,
                                'icon' => 'jstree-icon-group', 'children' => false,
                                'a_attr' => ['data-dd' => $ddName, 'data-type' => 'group', 'data-name' => $g]];
                }
                break;

            case 'ri':
                $stmt = $conn->query("SELECT RI_NAME FROM system.relations ORDER BY RI_NAME");
                while ($row = $stmt->fetchAssoc()) {
                    $r = $row['RI_NAME'];
                    $nodes[] = ['id' => "ri_{$ddName}_{$r}", 'text' => $r,
                                'icon' => 'jstree-icon-ri', 'children' => false,
                                'a_attr' => ['data-dd' => $ddName, 'data-type' => 'ri', 'data-name' => $r]];
                }
                break;

            case 'links':
                $stmt = $conn->query("SELECT LINK_NAME FROM system.links ORDER BY LINK_NAME");
                while ($row = $stmt->fetchAssoc()) {
                    $l = $row['LINK_NAME'];
                    $nodes[] = ['id' => "lnk_{$ddName}_{$l}", 'text' => $l,
                                'icon' => 'jstree-icon-link', 'children' => false,
                                'a_attr' => ['data-dd' => $ddName, 'data-type' => 'link', 'data-name' => $l]];
                }
                break;
        }
    } catch (Throwable $e) {
        $nodes = [['id' => "err_{$ddName}_{$cat}", 'text' => '⚠ ' . $e->getMessage(),
                   'icon' => 'jstree-icon-error', 'children' => false,
                   'a_attr' => ['data-type' => 'error']]];
    } finally {
        $conn->close();
    }

    echo json_encode($nodes);
    exit;
}

// ─── table_children ──────────────────────────────────────────────────────────
if ($action === 'table_children') {
    if ($ddName === '' || $table === '') { echo json_encode([]); exit; }

    $conn = getConn($ddName);
    if ($conn === null) { echo json_encode([]); exit; }

    $nodes = [];
    try {
        // Fields sub-node — leaf; click opens a metadata tab
        $nodes[] = [
            'id'       => "fields_{$ddName}_{$table}",
            'text'     => 'Fields',
            'icon'     => 'jstree-icon-fields',
            'children' => false,
            'a_attr'   => ['data-dd' => $ddName, 'data-type' => 'fields', 'data-table' => $table],
        ];
        // Indexes sub-node — leaf; click opens a metadata tab
        $nodes[] = [
            'id'       => "indexes_{$ddName}_{$table}",
            'text'     => 'Indexes',
            'icon'     => 'jstree-icon-indexes',
            'children' => false,
            'a_attr'   => ['data-dd' => $ddName, 'data-type' => 'indexes', 'data-table' => $table],
        ];
    } finally {
        $conn->close();
    }

    echo json_encode($nodes);
    exit;
}

echo json_encode([]);
