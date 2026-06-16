<?php
/**
 * Verify ADI recno fix: tables with > 255 rows must return correct records via index.
 * Run with: C:\php\php.exe -c C:\php\php_openads.ini test_adi_fix.php
 */
$ADD_PATH = 'F:\\OpenADS\\testdata\\pmsys\\pmsys_imported.add';
$conn = AdsConnection::connect(['path' => $ADD_PATH, 'user' => 'adssys', 'password' => 'pmsys']);
echo "Connected to OpenADS\n";

// propertytransactions has 12331 rows — recnos well above 255
// Query a few specific rows ordered by Amount and verify they come back correctly
$stmt = $conn->prepare("SELECT TOP 10 RECNO() AS rno, Amount FROM propertytransactions ORDER BY Amount");
$rs = $stmt->execute();
$rows = $rs->fetchAll();
$stmt->close();

echo "propertytransactions ORDER BY Amount (first 10):\n";
foreach ($rows as $i => $row)
    printf("  [%2d] recno=%6d  Amount=%s\n", $i, $row['rno'], $row['Amount']);

// Also test catcodes (small table, should still work)
$stmt = $conn->prepare("SELECT TOP 5 RECNO() AS rno, CatCode FROM catcodes ORDER BY CatCode");
$rs = $stmt->execute();
$rows = $rs->fetchAll();
$stmt->close();
echo "\ncatcodes ORDER BY CatCode (first 5):\n";
foreach ($rows as $i => $row)
    printf("  [%2d] recno=%4d  CatCode=%s\n", $i, $row['rno'], $row['CatCode']);

$conn->close();
echo "\nDONE\n";
