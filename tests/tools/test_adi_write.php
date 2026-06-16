<?php
/**
 * ADI write-path smoke test: INSERT + SELECT (ORDER BY) + DELETE.
 * Run: C:\php\php.exe -c C:\php\php_openads.ini test_adi_write.php
 */
$ADD_PATH = 'F:\\OpenADS\\testdata\\pmsys\\pmsys_imported.add';
$conn = AdsConnection::connect(['path' => $ADD_PATH, 'user' => 'adssys', 'password' => 'pmsys']);
echo "Connected\n";

// ── 1. Count rows before insert ──────────────────────────────────────────────
$stmt = $conn->prepare("SELECT COUNT(*) AS n FROM catcodes");
$rs   = $stmt->execute();
$row  = $rs->fetchAssoc();
$stmt->close();
$before = (int)$row['n'];
echo "catcodes rows before: $before\n";

// ── 2. INSERT a test row ──────────────────────────────────────────────────────
$stmt = $conn->prepare(
    "INSERT INTO catcodes (CatCode, Descript) VALUES ('ZZ_ADITEST', 'ADI write test')");
$stmt->execute();
$stmt->close();
echo "INSERT done\n";

// ── 3. SELECT via index to confirm row is visible in sorted order ─────────────
$stmt = $conn->prepare(
    "SELECT RECNO() AS rno, CatCode, Descript FROM catcodes ORDER BY CatCode");
$rs   = $stmt->execute();
$rows = $rs->fetchAll();
$stmt->close();

$found = false;
foreach ($rows as $i => $r) {
    if (trim($r['CatCode']) === 'ZZ_ADITEST') {
        printf("  FOUND at sorted pos %d: recno=%d CatCode=%s Desc=%s\n",
               $i, $r['rno'], trim($r['CatCode']), trim($r['Descript']));
        $found = true;
        break;
    }
}
if (!$found) { echo "ERROR: inserted row not found via index\n"; exit(1); }

// ── 4. Verify count increased ────────────────────────────────────────────────
$stmt = $conn->prepare("SELECT COUNT(*) AS n FROM catcodes");
$rs   = $stmt->execute();
$row  = $rs->fetchAssoc();
$stmt->close();
$after = (int)$row['n'];
echo "catcodes rows after insert: $after  (delta=" . ($after - $before) . ")\n";
if ($after !== $before + 1) { echo "ERROR: count mismatch\n"; exit(1); }

// ── 5. DELETE the test row ────────────────────────────────────────────────────
$stmt = $conn->prepare("DELETE FROM catcodes WHERE CatCode = 'ZZ_ADITEST'");
$stmt->execute();
$stmt->close();
echo "DELETE done\n";

// ── 6. Verify row gone and count restored ────────────────────────────────────
$stmt = $conn->prepare("SELECT COUNT(*) AS n FROM catcodes");
$rs   = $stmt->execute();
$row  = $rs->fetchAssoc();
$stmt->close();
$final = (int)$row['n'];
echo "catcodes rows after delete: $final\n";
if ($final !== $before) { echo "ERROR: count did not restore\n"; exit(1); }

// ── 7. Also test a larger table (>255 rows) via INSERT+SELECT+DELETE ─────────
echo "\nTesting propertytransactions (large table, recno>255)...\n";
$stmt = $conn->prepare("SELECT COUNT(*) AS n FROM propertytransactions");
$rs   = $stmt->execute(); $row = $rs->fetchAssoc(); $stmt->close();
$pt_before = (int)$row['n'];
echo "  rows before: $pt_before\n";

$stmt = $conn->prepare(
    "INSERT INTO propertytransactions (PropertyID, Amount) VALUES (999999, -9999.99)");
$stmt->execute(); $stmt->close();
echo "  INSERT done\n";

$stmt = $conn->prepare(
    "SELECT TOP 1 RECNO() AS rno, Amount FROM propertytransactions ORDER BY Amount");
$rs   = $stmt->execute(); $row = $rs->fetchAssoc(); $stmt->close();
printf("  Smallest Amount row: recno=%d Amount=%s\n", $row['rno'], $row['Amount']);

$stmt = $conn->prepare(
    "DELETE FROM propertytransactions WHERE PropertyID = 999999 AND Amount = -9999.99");
$stmt->execute(); $stmt->close();
echo "  DELETE done\n";

$stmt = $conn->prepare("SELECT COUNT(*) AS n FROM propertytransactions");
$rs   = $stmt->execute(); $row = $rs->fetchAssoc(); $stmt->close();
echo "  rows after: " . (int)$row['n'] . "\n";

$conn->close();
echo "\nALL TESTS PASSED\n";
