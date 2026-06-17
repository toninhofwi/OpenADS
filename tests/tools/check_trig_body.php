<?php
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$conn = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);
echo "=== Stored Procedures ===\n";
$st = $conn->query("SELECT PROC_NAME, CONTAINER, PROCEDURE, INPUT, OUTPUT FROM system.storedprocedures ORDER BY PROC_NAME");
while ($r = $st->fetchAssoc()) {
    echo "--- {$r['PROC_NAME']} ---\n";
    echo "INPUT: " . ($r['INPUT'] ?? '(null)') . "\n";
    echo "OUTPUT: " . ($r['OUTPUT'] ?? '(null)') . "\n";
    echo "CONTAINER: " . substr($r['CONTAINER'] ?? '', 0, 600) . "\n\n";
}
$conn->close();
