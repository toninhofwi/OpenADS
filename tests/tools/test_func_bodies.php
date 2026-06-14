<?php
require_once 'F:/OpenADS/DA-Web/api/openads_stubs.php';
$conn = AdsConnection::connect(['path'=>'F:/OpenADS/testdata/pmsys/pmsys_imported.add','user'=>'adssys','password'=>'pmsys']);
$dict = AdsDictionary::fromConnection($conn);

$u32 = fn($raw) => strlen($raw) >= 4 ? unpack('V', substr($raw, 0, 4))[1] : 0;

// Test functions via getFunctionProperty
echo "=== Functions (via getFunctionProperty) ===\n";
$funcNames = ['PhysPos','DaysInMonth','EoM','MonthsRented','CurrentLease','MonthsOnTheMarket','BoY','EoY','NewSeqKey'];
$pass = $fail = 0;
foreach ($funcNames as $name) {
    try {
        $retType = $dict->getFunctionProperty($name, 702); // return_type
        $inPar   = $dict->getFunctionProperty($name, 701); // input_params
        $body    = $dict->getFunctionProperty($name, 700); // implementation (body)
        $bodyLen = strlen(trim($body));
        if ($bodyLen > 0) {
            echo "[PASS] $name  ret=" . trim($retType) . "  in=" . substr(trim($inPar),0,30) . "  body=$bodyLen chars\n";
            $pass++;
        } else {
            echo "[FAIL] $name  body is empty\n";
            $fail++;
        }
    } catch (Throwable $e) {
        echo "[FAIL] $name: " . $e->getMessage() . "\n";
        $fail++;
    }
}
echo "\n";

// Test stored procedures via getProcProperty
echo "=== Procedures (via getProcProperty) ===\n";
$procNames = ['sp_GetPhysicalPath','sp_SaveIntoAuditLog','sp_movebaltonewlease',
              'sp_mgGetAllLocksAllTablesAllUsers','sp_ChargeLateFees',
              'sp_ChargeMonthlyRent','sp_createremforunchargedrents'];
foreach ($procNames as $name) {
    try {
        $body   = $dict->getProcProperty($name, 803); // procedure SQL body
        $inPar  = $dict->getProcProperty($name, 800); // input_params
        $outPar = $dict->getProcProperty($name, 801); // output_params
        $bodyLen = strlen(trim($body));
        if ($bodyLen > 0) {
            echo "[PASS] $name  in=" . substr(trim($inPar),0,25) . "  out=" . substr(trim($outPar),0,15) . "  body=$bodyLen chars\n";
            $pass++;
        } else {
            echo "[FAIL] $name  body is empty\n";
            $fail++;
        }
    } catch (Throwable $e) {
        echo "[FAIL] $name: " . $e->getMessage() . "\n";
        $fail++;
    }
}

echo "\nPassed: $pass  Failed: $fail\n";
$conn->close();
