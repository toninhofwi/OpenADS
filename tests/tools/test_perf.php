<?php
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$opts = ['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys'];
$conn = AdsConnection::connect($opts);

$t0 = microtime(true);
$stmt = $conn->query("SELECT GROUP_NAME FROM system.usergroupmembers WHERE USER_NAME = 'RCB' ORDER BY GROUP_NAME");
$t1 = microtime(true);
$rows = [];
while ($row = $stmt->fetchAssoc()) $rows[] = $row['GROUP_NAME'];
$t2 = microtime(true);
$stmt->close();
echo "usergroupmembers query: " . round(($t1-$t0)*1000) . "ms, fetch: " . round(($t2-$t1)*1000) . "ms\n";
echo "Groups: " . implode(', ', $rows) . "\n\n";

$t0 = microtime(true);
$stmt2 = $conn->query('SELECT * FROM system.permissions');
$t1 = microtime(true);
$n = 0;
while ($row = $stmt2->fetchAssoc()) $n++;
$t2 = microtime(true);
$stmt2->close();
echo "permissions query: " . round(($t1-$t0)*1000) . "ms, fetch $n rows: " . round(($t2-$t1)*1000) . "ms\n\n";

$t0 = microtime(true);
$stmt3 = $conn->query("SELECT Name FROM system.tables ORDER BY Name");
$t1 = microtime(true);
$m = 0;
while ($stmt3->fetchAssoc()) $m++;
$t2 = microtime(true);
echo "tables query: " . round(($t1-$t0)*1000) . "ms, fetch $m rows: " . round(($t2-$t1)*1000) . "ms\n\n";

$conn->close();
echo "Done.\n";
