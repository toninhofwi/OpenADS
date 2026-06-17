<?php
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$conn = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);

// Check which triggers are now visible
$st = $conn->query("SELECT TRIG_NAME, TABLE_NAME FROM system.triggers WHERE TABLE_NAME='leases' ORDER BY TRIG_NAME");
echo "Triggers on leases:\n";
while ($r = $st->fetchAssoc()) {
    echo "  {$r['TRIG_NAME']}\n";
}

// Try direct INSERT into auditlog (simple columns only, no reserved words)
echo "\nDirect INSERT (no reserved words):\n";
$b = (int)$conn->query("SELECT COUNT(*) AS n FROM auditlog")->fetchAssoc()['n'];
try {
    $conn->execute("INSERT INTO auditlog (TableKey, Action) VALUES ('TEST_SIMPLE','Test')");
    $a = (int)$conn->query("SELECT COUNT(*) AS n FROM auditlog")->fetchAssoc()['n'];
    echo "  delta=" . ($a-$b) . "\n";
} catch (Throwable $e) {
    echo "  ERROR: " . $e->getMessage() . "\n";
}
// clean up
try { $conn->execute("DELETE FROM auditlog WHERE TableKey='TEST_SIMPLE'"); } catch(Throwable $e){}

// Try with bracket-quoted reserved word [table]
echo "\nDirect INSERT ([table] bracket):\n";
$b2 = (int)$conn->query("SELECT COUNT(*) AS n FROM auditlog")->fetchAssoc()['n'];
try {
    $conn->execute("INSERT INTO auditlog ([table],TableKey,Action) VALUES ('leases','TEST_BRACKET','BracketTest')");
    $a2 = (int)$conn->query("SELECT COUNT(*) AS n FROM auditlog")->fetchAssoc()['n'];
    echo "  delta=" . ($a2-$b2) . "\n";
} catch (Throwable $e) {
    echo "  ERROR: " . $e->getMessage() . "\n";
}
try { $conn->execute("DELETE FROM auditlog WHERE TableKey='TEST_BRACKET'"); } catch(Throwable $e){}

$conn->close();
