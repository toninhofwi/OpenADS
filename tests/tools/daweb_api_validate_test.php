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

echo "All checks passed.\n";