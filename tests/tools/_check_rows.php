<?php
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$conn = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);
// Look for our test rows specifically
$st = $conn->query("SELECT [table],TableKey,Action FROM auditlog WHERE TableKey='TRIG_TEST_1' OR TableKey='TEST_DIRECT' OR TableKey='TEST_BRACKET'");
while ($r = $st->fetchAssoc()) {
    echo "table=" . var_export($r['table'],true) . " TableKey={$r['TableKey']} action={$r['Action']}\n";
}
echo "Done\n";
$conn->close();
