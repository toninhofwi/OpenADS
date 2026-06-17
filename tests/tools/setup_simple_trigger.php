<?php
/**
 * setup_simple_trigger.php
 * Creates a simple direct-INSERT trigger on leases in pmsys_imported.add.
 * This tests the OpenADS trigger executor without requiring cursor/proc support.
 *
 * Trigger body:
 *   SET @id = (SELECT leaseid FROM __new)
 *   INSERT INTO auditlog ([table],[user],[action],[creation],[TableKey])
 *     VALUES ('leases', 'ADSSYS', 'Insert', '2026-01-01', @id)
 *
 * The SET uses the new subquery pattern; the INSERT uses @var substitution.
 */
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';

$opts = ['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys'];
$conn = AdsConnection::connect($opts);
$dict = AdsDictionary::fromConnection($conn);

$name = 'OpenADS Test Insert';
// Semicolons required: trig_split_stmts_ splits on ';'
// [table] and [user] are reserved words; bracket-quoting is supported.
$body = "SET @id = ( SELECT leaseid FROM __new );\n" .
        "INSERT INTO auditlog ([table],[action],TableKey) " .
        "VALUES ('leases','Insert',@id)";

// Drop all known copies (correct name and legacy double-prefixed variant)
foreach ([$name, "leases::$name", "leases::leases::$name"] as $dn) {
    try { $dict->dropTrigger($dn); echo "Dropped '$dn'\n"; }
    catch (Throwable $e) { /* not present */ }
}

// Create: type=1 for INSERT
try {
    $dict->createTrigger($name, 'leases', 1, $body);
    echo "Created trigger '$name'\n";
} catch (Throwable $e) {
    echo "ERROR creating trigger: " . $e->getMessage() . "\n";
}

// Verify it's there
$st = $conn->query("SELECT TRIG_NAME, TABLE_NAME, EVENT_MASK FROM system.triggers WHERE TABLE_NAME='leases' ORDER BY TRIG_NAME");
echo "\nTriggers on leases:\n";
while ($r = $st->fetchAssoc()) {
    echo "  {$r['TRIG_NAME']} (event={$r['EVENT_MASK']})\n";
}
$conn->close();
