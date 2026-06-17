<?php
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$conn = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);
$dict = AdsDictionary::fromConnection($conn);
$st = $conn->query("SELECT TRIG_NAME FROM system.triggers ORDER BY TRIG_NAME");
while ($r = $st->fetchAssoc()) echo "EXISTS: {$r['TRIG_NAME']}\n";
foreach (['leases::leases::OpenADS Test Insert', 'OpenADS Test Insert', 'leases::OpenADS Test Insert'] as $n) {
    try { $dict->dropTrigger($n); echo "Dropped: $n\n"; break; }
    catch (Throwable $e) { echo "Cannot drop '$n': " . $e->getMessage() . "\n"; }
}
$conn->close();
