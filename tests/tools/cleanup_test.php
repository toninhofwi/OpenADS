<?php
$conn = AdsConnection::connect(['path'=>'F:\\OpenADS\\testdata\\pmsys\\pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);
$conn->prepare("DELETE FROM catcodes WHERE CatCode = 'ZZ_ADITEST'")->execute();
echo "Cleaned\n";
$conn->close();
