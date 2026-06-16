<?php
// Check SAP trigger properties directly via AdsDictionary (no system.triggers query)
// Run with: php -c C:\php\php_sapads.ini this_file.php
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';

$conn = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys.add','user'=>'adssys','password'=>'pmsys']);
$dict = AdsDictionary::fromConnection($conn);

// Known trigger names from previous test
$triggers = [
    ['table'=>'leases',     'name'=>'Insert AuditLog'],
    ['table'=>'leases',     'name'=>'Update AuditLog'],
    ['table'=>'properties', 'name'=>'Insert AuditLog'],
    ['table'=>'properties', 'name'=>'Update AuditLog'],
    ['table'=>'reminders',  'name'=>'Insert AuditLog'],
];
$timStr = fn($t) => match($t) { 1=>'BEFORE', 2=>'INSTEAD OF', 4=>'AFTER', default=>"($t)" };
$evStr  = fn($e) => match($e) { 1=>'INSERT', 2=>'UPDATE', 3=>'DELETE', default=>"($e)" };

foreach ($triggers as $t) {
    $key = $t['name'];  // SAP uses plain name
    echo "\n[{$t['table']}::{$t['name']}]\n";
    try {
        $evRaw  = $dict->getTriggerProperty($key, 1401);
        $timRaw = $dict->getTriggerProperty($key, 1402);
        $ev  = strlen($evRaw)  >= 4 ? unpack('V', substr($evRaw, 0, 4))[1] : 0;
        $tim = strlen($timRaw) >= 4 ? unpack('V', substr($timRaw, 0, 4))[1] : 0;
        echo "  timing=$tim (" . $timStr($tim) . ")  event=$ev (" . $evStr($ev) . ")\n";
    } catch (Throwable $e) { echo "  ERROR: " . $e->getMessage() . "\n"; }
}
$conn->close();
