<?php
// Debug why trigger doesn't fire
// Check trigger table_alias vs what the system knows about "leases"
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';

$opts = ['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys'];
$conn = AdsConnection::connect($opts);
$dict = AdsDictionary::fromConnection($conn);

// Check what tables are registered (system.tables)
echo "=== system.tables ===\n";
$stmt = $conn->query("SELECT Name FROM system.tables ORDER BY Name");
while ($r = $stmt->fetchAssoc()) {
    echo "  " . $r['Name'] . "\n";
}
$stmt->close();

// Check system.indexes for leases
echo "\n=== system.indexes for leases ===\n";
$stmt2 = $conn->query("SELECT TABLE_NAME, INDEX_FILE FROM system.indexes");
while ($r = $stmt2->fetchAssoc()) {
    if (strcasecmp($r['TABLE_NAME'], 'leases') === 0) {
        echo "  TABLE_NAME={$r['TABLE_NAME']} INDEX_FILE={$r['INDEX_FILE']}\n";
    }
}
$stmt2->close();

// Check trigger table_alias
echo "\n=== trigger table_alias (property 1408) ===\n";
$stmt3 = $conn->query("SELECT TRIG_NAME, TABLE_NAME FROM system.triggers");
while ($r = $stmt3->fetchAssoc()) {
    $key = $r['TRIG_NAME'];
    echo "  key=$key TABLE_NAME={$r['TABLE_NAME']}\n";
    try {
        $tblRaw = $dict->getTriggerProperty($key, 1408); // table alias
        echo "    prop1408=" . json_encode(trim($tblRaw)) . "\n";
    } catch (Throwable $e) { echo "    prop1408 error: " . $e->getMessage() . "\n"; }
}
$stmt3->close();

// Do a test insert and check
echo "\n=== Test INSERT + check trigger fire via direct approach ===\n";
// Check auditlog before
$c1 = $conn->query("SELECT COUNT(*) AS cnt FROM auditlog");
$before = (int)($c1->fetchAssoc()['cnt'] ?? 0);
$c1->close();
echo "auditlog before: $before\n";

// Insert
try {
    $conn->execute("INSERT INTO leases (leaseid) VALUES ('TESTLEASE99')");
    echo "INSERT ok\n";
} catch (Throwable $e) { echo "INSERT error: " . $e->getMessage() . "\n"; }

// Check auditlog after
$c2 = $conn->query("SELECT COUNT(*) AS cnt FROM auditlog");
$after = (int)($c2->fetchAssoc()['cnt'] ?? 0);
$c2->close();
echo "auditlog after: $after  delta=" . ($after - $before) . "\n";

// Cleanup
try { $conn->execute("DELETE FROM leases WHERE leaseid = 'TESTLEASE99'"); } catch (Throwable $e) {}
$conn->close();
