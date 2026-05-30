<?php
/**
 * api/gen_sql.php — generate a CREATE TABLE DDL script for a DD table.
 * GET ?dd=&table=
 *
 * Returns { sql: "..." } with a full SAP-compatible DDL script including
 * field definitions, index creation calls, table/field property calls,
 * and trigger definitions.
 */
header('Content-Type: application/json');
session_start();

$ddName = trim($_GET['dd']    ?? '');
$table  = trim($_GET['table'] ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if (!preg_match('/^[A-Za-z_][A-Za-z0-9_ ]*$/', $table)) {
    http_response_code(400);
    echo json_encode(['error' => 'invalid table name']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

// ── binary .add helpers ─────────────────────────────────────────────────────
function gble16(string $d, int $o): int { return unpack('v', substr($d, $o, 2))[1]; }
function gble32(string $d, int $o): int { return unpack('V', substr($d, $o, 4))[1]; }

function readTriggersFromBinary(string $addPath, string $table): array {
    $data = @file_get_contents($addPath);
    if ($data === false) return [];
    $amPath = preg_replace('/\.[^.\/\\\\]+$/', '.am', $addPath);
    $am = is_file($amPath) ? (@file_get_contents($amPath) ?: '') : '';

    $hdrLen = gble32($data, 0x20);
    $recLen = gble32($data, 0x24);
    if ($recLen === 0) return [];
    $total = intdiv(strlen($data) - $hdrLen, $recLen);

    $idToName = [];
    for ($i = 0; $i < $total; $i++) {
        $base = $hdrLen + $i * $recLen;
        if (ord($data[$base]) !== 0x04) continue;
        $oid = gble32($data, $base + 5);
        $nm  = rtrim(substr($data, $base + 23, 200), " \0");
        if ($nm !== '') $idToName[$oid] = $nm;
    }

    $trigs = [];
    for ($i = 0; $i < $total; $i++) {
        $base = $hdrLen + $i * $recLen;
        if (ord($data[$base]) !== 0x04) continue;
        $ot = rtrim(substr($data, $base + 13, 10), " \0");
        if ($ot !== 'Trigger') continue;
        $trigName = rtrim(substr($data, $base + 23, 200), " \0");
        $parentId = gble32($data, $base + 9);
        if (strcasecmp($idToName[$parentId] ?? '', $table) !== 0) continue;

        $PA     = $base + 225;
        $event  = gble32($data, $PA + 0);
        $timing = gble16($data, $PA + 6);

        $inlineStart = $PA + 18;
        $inlineEnd   = min($PA + 273, strlen($data));
        $inline = substr($data, $inlineStart, $inlineEnd - $inlineStart);
        $nul = strpos($inline, "\0");
        if ($nul !== false) $inline = substr($inline, 0, $nul);

        $amBlock = gble32($data, $base + 498);
        $amLen2  = gble32($data, $base + 502);
        $amPart  = '';
        if ($amBlock > 0 && $amLen2 > 0 && $am !== '') {
            $amOff  = $amBlock * 8;
            $amPart = substr($am, $amOff, $amLen2);
            $l = strlen($amPart);
            while ($l > 0) {
                $b = ord($amPart[$l - 1]);
                if (($b >= 0x20 && $b <= 0x7E) || $b === 0x09 || $b === 0x0A || $b === 0x0D) break;
                $l--;
            }
            $amPart = substr($amPart, 0, $l);
        }

        $timingStr = match($timing) { 1=>'BEFORE', 2=>'INSTEAD OF', 4=>'AFTER', default=>'' };
        $eventStr  = match($event)  { 1=>'INSERT', 2=>'UPDATE', 3=>'DELETE', default=>'' };
        $fullBody  = rtrim($inline . $amPart);

        $trigs[] = [
            'name'    => $trigName,
            'timing'  => $timingStr,
            'event'   => $eventStr,
            'body'    => $fullBody,
            'priority'=> 1,
            'noMemos' => true,
        ];
    }
    return $trigs;
}

// ── ADS CREATE TABLE type strings ─────────────────────────────────────────
function createTypeStr(string $raw, int $len, int $dec): string {
    switch ($raw) {
        case 'C': return "CIChar( $len )";
        case 'N': return $dec > 0 ? "Numeric( $len, $dec )" : "Numeric( $len )";
        case 'F': return $dec > 0 ? "Float( $len, $dec )"   : "Float( $len )";
        case 'L': return 'Logical';
        case 'D': return 'Date';
        case 'M': return 'Memo';
        case 'I': return 'Integer';
        case 'Y': return 'Money';
        case 'B': return 'Double';
        case 'V': return "Varchar( $len )";
        case 'Q': return "Varbinary( $len )";
        case 'T': return 'DateTime';
        case '@': return 'TimeStamp';
        case '+': return 'AutoIncrement';
        case 'W': return 'Blob';
        case 'G': return 'Binary';
    }
    switch (ord(substr($raw, 0, 1))) {
        case  1: return 'Logical';
        case  3: return 'Date';
        case  4: return "Char( $len )";
        case  5: return 'Memo';
        case  6: return 'Binary';
        case 10: return 'Double';
        case 11: return 'Integer';
        case 12: return 'ShortInt';
        case 13: return 'Time';
        case 14: return 'TimeStamp';
        case 15: return 'AutoIncrement';
        case 18: return 'Money';
        case 20: return "CIChar( $len )";
        default: return $raw ?: 'Char(1)';
    }
}


try {
    $conn = AdsConnection::connect($opts);
    $lines = [];

    // ── 1. Field definitions ────────────────────────────────────────────────
    $stmt = $conn->query("SELECT TABLE_NAME, COL_NAME, COL_NUM, COL_TYPE, COL_LEN, COL_DEC FROM system.columns");
    $fields = [];
    while ($row = $stmt->fetchAssoc()) {
        if (strcasecmp($row['TABLE_NAME'], $table) !== 0) continue;
        $fields[] = [
            'num'  => (int)$row['COL_NUM'],
            'name' => $row['COL_NAME'],
            'raw'  => trim($row['COL_TYPE']),
            'len'  => (int)$row['COL_LEN'],
            'dec'  => (int)$row['COL_DEC'],
        ];
    }
    usort($fields, fn($a, $b) => $a['num'] <=> $b['num']);

    if (empty($fields)) {
        $conn->close();
        echo json_encode(['sql' => "-- Table '$table' not found or has no columns."]);
        exit;
    }

    $lines[] = "CREATE TABLE $table (";
    $last = count($fields) - 1;
    foreach ($fields as $i => $f) {
        $typeStr = createTypeStr($f['raw'], $f['len'], $f['dec']);
        $comma   = ($i < $last) ? ',' : '';
        $lines[] = "      {$f['name']} $typeStr$comma";
    }
    $lines[] = "      ) IN DATABASE;";
    $lines[] = '';

    // ── 2. Index definitions ───────────────────────────────────────────────
    $primaryKeyTag = '';
    try {
        $dict = AdsDictionary::fromConnection($conn);
        $primaryKeyTag = strtoupper(trim($dict->getTableProperty($table, 209)));
    } catch (Throwable $e) {}

    try {
        $tbl  = AdsTable::open($conn, $table, 0);
        $tags = $tbl->getIndexTags();
        $tbl->close();

        // Derive index file name: same base as table + .adi
        $indexFile = strtolower($table) . '.adi';

        foreach ($tags as $t) {
            $tagUpper  = strtoupper($t['tag']);
            $isPrimary = ($primaryKeyTag !== '' && $tagUpper === $primaryKeyTag);
            $flags     = $isPrimary ? 2051 : 2;  // 2051≈unique+primary, 2=regular
            $lines[] = "EXECUTE PROCEDURE sp_CreateIndex90(";
            $lines[] = "   '$table',";
            $lines[] = "   '$indexFile',";
            $lines[] = "   '{$t['tag']}',";
            $lines[] = "   '{$t['expression']}',";
            $lines[] = "   '',";
            $lines[] = "   $flags,";
            $lines[] = "   512,";
            $lines[] = "   '' ); ";
            $lines[] = '';
            $lines[] = '';
        }
    } catch (Throwable $e) {}

    // ── 3. Table properties ────────────────────────────────────────────────
    $failTable = strtolower($table) . 'fail';
    $tblProps  = [];
    try {
        $dict3 = AdsDictionary::fromConnection($conn);
        // 209=Table_Primary_Key, 207=Table_Default_Index, 210=Table_Permission_Level
        $propCodes = [209 => 'Table_Primary_Key', 207 => 'Table_Default_Index', 210 => 'Table_Permission_Level'];
        foreach ($propCodes as $code => $propName) {
            try {
                $val = trim($dict3->getTableProperty($table, $code));
                if ($val !== '') {
                    $lines[] = "EXECUTE PROCEDURE sp_ModifyTableProperty( '$table', ";
                    $lines[] = "   '$propName', ";
                    $lines[] = "   '$val', 'APPEND_FAIL', '$failTable');";
                    $lines[] = '';
                }
            } catch (Throwable $e) {}
        }
    } catch (Throwable $e) {}

    // ── 4. Field properties (Required / Default) ───────────────────────────
    try {
        $dict4 = AdsDictionary::fromConnection($conn);
        foreach ($fields as $f) {
            $fieldName = $f['name'];
            $propLines = [];
            try {
                $req = trim($dict4->getFieldProperty($table, $fieldName, 305));
                if ($req !== '') {
                    // stored value is Field_Can_Be_Null; pass through as-is
                    $nullOk = $req;
                    $propLines[] = "EXECUTE PROCEDURE sp_ModifyFieldProperty ( '$table', ";
                    $propLines[] = "      '$fieldName', 'Field_Can_Be_Null', ";
                    $propLines[] = "      '$nullOk', 'APPEND_FAIL', '$failTable' ); ";
                }
            } catch (Throwable $e) {}
            try {
                $def = trim($dict4->getFieldProperty($table, $fieldName, 306));
                if ($def !== '') {
                    $propLines[] = "EXECUTE PROCEDURE sp_ModifyFieldProperty ( '$table', ";
                    $propLines[] = "      '$fieldName', 'Field_Default_Value', ";
                    $propLines[] = "      '$def', 'APPEND_FAIL', '$failTable' ); ";
                }
            } catch (Throwable $e) {}
            if (!empty($propLines)) {
                foreach ($propLines as $pl) $lines[] = $pl;
                $lines[] = '';
            }
        }
    } catch (Throwable $e) {}

    // ── 5. Triggers — read from binary .add/.am for correct timing + full body ──
    $trigs = readTriggersFromBinary($c['path'], $table);
    usort($trigs, fn($a, $b) => strcmp($a['name'], $b['name']));

    foreach ($trigs as $tr) {
        $name   = $tr['name'];
        $timing = $tr['timing'];
        $event  = $tr['event'];
        $body   = $tr['body'];
        $prio   = $tr['priority'];
        $noMemos = $tr['noMemos'] ? "\n   NO MEMOS " : '';

        $lines[] = "CREATE TRIGGER [$name]";
        $lines[] = "   ON $table";
        $lines[] = "   $timing ";
        $lines[] = "   $event ";
        $lines[] = "BEGIN ";
        if ($body !== '') {
            foreach (explode("\n", $body) as $bl) $lines[] = rtrim($bl);
        } else {
            $lines[] = "   -- (body unavailable)";
        }
        $lines[] = "END $noMemos";
        $lines[] = "   PRIORITY $prio;";
        $lines[] = '';
        $lines[] = '';
    }

    $conn->close();
    $sql = implode("\n", $lines);
    echo json_encode(['sql' => $sql], JSON_UNESCAPED_UNICODE | JSON_INVALID_UTF8_SUBSTITUTE);
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
}
