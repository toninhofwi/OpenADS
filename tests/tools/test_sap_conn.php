<?php
$ADD_PATH = 'F:\\OpenADS\\testdata\\pmsys\\pmsys.add';

$conn = AdsConnection::connect(['path' => $ADD_PATH, 'user' => 'adssys', 'password' => 'pmsys']);
echo "Connected OK\n";
echo "AdsConnection methods:\n";
print_r(get_class_methods($conn));
echo "\nAdsTable methods:\n";
// Try to figure out how to open a table
$refl = new ReflectionClass('AdsTable');
foreach ($refl->getMethods() as $m) {
    echo "  " . $m->getName() . "(" . implode(', ', array_map(fn($p) => '$'.$p->getName(), $m->getParameters())) . ")\n";
}
$conn->close();
