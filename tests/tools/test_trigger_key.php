<?php
// Test trigger key handling in trigger_body.php
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';

$opts = ['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys'];
$conn = AdsConnection::connect($opts);
$dict = AdsDictionary::fromConnection($conn);

$table = 'leases';
$stmt = $conn->query("SELECT TRIG_NAME FROM system.triggers WHERE TABLE_NAME = 'leases'");
while ($row = $stmt->fetchAssoc()) {
    $trigName = $row['TRIG_NAME'];
    $trigKey  = $table . '::' . $trigName;  // BUG: double-qualifies
    echo "trigName = $trigName\n";
    echo "trigKey  = $trigKey\n";

    echo "  Buggy key result: ";
    try {
        $body = $dict->getTriggerProperty($trigKey, 1404);
        echo "OK (" . strlen($body) . " bytes)\n";
    } catch (Throwable $e) { echo "ERROR: " . $e->getMessage() . "\n"; }

    echo "  Fixed key result: ";
    try {
        $body2 = $dict->getTriggerProperty($trigName, 1404);
        echo "OK (" . strlen($body2) . " bytes), first 80: " . substr(trim($body2), 0, 80) . "\n";
    } catch (Throwable $e) { echo "ERROR: " . $e->getMessage() . "\n"; }
}
$stmt->close();
$conn->close();
echo "Done.\n";
