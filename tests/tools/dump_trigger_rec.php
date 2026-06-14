<?php
/**
 * dump_trigger_rec.php
 * Reads pmsys.add (SAP binary .add) and dumps the raw bytes
 * of the "Insert AuditLog" trigger record's property area.
 *
 * The .add file is a fixed-record-length DBF-style file.
 * Header starts with a magic/version byte, then hdr_len (LE uint16 at byte 8),
 * rec_len (LE uint16 at byte 10).
 * Record layout (rec_len = 524 per data_dict.cpp comment):
 *   [0]      status (0x04 = active)
 *   [1..4]   ?
 *   [5..8]   obj_id  LE uint32
 *   [9..12]  parent_id LE uint32
 *   [13..22] obj_type CHAR(10)
 *   [23..222] obj_name CHAR(200)
 *   [223..224] plen LE uint16
 *   [225..497] property area (273 bytes)
 *   [498..506] more_property (9 bytes)
 *   ...
 */

$path = $argv[1] ?? 'F:/OpenADS/testdata/pmsys/pmsys.add';
$data = file_get_contents($path);
if ($data === false) { die("Cannot read $path\n"); }
echo "File: $path\n";

function le16(string $s, int $off): int {
    return ord($s[$off]) | (ord($s[$off+1]) << 8);
}
function le32(string $s, int $off): int {
    return ord($s[$off]) | (ord($s[$off+1]) << 8) | (ord($s[$off+2]) << 16) | (ord($s[$off+3]) << 24);
}
function trim_fixed(string $s, int $off, int $len): string {
    return rtrim(substr($s, $off, $len), "\x00 ");
}
function hex_dump(string $s, int $off, int $len, string $label): void {
    echo "$label (offset $off, len $len):\n";
    for ($i = 0; $i < $len; $i += 16) {
        $chunk = substr($s, $off + $i, min(16, $len - $i));
        $hex   = implode(' ', array_map(fn($c) => sprintf('%02x', ord($c)), str_split($chunk)));
        $asc   = preg_replace('/[^\x20-\x7e]/', '.', $chunk);
        printf("  %04x: %-47s  |%s|\n", $i, $hex, $asc);
    }
}

// Read header — hdr_len at 0x20 (LE uint32), rec_len at 0x24 (LE uint32)
$hdr_len = le32($data, 0x20);
$rec_len  = le32($data, 0x24);
echo "hdr_len=$hdr_len  rec_len=$rec_len  file_size=" . strlen($data) . "\n\n";

$total = (int)(( strlen($data) - $hdr_len ) / $rec_len);
$found = [];

for ($i = 0; $i < $total; $i++) {
    $base = $hdr_len + $i * $rec_len;
    if ($base + $rec_len > strlen($data)) break;

    $status = ord($data[$base]);
    if ($status !== 0x04) continue;  // skip deleted/inactive

    $obj_type = trim_fixed($data, $base + 13, 10);
    $obj_name = trim_fixed($data, $base + 23, 200);

    if ($obj_type !== 'Trigger') continue;

    $plen = le16($data, $base + 223);
    $prop_start = $base + 225;

    echo "=== Trigger: '$obj_name' (record $i, base=$base) ===\n";
    echo "  plen=0x" . sprintf('%04x', $plen) . " (" . ($plen === 0xFFFF ? 'NULL' : $plen) . ")\n";

    if ($plen !== 0xFFFF && $plen > 0) {
        echo "  declared property ($plen bytes): ";
        echo bin2hex(substr($data, $prop_start, min($plen, 32)));
        echo "\n";
    }

    // Full 273-byte property area
    hex_dump($data, $prop_start, 273, "  full property area");

    // More property (9 bytes)
    $mp_off = $base + 498;
    $am_block = le32($data, $mp_off);
    $am_len   = le32($data, $mp_off + 4);
    echo "  more_property: am_block=$am_block  am_len=$am_len\n";
    echo "\n";

    $found[] = $obj_name;
    if (count($found) >= 4) break;  // only first 4 triggers
}

if (empty($found)) echo "No active Trigger records found.\n";
