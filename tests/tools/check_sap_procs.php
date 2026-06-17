<?php
// Check stored proc bodies via SAP ACE (which can read pmsys.add directly)
// This runs with the SAP ACE PHP extension (no openads)
if (!function_exists('ads_connect')) { echo "No ADS function available\n"; exit; }

$conn = ads_connect('F:\\OpenADS\\testdata\\pmsys\\pmsys.add', 'adssys', 'pmsys');
if (!$conn) { echo "Connect failed\n"; exit; }

// Check sp_SaveIntoAuditLog procedure body
$st = ads_prepare($conn, "SELECT PROC_NAME, PROCEDURE FROM system.storedprocedures WHERE PROC_NAME = 'sp_SaveIntoAuditLog'");
if ($st && ads_execute($st)) {
    $r = ads_fetch_array($st, ADS_ASSOC);
    if ($r) {
        echo "PROC_NAME: {$r['PROC_NAME']}\n";
        echo "PROCEDURE: " . substr($r['PROCEDURE'] ?? '(null)', 0, 1000) . "\n";
    }
    ads_free_result($st);
}
ads_disconnect($conn);
