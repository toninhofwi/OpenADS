<?php
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$opts = ['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys'];
$conn = AdsConnection::connect($opts);

// Test fetchAll() vs fetchAssoc() for system.permissions
$t0 = microtime(true);
$stmt = $conn->query('SELECT * FROM system.permissions');
$t1 = microtime(true);
$all = $stmt->fetchAll();
$t2 = microtime(true);
$stmt->close();
echo "permissions query: " . round(($t1-$t0)*1000) . "ms, fetchAll " . count($all) . " rows: " . round(($t2-$t1)*1000) . "ms\n";

// Test usergroupmembers fetchAll
$t0 = microtime(true);
$stmt2 = $conn->query('SELECT * FROM system.usergroupmembers');
$t1 = microtime(true);
$all2 = $stmt2->fetchAll();
$t2 = microtime(true);
$stmt2->close();
echo "usergroupmembers fetchAll " . count($all2) . " rows: " . round(($t2-$t1)*1000) . "ms\n";
echo "Sample: "; print_r(array_slice($all2, 0, 2));

$conn->close();
