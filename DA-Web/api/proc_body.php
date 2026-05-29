<?php
/**
 * api/proc_body.php — return full stored-procedure or function SQL body.
 *
 * POST { dd, type, name }
 *   type = "proc" | "function"
 *
 * Returns { body, input_params, output_params }
 *
 * Reads the SAP ADS binary .add file directly so the full body is returned
 * regardless of the CHAR(255) limit on the system.storedprocedures virtual table.
 *
 * Binary .add record layout (rec_len = 524):
 *   [0]       status 0x04=active
 *   [5..8]    obj_id  (uint32 LE)
 *   [9..12]   parent_id (uint32 LE)
 *   [13..22]  Object Type (CHAR 10)
 *   [23..222] Object Name (CHAR 200)
 *   [223..224] plen (uint16 LE; 0xFFFF = null)
 *   [225..497] Property (273 bytes): [input_params\0][0xFF×6][8 hdr bytes][CRLF][SQL body]
 *   [498..506] More Property (9 bytes): [uint32 LE am_block][uint32 LE am_len][0x00]
 *   [507..514] Info1 / Info2
 *   [515..523] Comment (Memo ref)
 *
 * Companion .am memo file: block_size = 8 bytes; data at am_block * 8.
 */
header('Content-Type: application/json');
session_start();

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName = trim($body['dd']   ?? '');
$type   = trim($body['type'] ?? '');   // 'proc' or 'function'
$name   = trim($body['name'] ?? '');

if ($ddName === '' || $name === '' || !in_array($type, ['proc', 'function'], true)) {
    http_response_code(400);
    echo json_encode(['error' => 'dd, type (proc|function), and name are required']);
    exit;
}

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}

$addPath = $_SESSION['connections'][$ddName]['path'];
$amPath  = preg_replace('/\.[^.\/\\\\]+$/', '.am', $addPath);

$data = @file_get_contents($addPath);
if ($data === false) {
    http_response_code(500);
    echo json_encode(['error' => "Cannot read dictionary file"]);
    exit;
}

// Read file-level header
function le32(string $data, int $off): int {
    return unpack('V', substr($data, $off, 4))[1];
}
function le16(string $data, int $off): int {
    return unpack('v', substr($data, $off, 2))[1];
}

$hdrLen = le32($data, 0x20);
$recLen = le32($data, 0x24);
if ($recLen === 0) {
    http_response_code(500);
    echo json_encode(['error' => 'Corrupt dictionary header']);
    exit;
}

$total = (int)((strlen($data) - $hdrLen) / $recLen);

// Target obj_type based on request type
$targetType = ($type === 'function') ? 'Function' : null;  // StoredProc OR Procedure

