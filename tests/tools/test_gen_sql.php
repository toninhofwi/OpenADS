<?php
// Simulate gen_sql.php for the 'leases' table against pmsys_imported.add
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';

$opts = ['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys'];
$table = 'leases';

$conn = AdsConnection::connect($opts);

// Test 1: system.columns
echo "=== system.columns for $table ===\n";
try {
    $stmt = $conn->query("SELECT TABLE_NAME, COL_NAME, COL_NUM, COL_TYPE, COL_LEN, COL_DEC FROM system.columns");
    $fields = [];
    while ($row = $stmt->fetchAssoc()) {
        if (strcasecmp($row['TABLE_NAME'], $table) !== 0) continue;
        $fields[] = $row;
    }
    echo "Found " . count($fields) . " fields for $table\n";
    foreach ($fields as $f) {
        echo "  {$f['COL_NUM']}. {$f['COL_NAME']} type=" . bin2hex($f['COL_TYPE']) . " len={$f['COL_LEN']}\n";
    }
} catch (Throwable $e) { echo "ERROR system.columns: " . $e->getMessage() . " (code=" . $e->getCode() . ")\n"; }

// Test 2: trigger query with WHERE TABLE_NAME
echo "\n=== system.triggers WHERE TABLE_NAME='$table' ===\n";
try {
    $stmt = $conn->query("SELECT TRIG_NAME FROM system.triggers WHERE TABLE_NAME = '$table'");
    while ($row = $stmt->fetchAssoc()) echo "  " . $row['TRIG_NAME'] . "\n";
} catch (Throwable $e) { echo "ERROR triggers: " . $e->getMessage() . " (code=" . $e->getCode() . ")\n"; }

// Test 3: getTriggerProperty with plain name (no composite key)
echo "\n=== getTriggerProperty with plain name (broken) ===\n";
try {
    $dict = AdsDictionary::fromConnection($conn);
    $r = $dict->getTriggerProperty('Insert AuditLog', 1401);
    echo "  result len=" . strlen($r) . "\n";
} catch (Throwable $e) { echo "ERROR: " . $e->getMessage() . " (code=" . $e->getCode() . ")\n"; }

// Test 4: getTriggerProperty with composite key (correct)
echo "\n=== getTriggerProperty with composite key ===\n";
try {
    $dict2 = AdsDictionary::fromConnection($conn);
    $r = $dict2->getTriggerProperty('leases::Insert AuditLog', 1401);
    echo "  result len=" . strlen($r) . " hex=" . bin2hex($r) . "\n";
} catch (Throwable $e) { echo "ERROR: " . $e->getMessage() . " (code=" . $e->getCode() . ")\n"; }

$conn->close();
