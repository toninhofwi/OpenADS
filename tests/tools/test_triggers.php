<?php
/**
 * test_triggers.php — verify trigger execution on INSERT/DELETE.
 *
 * Uses pmsys_test2.add (a copy of pmsys_test.add) to avoid corrupting live data.
 * Creates a simple AFTER INSERT trigger on 'leases' that INSERTs a sentinel row
 * into 'auditlog', fires the trigger by inserting into 'leases', then verifies
 * the auditlog row was written.
 *
 * Run:  php -c "C:/php/conf-openads/php.ini" tests/tools/test_triggers.php
 */
ini_set('display_errors', 1);
error_reporting(E_ALL);
require_once __DIR__ . '/../../DA-Web/api/openads_stubs.php';

$DD  = 'F:/OpenADS/testdata/pmsys/pmsys_test2.add';
// Action field is 15 chars; keep sentinel short.
$SENTINEL = 'TT_' . getmypid();

// ── helper ────────────────────────────────────────────────────────────────────
function check(bool $ok, string $msg): void {
    echo ($ok ? '[PASS]' : '[FAIL]') . ' ' . $msg . PHP_EOL;
    if (!$ok) exit(1);
}

function conn(): AdsConnection {
    global $DD;
    return AdsConnection::connect(['path' => $DD]);
}

// ── 1. Ensure pmsys_test2.add exists (copy from pmsys_test.add if needed) ───
if (!file_exists($DD)) {
    $src = dirname($DD) . '/pmsys_test.add';
    copy($src, $DD);
    copy(str_replace('.add', '.am', $src), str_replace('.add', '.am', $DD));
    echo "Copied pmsys_test.add -> pmsys_test2.add" . PHP_EOL;
}

// ── 2. Register a simple trigger (no __new/__old) ────────────────────────────
$c = conn();
$d = AdsDictionary::fromConnection($c);

$trigName = 'Test Insert Trigger';

// Remove stale trigger from previous run (ignore errors)
try { $d->dropTrigger($trigName); } catch (Throwable) {}

// INSERT INTO auditlog after an INSERT on leases — no __new/__old, no reserved-word columns
// Use Action column (regular char field) for the sentinel so WHERE comparison works.
// Changes is a Memo field; WHERE on Memo is unsupported.
$body = "INSERT INTO auditlog (Action,Changes) VALUES ('$SENTINEL','trigger fired')";

// createTrigger(name, table, type=1(stored proc container), container, procedure, priority)
$d->createTrigger($trigName, 'leases', 1, $body, '', 1);

// Set timing=AFTER(4), event=INSERT(1), enabled=Yes
$d->setTriggerProperty($trigName, 1402, '4');   // AFTER
$d->setTriggerProperty($trigName, 502,  '1');   // INSERT
$d->setTriggerProperty($trigName, 505,  'Yes'); // enabled

check(true, "Created trigger '$trigName'");
$c->close();

// ── 3. Count auditlog rows before ────────────────────────────────────────────
$c = conn();
$stmt = $c->query("SELECT COUNT(*) AS cnt FROM auditlog WHERE Action='$SENTINEL'");
$before = (int)($stmt->fetchAssoc()['cnt'] ?? 0);
$stmt->close();
$c->close();

check($before === 0, "No sentinel rows in auditlog before insert (found $before)");

// ── 4. INSERT a row into leases to fire the trigger ──────────────────────────
$c = conn();
try {
    $c->execute("INSERT INTO leases (leaseid, propertyID, LandLordID, ManagerID, TenantName) VALUES ('LS00-TRIGTEST','TRIG_PROP','TRIG_LL','TRIG_MGR','TRIG_TENANT')");
    check(true, "Inserted row into leases");
} catch (Throwable $e) {
    echo "[INFO] leases INSERT threw: " . $e->getMessage() . PHP_EOL;
    check(false, "leases INSERT failed: " . $e->getMessage());
}
$c->close();

// ── 5. Verify trigger fired ───────────────────────────────────────────────────
$c = conn();
$stmt = $c->query("SELECT COUNT(*) AS cnt FROM auditlog WHERE Action='$SENTINEL'");
$after = (int)($stmt->fetchAssoc()['cnt'] ?? 0);
$stmt->close();
$c->close();

check($after > 0, "Trigger fired: $after sentinel row(s) in auditlog after insert");

// ── 6. Cleanup ────────────────────────────────────────────────────────────────
$c = conn();
$d = AdsDictionary::fromConnection($c);
try { $d->dropTrigger($trigName); echo "[PASS] Trigger deleted" . PHP_EOL; }
catch (Throwable $e) { echo "[WARN] Could not delete trigger: " . $e->getMessage() . PHP_EOL; }
// Remove the leases test row
try { $c->execute("DELETE FROM leases WHERE leaseid='LS00-TRIGTEST'"); echo "[PASS] Leases test row deleted" . PHP_EOL; }
catch (Throwable $e) { echo "[WARN] Could not delete leases test row: " . $e->getMessage() . PHP_EOL; }
// Remove the auditlog sentinel rows
try { $c->execute("DELETE FROM auditlog WHERE Action='$SENTINEL'"); echo "[PASS] Auditlog sentinel row(s) deleted" . PHP_EOL; }
catch (Throwable $e) { echo "[WARN] Could not delete auditlog sentinel: " . $e->getMessage() . PHP_EOL; }
$c->close();

echo PHP_EOL . "All trigger tests passed." . PHP_EOL;
