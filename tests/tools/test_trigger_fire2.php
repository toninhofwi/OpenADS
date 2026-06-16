<?php
// Investigate trigger timing and correct INSERT syntax
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';

// SAP side: check timing on original triggers
echo "=== SAP triggers for leases (pmsys.add) ===\n";
$sap = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys.add','user'=>'adssys','password'=>'pmsys']);
$sd = AdsDictionary::fromConnection($sap);
$stmt = $sap->query("SELECT TRIG_NAME, TABLE_NAME FROM system.triggers WHERE TABLE_NAME = 'leases'");
while ($row = $stmt->fetchAssoc()) {
    $key = $row['TRIG_NAME'];
    $evRaw  = $sd->getTriggerProperty($key, 1401);
    $timRaw = $sd->getTriggerProperty($key, 1402);
    $ev  = strlen($evRaw)  >= 4 ? unpack('V', substr($evRaw, 0, 4))[1] : 0;
    $tim = strlen($timRaw) >= 4 ? unpack('V', substr($timRaw, 0, 4))[1] : 0;
    echo "  [{$key}] ev=$ev tim=$tim\n";
}
$stmt->close();
$sap->close();

// OpenADS side: check leases column list
echo "\n=== leases columns in pmsys_imported.add ===\n";
$conn = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);
$stmt2 = $conn->query("SELECT COL_NAME, COL_TYPE, COL_LEN FROM system.columns WHERE TABLE_NAME = 'leases'");
// Actually WHERE on system.columns needs testing -- let's fetch all
$stmt2 = $conn->query("SELECT TABLE_NAME, COL_NAME, COL_TYPE, COL_LEN, COL_DEC FROM system.columns");
while ($row = $stmt2->fetchAssoc()) {
    if (strcasecmp($row['TABLE_NAME'], 'leases') !== 0) continue;
    echo "  {$row['COL_NAME']} {$row['COL_TYPE']}({$row['COL_LEN']})\n";
}
$stmt2->close();

// Try different INSERT syntaxes
echo "\n=== Testing INSERT syntaxes ===\n";
$tests = [
    "INSERT INTO leases (leaseid, propertyid, startdate, enddate, rent, status) VALUES ('TESTLEASE99', 'TESTPROP1', '2026-01-01', '2027-01-01', 1000.00, 'Active')",
    "INSERT INTO leases (leaseid, propertyid) VALUES ('TESTLEASE99', 'TESTPROP1')",
];
foreach ($tests as $sql) {
    echo "SQL: " . substr($sql, 0, 80) . "...\n";
    try {
        $conn->execute($sql);
        echo "  OK\n";
        $conn->execute("DELETE FROM leases WHERE leaseid = 'TESTLEASE99'");
    } catch (Throwable $e) {
        echo "  ERROR: " . $e->getMessage() . "\n";
    }
}
$conn->close();
