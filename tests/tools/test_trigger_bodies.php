<?php
/**
 * test_trigger_bodies.php
 *
 * Compares trigger bodies between:
 *   - pmsys.add   (SAP ADS, via php_sapads on port 8080/SAP ini)
 *   - pmsys_imported.add (OpenADS, via php_openads on port 8080/OpenADS ini)
 *
 * Usage: php -c C:/php/conf-openads/php.ini test_trigger_bodies.php
 *
 * The test connects to both DDs with their respective PHP extensions and
 * confirms that every trigger body in pmsys_imported.add matches the
 * corresponding one in pmsys.add (after normalising whitespace).
 *
 * Requires both php_openads and php_advantage extensions to be loadable.
 * If only one is available, that side is skipped with a warning.
 */

$OPENADS_DD  = 'F:/OpenADS/testdata/pmsys/pmsys_imported.add';
$SAP_DD      = 'F:/OpenADS/testdata/pmsys/pmsys.add';
$DD_PASSWORD = 'pmsys';

// ─── helpers ──────────────────────────────────────────────────────────────────

function normalise(string $s): string {
    // collapse runs of whitespace (spaces, tabs, CRLF, LF) to a single space
    return preg_replace('/\s+/', ' ', trim($s));
}

function pass(string $msg): void { echo "[PASS] $msg\n"; }
function fail(string $msg): void { echo "[FAIL] $msg\n"; }
function warn(string $msg): void { echo "[WARN] $msg\n"; }
function info(string $msg): void { echo "[INFO] $msg\n"; }

// ─── fetch trigger bodies from a DD ───────────────────────────────────────────

/**
 * Returns array: trigger_name => body_sql
 * Works with both AdsConnection (OpenADS) and AdvantageConnection (SAP).
 */
function fetch_trigger_bodies(string $ddPath, string $password, string $connClass): array {
    if (!class_exists($connClass)) {
        warn("$connClass not available — skipping $ddPath");
        return [];
    }

    try {
        $conn = $connClass::connect([
            'path'     => $ddPath,
            'user'     => 'adssys',
            'password' => $password,
        ]);
    } catch (Throwable $e) {
        warn("Cannot connect to $ddPath via $connClass: " . $e->getMessage());
        return [];
    }

    $bodies = [];
    try {
        $stmt = $conn->query(
            "SELECT TRIG_NAME, TRIG_BODY FROM system.triggers ORDER BY TRIG_NAME"
        );
        while ($row = $stmt->fetchAssoc()) {
            $name = trim($row['TRIG_NAME'] ?? '');
            $body = $row['TRIG_BODY'] ?? '';
            if ($name !== '') $bodies[$name] = $body;
        }
    } catch (Throwable $e) {
        warn("system.triggers query failed on $ddPath: " . $e->getMessage());
    } finally {
        $conn->close();
    }
    return $bodies;
}

// ─── main ─────────────────────────────────────────────────────────────────────

echo "=== Trigger body comparison: SAP pmsys.add vs OpenADS pmsys_imported.add ===\n\n";

$sapBodies  = fetch_trigger_bodies($SAP_DD,     $DD_PASSWORD, 'AdvantageConnection');
$oadsBodies = fetch_trigger_bodies($OPENADS_DD, $DD_PASSWORD, 'AdsConnection');

if (empty($sapBodies) && empty($oadsBodies)) {
    fail("Both DDs unreachable — nothing to compare.");
    exit(1);
}

if (empty($sapBodies)) {
    warn("SAP DD unavailable — will only dump OpenADS trigger bodies.");
}
if (empty($oadsBodies)) {
    warn("OpenADS DD unavailable — will only dump SAP trigger bodies.");
}

// ─── collect all trigger names from both sides ────────────────────────────────

$allNames = array_unique(array_merge(array_keys($sapBodies), array_keys($oadsBodies)));
sort($allNames);

$pass = 0;
$failures = [];

foreach ($allNames as $name) {
    $sapBody  = $sapBodies[$name]  ?? null;
    $oadsBody = $oadsBodies[$name] ?? null;

    if ($sapBody === null) {
        warn("$name — only in OpenADS (not in SAP)");
        continue;
    }
    if ($oadsBody === null) {
        warn("$name — only in SAP (not in OpenADS)");
        continue;
    }

    $sapNorm  = normalise($sapBody);
    $oadsNorm = normalise($oadsBody);

    if ($sapNorm === $oadsNorm) {
        pass($name);
        $pass++;
    } else {
        fail($name);
        $failures[] = [
            'name'  => $name,
            'sap'   => $sapBody,
            'oads'  => $oadsBody,
        ];
    }
}

// ─── detailed diffs for failures ──────────────────────────────────────────────

if (!empty($failures)) {
    echo "\n=== DETAILED DIFFS ===\n";
    foreach ($failures as $f) {
        echo "\n--- SAP   [{$f['name']}] ---\n" . rtrim($f['sap'])  . "\n";
        echo "\n--- OADS  [{$f['name']}] ---\n" . rtrim($f['oads']) . "\n";
        echo str_repeat('-', 60) . "\n";
    }
}

// ─── summary ──────────────────────────────────────────────────────────────────

$total    = count($allNames);
$failCnt  = count($failures);
echo "\n=== SUMMARY: $pass/$total passed, $failCnt failed ===\n";
exit($failCnt > 0 ? 1 : 0);
