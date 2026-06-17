<?php
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$conn = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);
$st = $conn->query("SELECT TOP 3 [table],TableKey,Action FROM auditlog ORDER BY Creation DESC");
echo "Last 3 auditlog rows:\n";
while ($r = $st->fetchAssoc()) {
    echo "  table={$r['table']} TableKey={$r['TableKey']} action={$r['Action']}\n";
}
$conn->close();
