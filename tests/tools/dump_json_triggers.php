<?php
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$conn = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);
$stmt = $conn->query("SELECT TRIG_NAME, TABLE_NAME, CONTAINER FROM system.triggers ORDER BY TABLE_NAME, TRIG_NAME");
while ($row = $stmt->fetchAssoc()) {
    $body = trim($row['CONTAINER'] ?? '');
    $key = $row['TABLE_NAME'] . '::' . $row['TRIG_NAME'];
    echo $key . ' (' . strlen($body) . ' chars): ' . substr($body, 0, 80) . "\n";
}
$conn->close();
echo "Done.\n";
