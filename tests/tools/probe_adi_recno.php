<?php
/**
 * ADI recno encoding probe — SAP ACE ground truth vs raw ADI bytes.
 *
 * Run with:  C:\php\php.exe -c C:\php\php_sapads.ini probe_adi_recno.php
 *
 * For each probed table this script:
 *   1) Uses SAP ACE to walk all records in natural (default index) order,
 *      capturing (position, recno, key_field_value).
 *   2) Binary-dumps every dense leaf of the ADI file's first tag, showing
 *      the raw entry bytes alongside the SAP-known recno for cross-reference.
 *
 * The goal: understand what byte(s) in a dense-leaf entry encode the recno.
 */

$DATA_DIR = 'F:\\OpenADS\\testdata\\pmsys';
$ADD_PATH = $DATA_DIR . '\\pmsys.add';

// Tables to probe.  show_field=null means use the tag0 key field for display.
$TABLES = [
    ['catcodes',              null],  // 64 rows  — confirms 1-byte recno range
    ['landlords',             null],  // medium   — tag0 is 'inactive' (1-byte key)
    ['leases',                null],  // medium   — tag0 is EndDate (DATE, 4-byte key)
    ['auditlog',              null],  // large    — recnos likely > 255
    ['propertytransactions',  null],  // large    — recnos likely > 255
];

const PAGE = 512;

// ── binary helpers ────────────────────────────────────────────────────────────
function u16le(string $d, int $o): int {
    return ord($d[$o]) | (ord($d[$o+1]) << 8);
}
function u32le(string $d, int $o): int {
    return ord($d[$o]) | (ord($d[$o+1]) << 8) | (ord($d[$o+2]) << 16) | (ord($d[$o+3]) << 24);
}
function u32be(string $d, int $o): int {
    return (ord($d[$o]) << 24) | (ord($d[$o+1]) << 16) | (ord($d[$o+2]) << 8) | ord($d[$o+3]);
}

function read_page(string $path, int $pgno): string {
    $fp = fopen($path, 'rb');
    fseek($fp, $pgno * PAGE);
    $d = fread($fp, PAGE);
    fclose($fp);
    return $d ?: str_repeat("\0", PAGE);
}

// ── ADT field descriptor parsing ─────────────────────────────────────────────
function parse_adt_fields(string $adt_path): array {
    $fp = fopen($adt_path, 'rb');
    $hdr = fread($fp, 400);
    $hdr_len = u32le($hdr, 32);
    $rec_len  = u32le($hdr, 36);
    $n = ($hdr_len - 400) / 200;
    $raw = fread($fp, $n * 200);
    fclose($fp);
    $fields = [];
    for ($i = 0; $i < $n; $i++) {
        $b = $i * 200;
        $nm = '';
        for ($c = 0; $c < 128 && ord($raw[$b+$c]); $c++) $nm .= $raw[$b+$c];
        $fields[] = [
            'name'   => $nm,
            'type'   => u16le($raw, $b+129),
            'offset' => u16le($raw, $b+131),
            'length' => u16le($raw, $b+135),
        ];
    }
    return ['fields' => $fields, 'hdr_len' => $hdr_len, 'rec_len' => $rec_len];
}

// ── ADI tag-directory scan ────────────────────────────────────────────────────
function scan_adi_tags(string $adi_path): array {
    $fp = fopen($adi_path, 'rb');
    fseek($fp, 2 * PAGE);
    $pg = fread($fp, PAGE);
    $count = u16le($pg, 2);
    $tags = [];
    for ($i = 0; $i < $count; $i++) {
        $off = 24 + $i * 6;
        $xx = ord($pg[$off]);
        $fmk_pg  = $xx + 1;
        $root_pg = $fmk_pg + 1;
        fseek($fp, $fmk_pg * PAGE);
        $fmk = fread($fp, PAGE);
        // Parse F-marker: "F<num>[;F<num>...]"
        $fnums = [];
        $j = ($fmk[0] === 'F') ? 1 : 0;
        if ($j) {
            while ($j < strlen($fmk) && $fmk[$j] >= '1' && $fmk[$j] <= '9') {
                $n = 0;
                while ($j < strlen($fmk) && $fmk[$j] >= '0' && $fmk[$j] <= '9')
                    $n = $n * 10 + (ord($fmk[$j++]) - 48);
                $fnums[] = $n;
                if ($j+1 < strlen($fmk) && $fmk[$j] === ';' && $fmk[$j+1] === 'F') $j += 2;
                else break;
            }
        }
        fseek($fp, $xx * PAGE);
        $hdr = fread($fp, PAGE);
        $uniq = (strlen($hdr) > 14) ? (ord($hdr[14]) & 1) : 0;
        $tags[] = ['xx'=>$xx,'fmk_pg'=>$fmk_pg,'root_pg'=>$root_pg,'fnums'=>$fnums,'unique'=>$uniq];
    }
    fclose($fp);
    return $tags;
}

