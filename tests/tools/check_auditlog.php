<?php
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$conn = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);

// Get auditlog column names
$st = $conn->query("SELECT TOP 1 * FROM auditlog");
$r = $st->fetchAssoc();
if ($r) {
    echo "auditlog columns: " . implode(', ', array_keys($r)) . "\n\n";
    foreach ($r as $k => $v) echo "  $k: " . substr((string)$v, 0, 80) . "\n";
} else {
    echo "auditlog is empty\n";
    // Try to get column names from field info
    $st2 = $conn->query("SELECT * FROM system.columns WHERE TABLE_NAME='auditlog'");
    while ($r2 = $st2->fetchAssoc()) echo "  " . print_r($r2, true);
}
$conn->close();
