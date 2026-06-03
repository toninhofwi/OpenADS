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

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$action = trim($body['action'] ?? 'save');
$ddName = trim($body['dd']     ?? '');
$riName = trim($body['ri']     ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '' || $riName === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd and ri are required']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
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

    if ($action === 'delete') {
        $conn->execute("EXECUTE PROCEDURE sp_DropReferentialIntegrity('$riName')");
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
        http_response_code(400);
        echo json_encode(['error' => 'parent and child tables are required']);
        exit;
    }

    // Drop existing RI with this name (ignore error if it doesn't exist)
    try { $conn->execute("EXECUTE PROCEDURE sp_DropReferentialIntegrity('$riName')"); } catch (Throwable $e) {}

    // Create new RI — ACE parameter order: riName, failTable, parentTable, childTable, parentTag, updateRule, deleteRule
    $sql = "EXECUTE PROCEDURE sp_CreateReferentialIntegrity('$riName', '$failTable', '$parent', '$child', '$parentTag', $updateOpt, $deleteOpt)";
    $conn->execute($sql);
    $conn->close();
    echo json_encode(['saved' => true]);
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
}
