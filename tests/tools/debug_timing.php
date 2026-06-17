<?php
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$conn = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);
$dict = AdsDictionary::fromConnection($conn);

echo "=== system.triggers TIMING column ===\n";
$st = $conn->query("SELECT TRIG_NAME, TABLE_NAME, EVENT_MASK, TIMING, EVENT FROM system.triggers ORDER BY TRIG_NAME");
while ($r = $st->fetchAssoc()) {
    echo "  {$r['TRIG_NAME']}: EVENT_MASK={$r['EVENT_MASK']} TIMING=" . var_export($r['TIMING'],true) . " EVENT=" . var_export($r['EVENT'],true) . "\n";
}
$st->close();

echo "\n=== getTriggerProperty(name, 1401/1402) ===\n";
$st2 = $conn->query("SELECT TRIG_NAME FROM system.triggers ORDER BY TRIG_NAME");
$names = [];
while ($r = $st2->fetchAssoc()) $names[] = $r['TRIG_NAME'];
$st2->close();

foreach ($names as $n) {
    $evRaw  = $dict->getTriggerProperty($n, 1401);
    $timRaw = $dict->getTriggerProperty($n, 1402);
    $ev  = strlen($evRaw)  >= 4 ? unpack('V', substr($evRaw,  0, 4))[1] : '(short:'.strlen($evRaw).')';
    $tim = strlen($timRaw) >= 4 ? unpack('V', substr($timRaw, 0, 4))[1] : '(short:'.strlen($timRaw).')';
    echo "  $n: event=$ev timing=$tim\n";
}
$conn->close();
