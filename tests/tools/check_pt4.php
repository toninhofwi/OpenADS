<?php
$conn = AdsConnection::connect(['path'=>'F:\\OpenADS\\testdata\\pmsys\\pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);

// Does ORDER BY Amount return 12332 rows (including our insert) or 12331?
$rs = $conn->prepare("SELECT COUNT(*) AS n FROM propertytransactions")->execute();
$total = (int)$rs->fetchAssoc()['n'];
echo "Total rows: $total\n";

// Scan ORDER BY Amount looking for our -9999.99 row
$rs = $conn->prepare("SELECT Amount FROM propertytransactions ORDER BY Amount")->execute();
$all = $rs->fetchAll();
echo "Rows returned by ORDER BY Amount: " . count($all) . "\n";
$found_idx = -1;
foreach ($all as $i => $r) {
    if (abs((float)$r['Amount'] - (-9999.99)) < 0.001) { $found_idx = $i; break; }
}
echo "Our row (-9999.99) at index: " . ($found_idx >= 0 ? $found_idx : 'NOT FOUND') . "\n";

// Clean up the test row
$conn->prepare("DELETE FROM propertytransactions WHERE Amount = -9999.99")->execute();
$rs = $conn->prepare("SELECT COUNT(*) AS n FROM propertytransactions")->execute();
echo "Count after cleanup: " . $rs->fetchAssoc()['n'] . "\n";

$conn->close();
