<?php
/**
 * api/table_meta.php — field, index, or trigger metadata for a DD table.
 * GET ?dd=&table=&kind=fields|indexes|triggers
 */
header('Content-Type: application/json');
session_start();

$ddName = trim($_GET['dd']    ?? '');
$table  = trim($_GET['table'] ?? '');
$kind   = trim($_GET['kind']  ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if (!preg_match('/^[A-Za-z_][A-Za-z0-9_ ]*$/', $table) ||
    !in_array($kind, ['fields', 'indexes', 'triggers'], true)) {
    http_response_code(400);
    echo json_encode(['error' => 'invalid parameters']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

// ── binary .add helpers ─────────────────────────────────────────────────────
function le16b(string $d, int $o): int { return unpack('v', substr($d, $o, 2))[1]; }
function le32b(string $d, int $o): int { return unpack('V', substr($d, $o, 4))[1]; }

// ── DBF/ADT binary header field reader ─────────────────────────────────────
// Returns array of ['Field', 'Type' (letter), 'Size', 'Dec'] from a .dbf or .adt file.
function readDbfFields(string $filePath): array {
    $f = @file_get_contents($filePath, false, null, 0, 65536);
    if ($f === false || strlen($f) < 32) return [];

    $version  = ord($f[0]);
    $isAdt    = (strtolower(pathinfo($filePath, PATHINFO_EXTENSION)) === 'adt');
    $numRecs  = unpack('V', substr($f, 4, 4))[1];
    $hdrBytes = unpack('v', substr($f, 8, 2))[1];   // header size = first field descriptor offset
    $recLen   = unpack('v', substr($f, 10, 2))[1];

    $fields = [];
    $pos    = 32;   // first field descriptor starts here in standard DBF

    // For ADT: field descriptors start at a different offset
    // ADT header is 400 bytes, field descriptors at byte 400
    if ($isAdt) {
        $pos = 400;
        // ADT field descriptor: 128 bytes each
        // [0..127]: name(128), type(1), len(2 LE), dec(1), ...
        $numFields = ($hdrBytes - 400) > 0 ? intdiv($hdrBytes - 400, 128) : 0;
        for ($i = 0; $i < $numFields && $pos + 128 <= strlen($f); $i++, $pos += 128) {
            $name = rtrim(substr($f, $pos, 128), " \0");
            if ($name === '' || $name[0] === "\0") break;
            $typeB = ord($f[$pos + 128 + 0]) ?? 0;  // type is after the name? check ADT format
            // Simplified: ADT field type at a known offset
            // Actually ADT stores: name(128 bytes), type(1), reserved(1), length(2 LE), decimals(1)
            // at offset pos+128 from start of descriptor
            // This is approximate — real ADT format may differ
            $fields[] = [
                'Order' => $i + 1,
                'Field' => $name,
                'Type'  => '',
                'BaseType' => '',
                'Size'  => 0,
                'Decimals' => 0,
                'Required' => '',
                'Default'  => '',
                'Index'    => 'No',
            ];
        }
        return $fields;
    }

    // Standard DBF field descriptors: 32 bytes each, terminated by 0x0D
    $order = 1;
    while ($pos + 32 <= strlen($f)) {
        if (ord($f[$pos]) === 0x0D) break;   // terminator
        $name = rtrim(substr($f, $pos, 11), " \0");
        if ($name === '') { $pos += 32; continue; }
        $typeChar = $f[$pos + 11];
        $fldLen   = ord($f[$pos + 16]);
        $fldDec   = ord($f[$pos + 17]);
        $label    = fieldTypeLabel($typeChar, $fldLen, $fldDec);
        $base     = fieldBaseType($typeChar);
        $fields[] = [
            'Order'    => $order++,
            'Field'    => $name,
            'Type'     => $label,
            'BaseType' => $base,
            'Size'     => $fldLen,
            'Decimals' => $fldDec,
            'Required' => '',
            'Default'  => '',
            'Index'    => 'No',
        ];
        $pos += 32;
    }
    return $fields;
}

// ── field-type label (display) ──────────────────────────────────────────────
function fieldTypeLabel(string $raw, int $len, int $dec): string {
    switch ($raw) {
        case 'C': return "Character($len)";
        case 'N': return $dec > 0 ? "Numeric($len,$dec)" : "Numeric($len)";
        case 'F': return $dec > 0 ? "Float($len,$dec)"   : "Float($len)";
        case 'L': return 'Logical';
        case 'D': return 'Date';
        case 'M': return 'Memo';
        case 'I': return 'Integer';
        case 'Y': return 'Money';
        case 'B': return 'Double';
        case 'V': return "Varchar($len)";
        case 'Q': return "Varbinary($len)";
        case 'T': return 'DateTime';
        case '@': return 'Timestamp';
        case '+': return 'AutoIncrement';
        case 'W': return 'Blob';
        case 'G': return 'Binary';
    }
    switch (ord(substr($raw, 0, 1))) {
        case  1: return 'Logical';
        case  3: return 'Date';
        case  4: return "Character($len)";
        case  5: return 'Memo';
        case  6: return 'Binary';
        case 10: return 'Double';
        case 11: return 'Integer';
        case 12: return 'ShortInt';
        case 13: return 'Time';
        case 14: return 'Timestamp';
        case 15: return 'AutoIncrement';
        case 18: return 'Money';
        case 20: return "CICharacter($len)";
        default: return $raw ?: '?';
    }
}

// ── base type key (for editor dropdown) ────────────────────────────────────
function fieldBaseType(string $raw): string {
    switch ($raw) {
        case 'C': return 'Character';
        case 'N': return 'Numeric';
        case 'F': return 'Float';
        case 'L': return 'Logical';
        case 'D': return 'Date';
        case 'M': return 'Memo';
        case 'I': return 'Integer';
        case 'Y': return 'Money';
        case 'B': return 'Double';
        case 'V': return 'Varchar';
        case 'Q': return 'Varbinary';
        case 'T': return 'DateTime';
        case '@': return 'Timestamp';
        case '+': return 'AutoIncrement';
        case 'W': return 'Blob';
        case 'G': return 'Binary';
    }
    switch (ord(substr($raw, 0, 1))) {
        case  1: return 'Logical';
        case  3: return 'Date';
        case  4: return 'Character';
        case  5: return 'Memo';
        case  6: return 'Binary';
        case 10: return 'Double';
        case 11: return 'Integer';
        case 12: return 'ShortInt';
        case 13: return 'Time';
        case 14: return 'Timestamp';
        case 15: return 'AutoIncrement';
        case 18: return 'Money';
        case 20: return 'CICharacter';
        default: return $raw ?: '?';
    }
}

// ── decode trigger EVENT_MASK → timing + event strings ─────────────────────
function decodeTriggerMask(int $mask): array {
    // ADS_BEFORE_INSERT=0x01, ADS_AFTER_INSERT=0x02
    // ADS_BEFORE_UPDATE=0x04, ADS_AFTER_UPDATE=0x08
    // ADS_BEFORE_DELETE=0x10, ADS_AFTER_DELETE=0x20
    $isBefore = ($mask & 0x15) !== 0;  // bits 0,2,4
    $isAfter  = ($mask & 0x2A) !== 0;  // bits 1,3,5
    if ($isBefore && !$isAfter)     $timing = 'BEFORE';
    elseif ($isAfter && !$isBefore) $timing = 'AFTER';
    elseif ($isBefore && $isAfter)  $timing = 'BEFORE/AFTER';
    else                            $timing = '';

    $events = [];
    if ($mask & 0x03) $events[] = 'INSERT';
    if ($mask & 0x0C) $events[] = 'UPDATE';
    if ($mask & 0x30) $events[] = 'DELETE';
    return ['timing' => $timing, 'event' => implode(', ', $events)];
}

try {
    $conn = AdsConnection::connect($opts);
    $rows = [];

    // ── FIELDS ─────────────────────────────────────────────────────────────
    if ($kind === 'fields') {
        $fields = [];
        // Try system.columns first (works for DD connections)
        try {
            $stmt = $conn->query("SELECT TABLE_NAME, COL_NAME, COL_NUM, COL_TYPE, COL_LEN, COL_DEC FROM system.columns");
            while ($row = $stmt->fetchAssoc()) {
                if (strcasecmp($row['TABLE_NAME'], $table) !== 0) continue;
                $len = (int)$row['COL_LEN'];
                $dec = (int)$row['COL_DEC'];
                $raw = trim($row['COL_TYPE']);
                $fields[] = [
                    'Order'    => (int)$row['COL_NUM'],
                    'Field'    => $row['COL_NAME'],
                    'Type'     => fieldTypeLabel($raw, $len, $dec),
                    'BaseType' => fieldBaseType($raw),
                    'Size'     => $len,
                    'Decimals' => $dec,
                    'Required' => '',
                    'Default'  => '',
                    'Index'    => 'No',
                ];
            }
            usort($fields, fn($a, $b) => $a['Order'] <=> $b['Order']);
        } catch (Throwable $e) {}

        // Fallback for free tables: read field info from the .dbf/.adt binary header
        if (empty($fields)) {
            $dir = rtrim($c['path'], '/\\');
            foreach (['.dbf', '.DBF', '.adt', '.ADT'] as $ext) {
                $candidate = $dir . DIRECTORY_SEPARATOR . $table . $ext;
                if (!file_exists($candidate)) {
                    // Try uppercase table name
                    $candidate = $dir . DIRECTORY_SEPARATOR . strtoupper($table) . $ext;
                }
                if (file_exists($candidate)) {
                    $fields = readDbfFields($candidate);
                    break;
                }
            }
        }

        // Field properties: Required (305) and Default (306) via AdsDictionary
        try {
            $dict = AdsDictionary::fromConnection($conn);
            foreach ($fields as &$f) {
                try {
                    // ADS_DD_FIELD_REQUIRED (305) stores "Field_Can_Be_Null" value.
                    // "False" = cannot be null = field IS required.
                    $req = trim($dict->getFieldProperty($table, $f['Field'], 305));
                    $f['Required'] = (strcasecmp($req, 'False') === 0) ? 'True' : 'False';
                } catch (Throwable $e) {}
                try {
                    $f['Default'] = trim($dict->getFieldProperty($table, $f['Field'], 306));
                } catch (Throwable $e) {}
            }
            unset($f);
        } catch (Throwable $e) {}

        // Index membership: open table, scan tag expressions for field names
        $fieldIndex = [];
        $primaryKeyTag = '';
        try {
            $dict2 = AdsDictionary::fromConnection($conn);
            // ADS_DD_TABLE_PRIMARY_KEY = 209 (SAP-compatible code)
            $primaryKeyTag = strtoupper(trim($dict2->getTableProperty($table, 209)));
        } catch (Throwable $e) {}

        try {
            $tbl  = AdsTable::open($conn, $table, 0);
            $tags = $tbl->getIndexTags();
            $tbl->close();
            foreach ($tags as $t) {
                $tagUpper  = strtoupper($t['tag']);
                $isPrimary = ($primaryKeyTag !== '' && $tagUpper === $primaryKeyTag);
                $parts = preg_split('/[;+,\s]+/', strtoupper($t['expression']), -1, PREG_SPLIT_NO_EMPTY);
                foreach ($parts as $p) {
                    $p = trim($p, " \t'\"");
                    if ($p === '') continue;
                    if ($isPrimary) {
                        $fieldIndex[$p] = 'Primary';
                    } elseif (!isset($fieldIndex[$p]) || $fieldIndex[$p] === 'No') {
                        $fieldIndex[$p] = 'Yes';
                    }
                }
            }
        } catch (Throwable $e) {}

        foreach ($fields as &$f) {
            $key = strtoupper($f['Field']);
            if (isset($fieldIndex[$key])) $f['Index'] = $fieldIndex[$key];
        }
        unset($f);

        $rows = $fields;
    }

    // ── INDEXES ─────────────────────────────────────────────────────────────
    elseif ($kind === 'indexes') {
        $tbl  = AdsTable::open($conn, $table, 0);
        $tags = $tbl->getIndexTags();
        $tbl->close();
        foreach ($tags as $t) {
            $rows[] = [
                'Tag'        => $t['tag'],
                'Expression' => $t['expression'],
                'Descending' => $t['descending'] ? 'Yes' : 'No',
                'Unique'     => '',
                'Binary'     => '',
                'KeyType'    => '',
            ];
        }
    }

    // ── TRIGGERS — read directly from .add/.am binary for full body + correct timing ──
    else {
        $addPath = $c['path'];
        $amPath  = preg_replace('/\.[^.\/\\\\]+$/', '.am', $addPath);
        $binData = @file_get_contents($addPath);
        $am      = is_file($amPath) ? (@file_get_contents($amPath) ?: '') : '';

        if ($binData !== false) {
            $hdrLen2 = le32b($binData, 0x20);
            $recLen2 = le32b($binData, 0x24);
            $total2  = $recLen2 > 0 ? intdiv(strlen($binData) - $hdrLen2, $recLen2) : 0;

            // Build id→alias map
            $idToName2 = [];
            for ($j = 0; $j < $total2; $j++) {
                $base2 = $hdrLen2 + $j * $recLen2;
                if (ord($binData[$base2]) !== 0x04) continue;
                $oid2 = le32b($binData, $base2 + 5);
                $nm2  = rtrim(substr($binData, $base2 + 23, 200), " \0");
                if ($nm2 !== '') $idToName2[$oid2] = $nm2;
            }

            for ($j = 0; $j < $total2; $j++) {
                $base2 = $hdrLen2 + $j * $recLen2;
                if (ord($binData[$base2]) !== 0x04) continue;
                $ot2 = rtrim(substr($binData, $base2 + 13, 10), " \0");
                if ($ot2 !== 'Trigger') continue;
                $trigName2  = rtrim(substr($binData, $base2 + 23, 200), " \0");
                $parentId2  = le32b($binData, $base2 + 9);
                $tableAlias = $idToName2[$parentId2] ?? '';
                if (strcasecmp($tableAlias, $table) !== 0) continue;

                $PA2   = $base2 + 225;
                $event2  = le32b($binData, $PA2 + 0);
                $timing2 = le16b($binData, $PA2 + 6);

                // Inline body: bytes 18+ of the property area
                $inlineStart = $PA2 + 18;
                $inlineEnd   = $PA2 + 273;
                if ($inlineEnd > strlen($binData)) $inlineEnd = strlen($binData);
                $inline2 = substr($binData, $inlineStart, $inlineEnd - $inlineStart);
                $nul2 = strpos($inline2, "\0");
                if ($nul2 !== false) $inline2 = substr($inline2, 0, $nul2);

                // .am continuation
                $amBlock2 = le32b($binData, $base2 + 498);
                $amLen2   = le32b($binData, $base2 + 502);
                $amPart   = '';
                if ($amBlock2 > 0 && $amLen2 > 0 && $am !== '') {
                    $amOff2 = $amBlock2 * 8;
                    $amPart = substr($am, $amOff2, $amLen2);
                    $l2 = strlen($amPart);
                    while ($l2 > 0) {
                        $b2 = ord($amPart[$l2 - 1]);
                        if (($b2 >= 0x20 && $b2 <= 0x7E) || $b2 === 0x09 || $b2 === 0x0A || $b2 === 0x0D) break;
                        $l2--;
                    }
                    $amPart = substr($amPart, 0, $l2);
                }

                $fullBody = rtrim($inline2 . $amPart);
                $timingStr2 = match($timing2) { 1=>'BEFORE', 2=>'INSTEAD OF', 4=>'AFTER', default=>'' };
                $eventStr2  = match($event2)  { 1=>'INSERT', 2=>'UPDATE', 3=>'DELETE', default=>'' };

                $rows[] = [
                    'Name'     => $trigName2,
                    'Timing'   => $timingStr2,
                    'Event'    => $eventStr2,
                    'Enabled'  => 'Yes',
                    'Priority' => 1,
                    'Body'     => $fullBody,
                ];
            }
        }
        usort($rows, fn($a, $b) => strcmp($a['Name'], $b['Name']));
    }

    $conn->close();
    echo json_encode(['data' => $rows]);
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
}