// ── dump dense leaves with SAP-recno cross-reference ─────────────────────────
function dump_dense_leaves_with_truth(
    string $adi_path,
    int $root_pg,
    int $entry_sz,   // 2 for 1-byte fields, 3 for wider
    bool $char_key,
    int $key_padded, // (key_len+3)&~3 for char, 0 for numeric
    array $sap_truth // [sap_pos => recno] for ALL positions (indexed by row position 0-based)
): void {
    $BRANCH_SZ = $char_key ? ($key_padded + 5) : 16;

    // Walk from root to first dense leaf
    $cur = $root_pg;
    for (;;) {
        $pg = read_page($adi_path, $cur);
        $lv = u16le($pg, 0);
        $ct = u16le($pg, 2);
        if ($ct === 0) { echo "  [empty page $cur]\n"; return; }
        if ($lv === 2 || $lv === 3) break;
        // Branch or sparse: follow first child
        if ($char_key) {
            $next = ord($pg[12 + $key_padded + 4]);
        } else {
            $next = u32be($pg, 12 + 12);  // key[8]+cum[4]+page[4], page starts at +12
        }
        // Also print branch/sparse page info
        echo "  [page $cur lv=$lv cnt=$ct — branch, following first child → $next]\n";
        // Print branch entries
        for ($i = 0; $i < min($ct, 5); $i++) {
            $off = 12 + $i * $BRANCH_SZ;
            $bytes = [];
            for ($b = 0; $b < $BRANCH_SZ; $b++) $bytes[] = ord($pg[$off+$b]);
            if ($char_key) {
                $cum  = u32le($pg, $off + $key_padded);
                $page = ord($pg[$off + $key_padded + 4]);
            } else {
                $cum  = u32be($pg, $off + 8);
                $page = u32be($pg, $off + 12);
            }
            $hex = implode(' ', array_map(fn($x)=>sprintf('%02X',$x), $bytes));
            echo "    entry[$i]: cum=$cum page=$page  raw=[$hex]\n";
        }
        $cur = $next;
    }

    // Walk dense leaves via right-sibling links
    $leaf_no = 0;
    $global_pos = 0;  // running position across ALL leaves
    while (true) {
        $pg = read_page($adi_path, $cur);
        $lv = u16le($pg, 0);
        $ct = u16le($pg, 2);
        $ls = u32le($pg, 4);
        $rs = u32le($pg, 8);
        // Sub-header: 12 bytes at offset 12
        $sh0 = u32le($pg, 12);
        $sh1 = u32le($pg, 16);
        $sh2 = u32le($pg, 20);

        printf("\n  ── Dense leaf %d (page=%d lv=%d cnt=%d lsib=%s rsib=%s) ──\n",
               $leaf_no, $cur, $lv, $ct,
               $ls === 0xFFFFFFFF ? 'none' : $ls,
               $rs === 0xFFFFFFFF ? 'none' : $rs);
        printf("     Sub-header: %08X %08X %08X\n", $sh0, $sh1, $sh2);

        printf("     %-4s | %-7s | %-8s | %s\n", 'idx', 'raw', 'sap_rno', 'match?');
        printf("     -----|---------|---------|-------\n");

        for ($i = 0; $i < $ct; $i++) {
            $off = 24 + $i * $entry_sz;
            $raw = [];
            for ($b = 0; $b < $entry_sz; $b++) $raw[] = ord($pg[$off+$b]);
            $hex = implode(' ', array_map(fn($x)=>sprintf('%02X',$x), $raw));
            $sap_rno = $sap_truth[$global_pos] ?? '?';
            // Check if raw[0] directly matches SAP recno
            $direct_match = ($raw[0] === $sap_rno) ? 'YES' : "NO(b0={$raw[0]})";
            printf("     %4d | %-7s | %7s | %s\n", $i, $hex, $sap_rno, $direct_match);
            $global_pos++;
        }
        $leaf_no++;
        if ($rs === 0xFFFFFFFF || $rs === 0) break;
        $cur = $rs;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
echo "=== ADI Recno Encoding Probe (SAP ACE ground truth)\n";
echo "Data: $DATA_DIR\n\n";

// ── Pass 1: gather ADT/ADI metadata and SAP truth while connection is open ───
$conn = AdsConnection::connect(['path' => $ADD_PATH, 'user' => 'adssys', 'password' => 'pmsys']);
$table_data = [];

foreach ($TABLES as [$tname, $show_field]) {
    $adt = "$DATA_DIR\\$tname.adt";
    $adi = "$DATA_DIR\\$tname.adi";
    if (!file_exists($adi)) { echo "Skipping $tname (no .adi)\n"; continue; }

    $adt_info = parse_adt_fields($adt);
    $tags = scan_adi_tags($adi);

    if (empty($tags)) { echo "Skipping $tname (no tags)\n"; continue; }
    $tag = $tags[0];
    $fi  = $tag['fnums'][0] - 1;
    $fld = $adt_info['fields'][$fi] ?? null;
    if (!$fld) { echo "Skipping $tname (field not found)\n"; continue; }

    $char_key   = in_array($fld['type'], [4, 20]);
    $key_len    = $fld['length'];
    $key_padded = $char_key ? (($key_len + 3) & ~3) : 0;
    $entry_sz   = ($key_len === 1) ? 2 : 3;
    $key_field  = $fld['name'];

    $val_field = $show_field ?? $key_field;  // use key if no explicit show field
    $truth = [];
    $vals  = [];
    try {
        $sql  = "SELECT RECNO() AS rno, $val_field AS val FROM $tname ORDER BY $key_field";
        $stmt = $conn->prepare($sql);
        $rs   = $stmt->execute();
        $all  = $rs->fetchAll();
        $stmt->close();
        foreach ($all as $pos => $row) {
            $truth[$pos] = (int)$row['rno'];
            $vals[$pos]  = $row['val'];
        }
        echo "Gathered truth: $tname  " . count($truth) . " rows  (ORDER BY $key_field)\n";
    } catch (Exception $e) {
        echo "SQL error on $tname: " . $e->getMessage() . "\n";
        continue;
    }

    $table_data[] = compact('tname','val_field','adt','adi','adt_info','tags',
                            'tag','fld','char_key','key_len','key_padded',
                            'entry_sz','key_field','truth','vals');
}

$conn->close();  // release ACE lock on .adi files

// ── Pass 2: binary analysis (ACE no longer holds the files) ─────────────────
foreach ($table_data as $td) {
    extract($td);

    echo "\n" . str_repeat('=', 72) . "\n";
    echo "TABLE: $tname  val_field=$val_field\n";
    echo str_repeat('=', 72) . "\n";

    echo "ADT: hdr_len={$adt_info['hdr_len']} rec_len={$adt_info['rec_len']}\n";
    foreach ($adt_info['fields'] as $idx => $f)
        printf("  [%2d] %-20s type=%2d off=%3d len=%3d\n",
               $idx+1, $f['name'], $f['type'], $f['offset'], $f['length']);

    echo "\nADI tags: " . count($tags) . "\n";
    foreach ($tags as $ti => $t)
        printf("  tag%d: fnums=[%s] hdr=%d fmk=%d root=%d unique=%d\n",
               $ti, implode(',', $t['fnums']),
               $t['xx'], $t['fmk_pg'], $t['root_pg'], $t['unique']);

    $total = count($truth);
    echo "\nSAP truth ($total rows, ORDER BY $key_field):\n";
    foreach ($truth as $pos => $rno) {
        if ($pos < 30 || $pos >= $total - 5)
            printf("  pos=%4d recno=%5d  val=%s\n", $pos, $rno, $vals[$pos] ?? '');
        elseif ($pos === 30)
            echo "  ...\n";
    }

    printf("\nFirst tag: field=%s type=%d len=%d char_key=%s entry_sz=%d\n",
           $fld['name'], $fld['type'], $key_len,
           $char_key ? 'yes' : 'no', $entry_sz);
    echo "\nDense leaf dump vs SAP recnos:\n";
    dump_dense_leaves_with_truth($adi, $tag['root_pg'], $entry_sz, $char_key, $key_padded, $truth);
    echo "\n";
}

echo "\n=== DONE ===\n";
