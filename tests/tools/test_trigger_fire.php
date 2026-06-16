<?php
/**
 * Test: trigger firing on INSERT/UPDATE/DELETE in OpenADS.
 *
 * pmsys_imported.add has triggers on leases, properties, reminders.
 * All trigger bodies use ADS procedural SQL (DECLARE @var, SET @var,
 * EXECUTE PROCEDURE, __new/__old) which OpenADS does not yet support.
 *
 * Status: trigger detection works; body execution fails silently.
 * INSERT/DELETE DML works. UPDATE with literal values works.
 */
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';

$opts = ['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys'];
$conn = AdsConnection::connect($opts);
$dict = AdsDictionary::fromConnection($conn);

function count_table(AdsConnection $c, string $t): int {
    $s = $c->query("SELECT COUNT(*) AS cnt FROM $t");
    $r = $s->fetchAssoc(); $s->close();
    return (int)($r['cnt'] ?? 0);
}

echo "=== Trigger fire test (OpenADS + pmsys_imported.add) ===\n\n";
echo "Tables with triggers:\n";
$stmt = $conn->query("SELECT TRIG_NAME, TABLE_NAME FROM system.triggers ORDER BY TABLE_NAME, TRIG_NAME");
while ($r = $stmt->fetchAssoc()) {
    echo "  {$r['TABLE_NAME']}: {$r['TRIG_NAME']}\n";
}
$stmt->close();

$auditBefore = count_table($conn, 'auditlog');
echo "\nauditlog before: $auditBefore\n";

// --- INSERT on leases (has Insert AuditLog trigger) ---
echo "\n--- INSERT leases ---\n";
$lBefore = count_table($conn, 'leases');
try {
    $conn->execute("INSERT INTO leases (leaseid) VALUES ('TESTTRIGGER1')");
    echo "INSERT ok (leases: $lBefore -> " . count_table($conn, 'leases') . ")\n";
} catch (Throwable $e) { echo "INSERT error: " . $e->getMessage() . "\n"; }
$auditAfterInsert = count_table($conn, 'auditlog');
echo "auditlog: $auditAfterInsert  delta=" . ($auditAfterInsert - $auditBefore) . "\n";

// --- UPDATE on leases (has Update AuditLog trigger) ---
echo "\n--- UPDATE leases (with literal value) ---\n";
$auditBeforeUpdate = count_table($conn, 'auditlog');
try {
    $conn->execute("UPDATE leases SET TenantName = 'Test' WHERE leaseid = 'TESTTRIGGER1'");
    echo "UPDATE ok\n";
} catch (Throwable $e) { echo "UPDATE error: " . $e->getMessage() . "\n"; }
$auditAfterUpdate = count_table($conn, 'auditlog');
echo "auditlog: $auditAfterUpdate  delta=" . ($auditAfterUpdate - $auditBeforeUpdate) . "\n";

// --- DELETE on leases ---
echo "\n--- DELETE leases ---\n";
$auditBeforeDelete = count_table($conn, 'auditlog');
try {
    $conn->execute("DELETE FROM leases WHERE leaseid = 'TESTTRIGGER1'");
    echo "DELETE ok\n";
} catch (Throwable $e) { echo "DELETE error: " . $e->getMessage() . "\n"; }
$auditAfterDelete = count_table($conn, 'auditlog');
echo "auditlog: $auditAfterDelete  delta=" . ($auditAfterDelete - $auditBeforeDelete) . "\n";

echo "\n=== Summary ===\n";
$total = count_table($conn, 'auditlog') - $auditBefore;
if ($total > 0) {
    echo "PASS: Triggers fired, auditlog grew by $total rows.\n";
} else {
    echo "PARTIAL: DML operations succeed; triggers are detected but body\n";
    echo "  SQL fails silently. Reason: ADS procedural SQL not yet implemented:\n";
    echo "  - DECLARE @var TYPE\n";
    echo "  - SET @var = expression\n";
    echo "  - __new / __old virtual cursor tables\n";
    echo "  - Multi-statement trigger body execution\n";
    echo "These require an ADS-compatible procedural SQL engine in OpenADS.\n";
}

$conn->close();
