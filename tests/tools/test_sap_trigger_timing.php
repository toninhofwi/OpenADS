<?php
// Check SAP trigger timing for leases (run with php_sapads.ini)
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';

$opts = ['path'=>'F:/OpenADS/testdata/pmsys/pmsys.add','user'=>'adssys','password'=>'pmsys'];
$conn = AdsConnection::connect($opts);
$dict = AdsDictionary::fromConnection($conn);

echo "=== SAP triggers for leases ===\n";
$stmt = $conn->query("SELECT TRIG_NAME, TABLE_NAME FROM system.triggers WHERE TABLE_NAME = 'leases'");
while ($row = $stmt->fetchAssoc()) {
    $key = $row['TRIG_NAME'];
    $evRaw  = $dict->getTriggerProperty($key, 1401);
    $timRaw = $dict->getTriggerProperty($key, 1402);
    $ev  = strlen($evRaw)  >= 4 ? unpack('V', substr($evRaw, 0, 4))[1] : 0;
    $tim = strlen($timRaw) >= 4 ? unpack('V', substr($timRaw, 0, 4))[1] : 0;
    echo "  [{$key}] ev=$ev tim=$tim\n";
}
$stmt->close();
$conn->close();
