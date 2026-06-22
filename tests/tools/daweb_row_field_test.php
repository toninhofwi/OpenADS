<?php
/**
 * Smoke test for daweb_row_field() pattern used in user_groups.php.
 * Run: php tests/tools/daweb_row_field_test.php
 */
declare(strict_types=1);

function daweb_row_field(array $row, string $col): string
{
    if (isset($row[$col])) {
        return trim((string)$row[$col]);
    }
    foreach ($row as $k => $v) {
        if (strcasecmp((string)$k, $col) === 0) {
            return trim((string)$v);
        }
    }
    return '';
}

function pass(string $msg): void { echo "  PASS: $msg\n"; }
function fail(string $msg): void { echo "  FAIL: $msg\n"; exit(1); }

echo "DA-Web row field helper smoke test\n";

$row = ['Group_Name' => ' Administrators ', 'User_Name' => 'RCB   '];
if (daweb_row_field($row, 'GROUP_NAME') !== 'Administrators') {
    fail('case-insensitive GROUP_NAME lookup');
}
pass('case-insensitive GROUP_NAME lookup');

if (daweb_row_field($row, 'USER_NAME') !== 'RCB') {
    fail('trimmed USER_NAME lookup');
}
pass('trimmed USER_NAME lookup');

echo "All checks passed.\n";