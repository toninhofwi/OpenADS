<?php
// Test trigger names from SAP ADS (port 80, pmsys.add)
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';

$opts = ['path'=>'F:/OpenADS/testdata/pmsys/pmsys.add','user'=>'adssys','password'=>'pmsys'];
$conn = AdsConnection::connect($opts);

echo "=== All triggers in pmsys.add (SAP) ===\n";
$stmt = $conn->query("SELECT TRIG_NAME, TABLE_NAME FROM system.triggers ORDER BY TABLE_NAME, TRIG_NAME");
while ($row = $stmt->fetchAssoc()) {
    echo "  [{$row['TABLE_NAME']}] {$row['TRIG_NAME']}\n";
}
$stmt->close();

$conn->close();
