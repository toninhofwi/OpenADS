<?php
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$conn = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);

echo "=== system.columns ===\n";
try {
    $stmt = $conn->query("SELECT TABLE_NAME, COL_NAME, COL_NUM, COL_TYPE, COL_LEN, COL_DEC FROM system.columns");
    $n = 0;
    while ($row = $stmt->fetchAssoc()) {
        $n++;
        if ($n <= 5) echo "  {$row['TABLE_NAME']}.{$row['COL_NAME']} type={$row['COL_TYPE']} len={$row['COL_LEN']}\n";
    }
    echo "Total: $n rows\n";
} catch (Throwable $e) { echo "ERROR: " . $e->getMessage() . "\n"; }

echo "\n=== system.triggers with WHERE TABLE_NAME ===\n";
try {
    $stmt = $conn->query("SELECT TRIG_NAME FROM system.triggers WHERE TABLE_NAME = 'leases'");
    while ($row = $stmt->fetchAssoc()) echo "  " . $row['TRIG_NAME'] . "\n";
} catch (Throwable $e) { echo "ERROR: " . $e->getMessage() . "\n"; }

echo "\n=== SELECT * FROM leases LIMIT 1 ===\n";
try {
    $stmt = $conn->query("SELECT * FROM leases LIMIT 1");
    $row = $stmt->fetchAssoc();
    echo "  fields: " . implode(', ', array_keys($row ?? [])) . "\n";
} catch (Throwable $e) { echo "ERROR: " . $e->getMessage() . "\n"; }

$conn->close();
