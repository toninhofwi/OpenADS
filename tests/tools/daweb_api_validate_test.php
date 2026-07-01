<?php
/**
 * Smoke test for DA-Web api/common.php validation helpers.
 * Run: php tests/tools/daweb_api_validate_test.php
 */
declare(strict_types=1);

require __DIR__ . '/../../DA-Web/api/common.php';

function pass(string $msg): void { echo "  PASS: $msg\n"; }
function fail(string $msg): void { echo "  FAIL: $msg\n"; exit(1); }

echo "DA-Web API validation smoke test\n";

const ID_PATTERN = '/^[A-Za-z_][A-Za-z0-9_]*$/';

foreach (['emp', 'Tag_1', 'RI_OBJ'] as $ok) {
    if (!preg_match(ID_PATTERN, $ok)) {
        fail("expected valid identifier: $ok");
    }
}
pass('identifier pattern accepts safe names');

foreach (["x'; DROP TABLE t; --", '../../passwd', 'bad-name'] as $bad) {
    if (preg_match(ID_PATTERN, $bad)) {
        fail("expected invalid identifier: $bad");
    }
}
pass('identifier pattern rejects injection and traversal');

$q = api_sql_quote("O'Brien");
if ($q !== "O''Brien") {
    fail("api_sql_quote expected doubled quote, got '$q'");
}
pass('api_sql_quote doubles single quotes');

$local = api_ads_connect_opts([
    'path' => 'data/example.add',
    'username' => 'admin',
    'password' => 'secret',
    'connType' => 'local',
]);
if (($local['serverType'] ?? null) !== (defined('ADS_LOCAL_SERVER') ? ADS_LOCAL_SERVER : 1)) {
    fail('local connType should set ADS_LOCAL_SERVER serverType');
}
if (($local['path'] ?? '') !== 'data/example.add') {
    fail('local connType should preserve path');
}
if (($local['user'] ?? '') !== 'admin' || ($local['password'] ?? '') !== 'secret') {
    fail('connection helper should copy credentials');
}
pass('local connection options preserve path and set serverType');

$remote = api_ads_connect_opts([
    'path' => 'data/example.add',
    'connType' => 'remote',
]);
if (($remote['serverType'] ?? null) !== (defined('ADS_REMOTE_SERVER') ? ADS_REMOTE_SERVER : 2)) {
    fail('remote connType should set ADS_REMOTE_SERVER serverType');
}
if (($remote['path'] ?? '') !== 'tcp://127.0.0.1:6264/data/example.add') {
    fail('remote connType should add default OpenADS tcp URI prefix on port 6264');
}
pass('remote connection options add tcp URI and set serverType');

$remoteUri = api_ads_connect_opts([
    'path' => 'tcp://dbhost:6262/data/example.add',
    'connType' => 'remote',
]);
if (($remoteUri['path'] ?? '') !== 'tcp://dbhost:6262/data/example.add') {
    fail('remote connType should preserve explicit tcp URI');
}
pass('remote connection options preserve explicit tcp URI');

echo "All checks passed.\n";
