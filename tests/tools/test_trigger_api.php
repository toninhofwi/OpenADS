<?php
// Test that getTriggerProperty (used by trigger_body.php) works with JSON-format triggers
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$conn = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);
$dict = AdsDictionary::fromConnection($conn);

$tests = [
    'leases::Insert AuditLog'       => 'leases',
    'properties::Delete AuditLog'   => 'properties',
    'reminders::Create Recurrance'  => 'reminders',
];

$pass = 0; $fail = 0;
foreach ($tests as $key => $table) {
    $evRaw   = $dict->getTriggerProperty($key, 1401);
    $timRaw  = $dict->getTriggerProperty($key, 1402);
    $optsRaw = $dict->getTriggerProperty($key, 1407);
    $body    = $dict->getTriggerProperty($key, 1404);

    $event = strlen($evRaw)   >= 4 ? unpack('V', substr($evRaw,   0, 4))[1] : 0;
    $timing = strlen($timRaw) >= 4 ? unpack('V', substr($timRaw,  0, 4))[1] : 0;
    $opts   = strlen($optsRaw) >= 4 ? unpack('V', substr($optsRaw, 0, 4))[1] : 0;
    $bodyLen = strlen(rtrim($body));

    $eventMap = [1=>'INSERT',2=>'UPDATE',3=>'DELETE'];
    $timingMap = [1=>'BEFORE',2=>'INSTEAD OF',4=>'AFTER'];

    $ok = $bodyLen > 0;
    echo ($ok ? '[PASS]' : '[FAIL]') . " $key\n";
    echo "  event=" . ($eventMap[$event] ?? $event) . " timing=" . ($timingMap[$timing] ?? $timing) . " opts=$opts body=$bodyLen chars\n";
    echo "  body[0..80]: " . substr(rtrim($body), 0, 80) . "\n";
    if ($ok) $pass++; else $fail++;
}
$conn->close();
echo "\nPassed: $pass  Failed: $fail\n";
