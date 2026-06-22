<?php
/**
 * api/save_ri.php — create, update, or delete a RI object.
 *
 * POST { action: 'save'|'delete', dd, ri, parent, parent_tag, child, child_tag, update_opt, delete_opt }
 *
 * For 'save': drops existing (if any) and recreates with sp_CreateReferentialIntegrity.
 * For 'delete': calls sp_DropReferentialIntegrity.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$action = trim($body['action'] ?? 'save');
$ddName = trim($body['dd']     ?? '');
$riName = trim($body['ri']     ?? '');

$c = api_require_connection($ddName);
if ($riName === '') {
    api_error(400, 'ri is required');
}
api_validate_identifier($riName, 'RI name');

$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

function ruleInt(string $v): int {
    return match(trim($v)) {
        'Restrict','1' => 1,
        'Cascade','2'  => 2,
        'SetNull','3'  => 3,
        default        => 1,
    };
}

try {
    $conn = AdsConnection::connect($opts);
    $qRi = api_sql_quote($riName);

    if ($action === 'delete') {
        $conn->execute("EXECUTE PROCEDURE sp_DropReferentialIntegrity('$qRi')");
        $conn->close();
        echo json_encode(['deleted' => true]);
        exit;
    }

    // 'save': drop existing (if present) and recreate
    $parent    = trim($body['parent']     ?? '');
    $parentTag = trim($body['parent_tag'] ?? '');
    $child     = trim($body['child']      ?? '');
    $childTag  = trim($body['child_tag']  ?? '');
    $updateOpt = ruleInt($body['update_opt'] ?? 'Restrict');
    $deleteOpt = ruleInt($body['delete_opt'] ?? 'Restrict');
    $failTable = trim($body['fail_table'] ?? '');

    if ($parent === '' || $child === '') {
        api_error(400, 'parent and child tables are required');
    }
    api_validate_identifier($parent, 'parent table');
    api_validate_identifier($child, 'child table');
    if ($parentTag !== '') {
        api_validate_identifier($parentTag, 'parent tag');
    }
    if ($childTag !== '') {
        api_validate_identifier($childTag, 'child tag');
    }
    if ($failTable !== '') {
        api_validate_identifier($failTable, 'fail table');
    }

    try {
        $conn->execute("EXECUTE PROCEDURE sp_DropReferentialIntegrity('$qRi')");
    } catch (Throwable $e) {}

    $sql = "EXECUTE PROCEDURE sp_CreateReferentialIntegrity('"
         . $qRi . "', '"
         . api_sql_quote($failTable) . "', '"
         . api_sql_quote($parent) . "', '"
         . api_sql_quote($child) . "', '"
         . api_sql_quote($parentTag) . "', $updateOpt, $deleteOpt)";
    $conn->execute($sql);
    $conn->close();
    echo json_encode(['saved' => true]);
} catch (Throwable $e) {
    api_exception(500, $e);
}