$result = null;
for ($i = 0; $i < $total; $i++) {
    $base   = $hdrLen + $i * $recLen;
    $status = ord($data[$base]);
    if ($status !== 0x04) continue;  // skip deleted / inactive

    $objType = rtrim(substr($data, $base + 13, 10), " \0");
    if ($type === 'function') {
        if ($objType !== 'Function') continue;
    } else {
        if ($objType !== 'StoredProc' && $objType !== 'Procedure') continue;
    }

    $objName = rtrim(substr($data, $base + 23, 200), " \0");
    if (strcasecmp($objName, $name) !== 0) continue;

    // --- Found the record ---
    $plen = le16($data, $base + 223);
    $propNull = ($plen === 0xFFFF);

    $PS = $base + 225;  // property data start
    $PL = 273;          // property area length

    $inputParams = '';
    $returnType  = '';   // always initialised; only set for functions
    $sqlBody     = '';

    if ($type === 'function') {
        // Function layout: [plen binary preamble][0xFF×N][le16+rettype\0][le16+inparams\0][le16+body]
        // FIX: when slen > remaining space the body is SPLIT — read as much as fits inline,
        // the rest is in the .am continuation (same as stored procs).
        $pos = $propNull ? 0 : $plen;
        while ($pos < $PL && ord($data[$PS + $pos]) === 0xFF) $pos++;

        $readLstr = function() use (&$pos, $PL, $PS, $data): string {
            if ($pos + 2 > $PL) return '';
            $slen = le16($data, $PS + $pos);
            $pos += 2;
            if ($slen === 0 || $slen === 0xFFFF) return '';
            // Read as many bytes as fit in the remaining property area (partial if body > space)
            $readable = min($slen, $PL - $pos);
            if ($readable <= 0) return '';
            $s    = substr($data, $PS + $pos, $readable);
            $pos += $readable;
            $nul  = strpos($s, "\0");
            return ($nul !== false) ? substr($s, 0, $nul) : $s;
        };

        $returnType  = $readLstr(); // return type (e.g. "INTEGER", "CHAR ( 13 )")
        $inputParams = $readLstr(); // "name TYPE, name TYPE"
        $sqlBody     = $readLstr(); // inline body (may be partial — .am has the rest)
        $sqlBody     = trim($sqlBody);

    } else {
        // StoredProc layout: [input_params\0][0xFF×6][8 binary bytes][CRLF][SQL body]

        // input_params: first plen bytes, NUL-terminated
        if (!$propNull && $plen > 0) {
            $raw = substr($data, $PS, $plen);
            $nul = strpos($raw, "\0");
            $inputParams = ($nul !== false) ? substr($raw, 0, $nul) : $raw;
        }

        // Inline SQL body: skip params → 0xFF markers → binary header → CRLF → body
        $pos = $propNull ? 0 : $plen;
        while ($pos < $PL && ord($data[$PS + $pos]) === 0xFF) $pos++;
        while ($pos + 1 < $PL) {
            if (ord($data[$PS + $pos]) === 0x0D && ord($data[$PS + $pos + 1]) === 0x0A) break;
            $pos++;
        }
        if ($pos + 1 < $PL) {
            $end = $PL;
            for ($j = $pos; $j < $PL; $j++) {
                if ($data[$PS + $j] === "\0") { $end = $j; break; }
            }
            $sqlBody = substr($data, $PS + $pos, $end - $pos);
            $sqlBody = ltrim($sqlBody, " \t\r\n");
            $sqlBody = rtrim($sqlBody, " \t\r\n");
        }
    }

    // Append .am continuation — more_property bytes [498..506]:
    //   [uint32 LE am_block][uint32 LE am_len][0x00]
    //   byte_offset in .am = am_block * 8
    $mp      = substr($data, $base + 498, 9);
    $amBlock = unpack('V', substr($mp, 0, 4))[1];
    $amLen   = unpack('V', substr($mp, 4, 4))[1];

    if ($amBlock > 0 && $amLen > 0 && is_file($amPath)) {
        $amOff  = $amBlock * 8;
        $amData = @file_get_contents($amPath, false, null, $amOff, $amLen);
        if ($amData !== false && strlen($amData) > 0) {
            // The .am block is padded to mp_len bytes; actual SQL content is shorter.
            // Scan backward and truncate at the last byte that is valid SQL:
            // printable ASCII (0x20-0x7E) or whitespace (tab/LF/CR).
            $len = strlen($amData);
            while ($len > 0) {
                $b = ord($amData[$len - 1]);
                if (($b >= 0x20 && $b <= 0x7E) || $b === 0x09 || $b === 0x0A || $b === 0x0D) break;
                $len--;
            }
            $amData = rtrim(substr($amData, 0, $len), " \t\r\n");
            $sqlBody .= $amData;
        }
    }

    $result = [
        'body'         => $sqlBody,
        'input_params' => $inputParams,
        'return_type'  => $returnType,
    ];
    break;
}

if ($result === null) {
    http_response_code(404);
    echo json_encode(['error' => "Object '$name' not found in '$ddName'"]);
    exit;
}

// json_encode returns false when the string contains invalid UTF-8 sequences.
// SQL bodies are ASCII; any surviving high bytes are .am padding garbage.
$json = json_encode($result, JSON_UNESCAPED_UNICODE | JSON_INVALID_UTF8_SUBSTITUTE);
if ($json === false) {
    // Fallback: strip all non-ASCII bytes and retry
    $result['body'] = preg_replace('/[\x80-\xFF]/', '', $result['body']);
    $json = json_encode($result, JSON_UNESCAPED_UNICODE);
}
echo $json;
