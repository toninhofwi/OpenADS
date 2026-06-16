<?php
// Test the exact queries that run when opening a table in DA-Web
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';

$opts = ['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys'];
$conn = AdsConnection::connect($opts);

echo "=== system.columns (no WHERE) ===\n";
try {
    $stmt = $conn->query("SELECT TABLE_NAME, COL_NAME, COL_NUM, COL_TYPE, COL_LEN, COL_DEC FROM system.columns");
    $rows = $stmt->fetchAll();
    $stmt->close();
    echo "OK: " . count($rows) . " rows\n";
} catch (Throwable $e) { echo "ERROR: " . $e->getMessage() . "\n"; }

echo "\n=== system.triggers WHERE TABLE_NAME ===\n";
try {
    $stmt = $conn->query("SELECT TRIG_NAME FROM system.triggers WHERE TABLE_NAME = 'leases'");
    $rows = $stmt->fetchAll();
    $stmt->close();
    echo "OK: " . count($rows) . " rows\n";
    foreach ($rows as $r) echo "  TRIG_NAME=" . $r['TRIG_NAME'] . "\n";
} catch (Throwable $e) { echo "ERROR: " . $e->getMessage() . "\n"; }

echo "\n=== SELECT * FROM leases LIMIT 5 ===\n";
try {
    $stmt = $conn->query("SELECT * FROM leases LIMIT 5");
    $rows = $stmt->fetchAll();
    $stmt->close();
    echo "OK: " . count($rows) . " rows\n";
} catch (Throwable $e) { echo "ERROR: " . $e->getMessage() . "\n"; }

echo "\n=== AdsTable::getIndexTags('leases') ===\n";
try {
    $tbl = AdsTable::open($conn, 'leases', 0);
    $tags = $tbl->getIndexTags();
    $tbl->close();
    echo "OK: " . count($tags) . " tags\n";
    foreach ($tags as $t) echo "  tag=" . $t['tag'] . " expr=" . $t['expression'] . "\n";
} catch (Throwable $e) { echo "ERROR: " . $e->getMessage() . "\n"; }

$conn->close();
echo "\nDone.\n";
