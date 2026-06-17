<?php
// Verify the plain-name trigger fallback fix in openace64.dll
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';

$opts = ['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys'];
$conn = AdsConnection::connect($opts);

echo "Extension loaded: " . (extension_loaded('openads') ? "YES" : "NO") . "\n";

$dict = AdsDictionary::fromConnection($conn);

// First confirm what triggers exist
echo "\n=== All triggers in pmsys_imported ===\n";
$stmt = $conn->query("SELECT TRIG_NAME, TABLE_NAME FROM system.triggers ORDER BY TABLE_NAME, TRIG_NAME");
$triggers = [];
while ($row = $stmt->fetchAssoc()) {
    echo "  [{$row['TABLE_NAME']}] {$row['TRIG_NAME']}\n";
    $triggers[] = $row;
}
$stmt->close();

if (empty($triggers)) {
    echo "(no triggers found)\n";
    $conn->close();
    exit;
}

// Pick first trigger and test both plain name and composite key
$first = $triggers[0];
$plainName = $first['TRIG_NAME'];
$compKey   = $first['TABLE_NAME'] . '::' . $plainName;

echo "\n=== Test plain name: '$plainName' ===\n";
try {
    $r = $dict->getTriggerProperty($plainName, 1401);
    echo "  OK: event_mask bytes=" . bin2hex($r) . "\n";
} catch (Throwable $e) {
    echo "  FAIL: " . $e->getMessage() . "\n";
}

echo "\n=== Test composite key: '$compKey' ===\n";
try {
    $r = $dict->getTriggerProperty($compKey, 1401);
    echo "  OK: event_mask bytes=" . bin2hex($r) . "\n";
} catch (Throwable $e) {
    echo "  FAIL: " . $e->getMessage() . "\n";
}

$conn->close();
