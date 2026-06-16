<?php
// Test UPDATE syntax options
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$conn = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);

// Insert test row
$conn->execute("INSERT INTO leases (leaseid, TenantName) VALUES ('TESTU1', 'Test Tenant')");
echo "Inserted TESTU1\n";

$tests = [
    "UPDATE leases SET TenantName = 'Updated Tenant' WHERE leaseid = 'TESTU1'",
    "UPDATE leases SET TenantName = TenantName WHERE leaseid = 'TESTU1'",
    "UPDATE leases SET leaseid = leaseid WHERE leaseid = 'TESTU1'",
];
foreach ($tests as $sql) {
    echo "\nSQL: " . $sql . "\n";
    try {
        $conn->execute($sql);
        echo "OK\n";
    } catch (Throwable $e) { echo "ERROR: " . $e->getMessage() . "\n"; }
}

// Cleanup
$conn->execute("DELETE FROM leases WHERE leaseid = 'TESTU1'");
$conn->close();
echo "\nDone.\n";
