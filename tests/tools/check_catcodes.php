<?php
$conn = AdsConnection::connect(['path'=>'F:\\OpenADS\\testdata\\pmsys\\pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);
$rs = $conn->prepare('SELECT TOP 1 * FROM catcodes')->execute();
print_r($rs->fetchAssoc());
$conn->close();
