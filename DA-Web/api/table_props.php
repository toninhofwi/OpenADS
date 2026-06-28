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
const ADS_DD_TABLE_MEMO_BLOCK_SIZE = 215;
const ADS_DD_TABLE_CACHING         = 217;

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

try {
    $conn = AdsConnection::connect($opts);
    $dict = AdsDictionary::fromConnection($conn);

    if ($isPost) {
        $caching = max(0, min(2, (int)($body['caching'] ?? 0)));
        table_prop_write_u16($dict, $table, ADS_DD_TABLE_AUTO_CREATE, !empty($body['autoCreate']) ? 1 : 0);
        table_prop_write_u16($dict, $table, ADS_DD_TABLE_MEMO_BLOCK_SIZE, (int)($body['memoBlockSize'] ?? 0));
        table_prop_write_u16($dict, $table, ADS_DD_TABLE_CACHING, $caching);
        $conn->close();
        echo json_encode(['saved' => true]);
    } else {
        $result = [
            'autoCreate'    => table_prop_read_u16($dict, $table, ADS_DD_TABLE_AUTO_CREATE) !== 0,
            'memoBlockSize' => table_prop_read_u16($dict, $table, ADS_DD_TABLE_MEMO_BLOCK_SIZE),
            'caching'       => table_prop_read_u16($dict, $table, ADS_DD_TABLE_CACHING),
            'cacheModes'    => [
                ['value' => 0, 'label' => 'None'],
                ['value' => 1, 'label' => 'Reads'],
                ['value' => 2, 'label' => 'Writes'],
            ],
        ];
        $conn->close();
        echo json_encode($result);
    }
} catch (Throwable $e) {
    api_exception(500, $e);
}
