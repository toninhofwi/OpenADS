<?php
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$opts = ['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys'];
$conn = AdsConnection::connect($opts);

// Test: does WHERE GRANTEE reduce PHP-side fetch time?
$grantees = ["'RCB'", "'Administrators'", "'General'", "'Supervisors'", "'Internet'", "'adssys'"];
$inClause = implode(',', $grantees);

$t0 = microtime(true);
$stmt = $conn->query("SELECT * FROM system.permissions WHERE GRANTEE IN ($inClause)");
$t1 = microtime(true);
$n = 0;
while ($row = $stmt->fetchAssoc()) $n++;
$t2 = microtime(true);
$stmt->close();
echo "permissions WHERE GRANTEE IN (6 values): query=" . round(($t1-$t0)*1000) . "ms, fetch $n rows: " . round(($t2-$t1)*1000) . "ms\n";

// Compare: all rows
$t0 = microtime(true);
$stmt2 = $conn->query('SELECT * FROM system.permissions');
$t1 = microtime(true);
$m = 0;
while ($stmt2->fetchAssoc()) $m++;
$t2 = microtime(true);
$stmt2->close();
echo "permissions (no WHERE): query=" . round(($t1-$t0)*1000) . "ms, fetch $m rows: " . round(($t2-$t1)*1000) . "ms\n";

$conn->close();
