<?php
$conn = AdsConnection::connect(['path'=>'F:\\OpenADS\\testdata\\pmsys\\pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);
// Look for our row by Amount without ORDER BY (table scan)
$rs = $conn->prepare("SELECT TOP 5 RECNO() AS rno, PTKey, propertyID, Amount FROM propertytransactions WHERE Amount < -5000")->execute();
$rows = $rs->fetchAll();
echo "Rows with Amount < -5000:\n";
foreach ($rows as $r) print_r($r);
echo count($rows) . " rows\n";

// Also get max PTKey to see what recno was assigned
$rs = $conn->prepare("SELECT MAX(PTKey) AS m FROM propertytransactions")->execute();
print_r($rs->fetchAssoc());
$conn->close();
