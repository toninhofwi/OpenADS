<?php
$conn = AdsConnection::connect(['path'=>'F:\\OpenADS\\testdata\\pmsys\\pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);
// Schema
$rs = $conn->prepare('SELECT TOP 1 * FROM propertytransactions')->execute();
echo "Columns: "; print_r(array_keys($rs->fetchAssoc()));
// Current smallest Amount
$rs = $conn->prepare('SELECT TOP 3 RECNO() AS rno, Amount, PropertyID FROM propertytransactions ORDER BY Amount')->execute();
echo "Smallest 3 by Amount:\n"; foreach ($rs->fetchAll() as $r) print_r($r);
// Count
$rs = $conn->prepare('SELECT COUNT(*) AS n FROM propertytransactions')->execute();
echo "Count: " . $rs->fetchAssoc()['n'] . "\n";
$conn->close();
