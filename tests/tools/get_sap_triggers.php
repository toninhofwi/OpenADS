<?php
// Dump all trigger metadata from SAP pmsys.add
// Body via getTriggerProperty(1404), metadata from system.triggers columns
$c = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys.add','user'=>'adssys','password'=>'pmsys']);
$d = AdsDictionary::fromConnection($c);

$st = $c->query('SELECT Name, Trig_TableName, Trig_Event_Type, Trig_Trigger_Type, Trig_Options FROM system.triggers ORDER BY Trig_TableName, Name');
$rows = [];
while ($row = $st->fetchAssoc()) { $rows[] = $row; }

foreach ($rows as $t) {
    $key = $t['Trig_TableName'] . '::' . $t['Name'];
    $body = '';
    try {
        $body = $d->getTriggerProperty($key, 1404);
    } catch (Exception $ex) {
        $body = '[ERROR: ' . $ex->getMessage() . ']';
    }
    echo json_encode([
        'table'   => $t['Trig_TableName'],
        'name'    => $t['Name'],
        'event'   => $t['Trig_Event_Type'],    // 1=INSERT 2=UPDATE 4=DELETE
        'timing'  => $t['Trig_Trigger_Type'],  // 1=BEFORE 2=INSTEAD_OF 4=AFTER
        'options' => $t['Trig_Options'],
        'body'    => $body,
    ], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES) . "\n";
}
$c->close();
