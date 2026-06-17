<?php
// Try reading pmsys.add directly with OpenADS to see if proc bodies are there
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$conn = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys.add','user'=>'adssys','password'=>'pmsys']);

$st = $conn->query("SELECT PROC_NAME, CONTAINER, PROCEDURE FROM system.storedprocedures ORDER BY PROC_NAME");
while ($r = $st->fetchAssoc()) {
    $body = $r['CONTAINER'] ?? '';
    $proc = $r['PROCEDURE'] ?? '';
    if (strlen($body) > 0 || strlen($proc) > 0) {
        echo "--- {$r['PROC_NAME']} ---\n";
        if ($body) echo "CONTAINER: " . substr($body, 0, 500) . "\n";
        if ($proc) echo "PROCEDURE: " . substr($proc, 0, 500) . "\n";
        echo "\n";
    } else {
        echo "{$r['PROC_NAME']}: (empty)\n";
    }
}
$conn->close();
