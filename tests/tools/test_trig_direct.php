<?php
/**
 * Direct test: can we INSERT into auditlog via SQL?
 * And does the trigger body show the correct content?
 */
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$conn = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);

echo "1. Direct INSERT into auditlog:\n";
$before = (int)$conn->query("SELECT COUNT(*) AS n FROM auditlog")->fetchAssoc()['n'];
try {
    $conn->execute("INSERT INTO auditlog ([table],[action],TableKey) VALUES ('leases','InsertTest','TEST_DIRECT')");
    $after = (int)$conn->query("SELECT COUNT(*) AS n FROM auditlog")->fetchAssoc()['n'];
    echo "   Before=$before After=$after delta=" . ($after-$before) . "\n";
} catch (Throwable $e) {
    echo "   ERROR: " . $e->getMessage() . "\n";
}

echo "\n2. Trigger body content for 'leases::OpenADS Test Insert':\n";
$st = $conn->query("SELECT TRIG_NAME, CONTAINER FROM system.triggers ORDER BY TRIG_NAME");
while ($r = $st->fetchAssoc()) {
    if (stripos($r['TRIG_NAME'], 'OpenADS Test') !== false) {
        echo "  [{$r['TRIG_NAME']}] len=" . strlen($r['CONTAINER']) . "\n";
        echo "  BODY: " . substr($r['CONTAINER'], 0, 400) . "\n";
    }
}

echo "\n3. INSERT into leases (trigger should fire):\n";
$b2 = (int)$conn->query("SELECT COUNT(*) AS n FROM auditlog")->fetchAssoc()['n'];
try {
    $conn->execute("INSERT INTO leases (leaseid) VALUES ('TRIG_TEST_1')");
    $a2 = (int)$conn->query("SELECT COUNT(*) AS n FROM auditlog")->fetchAssoc()['n'];
    echo "   auditlog before=$b2 after=$a2 delta=" . ($a2-$b2) . "\n";
} catch (Throwable $e) {
    echo "   ERROR: " . $e->getMessage() . "\n";
} finally {
    try { $conn->execute("DELETE FROM leases WHERE leaseid='TRIG_TEST_1'"); } catch(Throwable $e){}
}

$conn->close();
