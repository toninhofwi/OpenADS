<?php
/**
 * api/table_props.php - get or set DD table properties.
 *
 * RCB 06/27/2026: DA-Web stores these through AdsDDSetTableProperty so the
 * DD owns the settings and system.tables can reflect them.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';
require_once __DIR__ . '/openads_stubs.php';

const ADS_DD_TABLE_AUTO_CREATE     = 203;
const ADS_DD_TABLE_ENCRYPTION      = 214;
const ADS_DD_TABLE_MEMO_BLOCK_SIZE = 215;
const ADS_DD_TABLE_PERMISSION_LEVEL = 216;
const ADS_DD_TABLE_CACHING         = 217;
const ADS_DD_TABLE_TXN_FREE        = 218;

$isPost = $_SERVER['REQUEST_METHOD'] === 'POST';
$body   = $isPost ? (json_decode(file_get_contents('php://input'), true) ?? []) : [];
$ddName = trim((string)($isPost ? ($body['dd'] ?? '') : ($_GET['dd'] ?? '')));
$table  = trim((string)($isPost ? ($body['table'] ?? '') : ($_GET['table'] ?? '')));

if ($table === '') {
    api_error(400, 'table is required');
}
api_validate_identifier($table, 'table name');

$c = api_require_connection($ddName);
if (($c['entryType'] ?? 'dd') === 'free') {
    api_error(400, 'table properties require a data dictionary connection');
}
$opts = api_ads_connect_opts($c);

function table_prop_read_u16(AdsDictionary $dict, string $table, int $prop): int {
    try {
        $raw = $dict->getTableProperty($table, $prop);
        if ($raw === '') return 0;
        if (ctype_digit($raw)) return (int)$raw;
        if (strlen($raw) >= 2) return unpack('v', substr($raw, 0, 2))[1];
        return (int)$raw;
    } catch (Throwable) {
        return 0;
    }
}

function table_prop_write_u16(AdsDictionary $dict, string $table, int $prop, int $val): void {
    $dict->setTableProperty($table, $prop, (string)max(0, min(65535, $val)));
}

function table_prop_read_str(AdsDictionary $dict, string $table, int $prop): string {
    try {
        $raw = $dict->getTableProperty($table, $prop);
        return rtrim((string)$raw, "\0 ");
    } catch (Throwable) {
        return '';
    }
}

function table_prop_write_str(AdsDictionary $dict, string $table, int $prop, string $val): void {
    $dict->setTableProperty($table, $prop, $val);
}

try {
    $conn = AdsConnection::connect($opts);
    $dict = AdsDictionary::fromConnection($conn);

    if ($isPost) {
        $caching = max(0, min(2, (int)($body['caching'] ?? 0)));
        $permissionLevel = max(0, min(3, (int)($body['permissionLevel'] ?? 0)));
        table_prop_write_str($dict, $table, 1, (string)($body['comment'] ?? ''));
        table_prop_write_str($dict, $table, 3, (string)($body['userDefined'] ?? ''));
        table_prop_write_str($dict, $table, 200, (string)($body['validationExpr'] ?? ''));
        table_prop_write_str($dict, $table, 201, (string)($body['validationMsg'] ?? ''));
        table_prop_write_str($dict, $table, 202, (string)($body['primaryKey'] ?? ''));
        table_prop_write_str($dict, $table, 213, (string)($body['defaultIndex'] ?? ''));
        table_prop_write_u16($dict, $table, ADS_DD_TABLE_AUTO_CREATE, !empty($body['autoCreate']) ? 1 : 0);
        table_prop_write_u16($dict, $table, ADS_DD_TABLE_ENCRYPTION, !empty($body['encryption']) ? 1 : 0);
        table_prop_write_u16($dict, $table, ADS_DD_TABLE_MEMO_BLOCK_SIZE, (int)($body['memoBlockSize'] ?? 0));
        table_prop_write_u16($dict, $table, ADS_DD_TABLE_PERMISSION_LEVEL, $permissionLevel);
        table_prop_write_u16($dict, $table, ADS_DD_TABLE_CACHING, $caching);
        table_prop_write_u16($dict, $table, ADS_DD_TABLE_TXN_FREE, !empty($body['txnFree']) ? 1 : 0);
        $conn->close();
        echo json_encode(['saved' => true]);
    } else {
        $permissionLevel = table_prop_read_u16($dict, $table, ADS_DD_TABLE_PERMISSION_LEVEL);
        if ($permissionLevel > 3) $permissionLevel = 0;
        $result = [
            'comment'       => table_prop_read_str($dict, $table, 1),
            'userDefined'   => table_prop_read_str($dict, $table, 3),
            'validationExpr'=> table_prop_read_str($dict, $table, 200),
            'validationMsg' => table_prop_read_str($dict, $table, 201),
            'primaryKey'    => table_prop_read_str($dict, $table, 202),
            'defaultIndex'  => table_prop_read_str($dict, $table, 213),
            'autoCreate'    => table_prop_read_u16($dict, $table, ADS_DD_TABLE_AUTO_CREATE) !== 0,
            'encryption'    => table_prop_read_u16($dict, $table, ADS_DD_TABLE_ENCRYPTION) !== 0,
            'memoBlockSize' => table_prop_read_u16($dict, $table, ADS_DD_TABLE_MEMO_BLOCK_SIZE),
            'permissionLevel' => $permissionLevel,
            'caching'       => table_prop_read_u16($dict, $table, ADS_DD_TABLE_CACHING),
            'txnFree'       => table_prop_read_u16($dict, $table, ADS_DD_TABLE_TXN_FREE) !== 0,
            'cacheModes'    => [
                ['value' => 0, 'label' => 'None'],
                ['value' => 1, 'label' => 'Reads'],
                ['value' => 2, 'label' => 'Writes'],
            ],
            'permissionLevels' => [
                ['value' => 0, 'label' => 'Default'],
                ['value' => 1, 'label' => 'Level 1'],
                ['value' => 2, 'label' => 'Level 2'],
                ['value' => 3, 'label' => 'Level 3'],
            ],
        ];
        $conn->close();
        echo json_encode($result);
    }
} catch (Throwable $e) {
    api_exception(500, $e);
}
