<?php
// Check: what tags does propertytransactions.adi have?
// Use list_tags via DA-Web API or probe the binary directly.
$ADD_PATH = 'F:\\OpenADS\\testdata\\pmsys\\pmsys_imported.add';
$conn = AdsConnection::connect(['path' => $ADD_PATH, 'user' => 'adssys', 'password' => 'pmsys']);

// ORDER BY Amount — what does it return?
$rs = $conn->prepare("SELECT TOP 5 Amount FROM propertytransactions ORDER BY Amount")->execute();
echo "ORDER BY Amount (top 5):\n";
foreach ($rs->fetchAll() as $r) echo "  " . $r['Amount'] . "\n";

// ORDER BY Amount DESC
$rs = $conn->prepare("SELECT TOP 5 Amount FROM propertytransactions ORDER BY Amount DESC")->execute();
echo "ORDER BY Amount DESC (top 5):\n";
foreach ($rs->fetchAll() as $r) echo "  " . $r['Amount'] . "\n";

// Scan for our inserted row specifically
$rs = $conn->prepare("SELECT PTKey, Amount FROM propertytransactions WHERE Amount = -9999.99")->execute();
$rows = $rs->fetchAll();
echo "Rows with Amount=-9999.99: " . count($rows) . "\n";
foreach ($rows as $r) print_r($r);

// Get the actual min amount by full scan
$rs = $conn->prepare("SELECT MIN(Amount) AS mn FROM propertytransactions")->execute();
echo "MIN(Amount): " . $rs->fetchAssoc()['mn'] . "\n";

$conn->close();
