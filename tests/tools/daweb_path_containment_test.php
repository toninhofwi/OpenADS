<?php
/**
 * Smoke test for api_resolve_path_under_root() in DA-Web/api/common.php.
 * Run: php tests/tools/daweb_path_containment_test.php
 */
declare(strict_types=1);

require __DIR__ . '/../../DA-Web/api/common.php';

function pass(string $msg): void { echo "  PASS: $msg\n"; }
function fail(string $msg): void { echo "  FAIL: $msg\n"; exit(1); }

echo "DA-Web path containment smoke test\n";

$root = sys_get_temp_dir() . DIRECTORY_SEPARATOR . 'daweb_path_' . bin2hex(random_bytes(4));
if (!mkdir($root) || !mkdir($root . DIRECTORY_SEPARATOR . 'indexes')) {
    fail('could not create temp dirs');
}

$idx = $root . DIRECTORY_SEPARATOR . 'indexes' . DIRECTORY_SEPARATOR . 'emp.cdx';
file_put_contents($idx, str_repeat("\0", 1536));

$inside = api_resolve_path_under_root('indexes/emp.cdx', $root);
if ($inside === null || !is_file($inside)) {
    fail('expected indexes/emp.cdx under root');
}
pass('relative index path resolves inside root');

$outside = api_resolve_path_under_root('..' . DIRECTORY_SEPARATOR . 'passwd', $root);
if ($outside !== null) {
    fail('traversal path should be rejected');
}
pass('dot-dot index path rejected');

$absOutsidePath = DIRECTORY_SEPARATOR === '/' ? '/etc/passwd' : 'C:\\Windows\\win.ini';
$absOutside = api_resolve_path_under_root($absOutsidePath, $root);
if ($absOutside !== null) {
    fail('absolute path outside root should be rejected');
}
pass('absolute path outside root rejected');

@unlink($idx);
@rmdir($root . DIRECTORY_SEPARATOR . 'indexes');
@rmdir($root);

echo "All checks passed.\n";