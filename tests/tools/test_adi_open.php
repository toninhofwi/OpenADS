<?php
$ADD_PATH = 'F:\\OpenADS\\testdata\\pmsys\\pmsys.add';
$conn = AdsConnection::connect(['path' => $ADD_PATH, 'user' => 'adssys', 'password' => 'pmsys']);

// Confirm basic SELECT works
$stmt = $conn->prepare("SELECT TOP 5 CatCode FROM catcodes ORDER BY CatCode");
$rs = $stmt->execute();
echo "SELECT catcodes (fetchAssoc):\n";
while ($row = $rs->fetchAssoc()) { print_r($row); }
$stmt->close();

// Test row number / recno functions
$funcs = [
    "RECNO()",
    "ROWID()",
    "ROWNUM()",
    "ROW_NUMBER() OVER (ORDER BY CatCode)",
];
foreach ($funcs as $fn) {
    try {
        $stmt = $conn->prepare("SELECT TOP 1 $fn AS rno, CatCode FROM catcodes");
        $rs = $stmt->execute();
        $row = $rs->fetchAssoc();
        echo "$fn => " . ($row ? print_r($row, true) : 'null') . "\n";
        $stmt->close();
    } catch (Exception $e) {
        echo "$fn FAILED: " . $e->getMessage() . "\n";
    }
}

$conn->close();
