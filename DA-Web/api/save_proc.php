<?php
/**
 * api/save_proc.php — save stored-procedure or function back to the DD.
 *
 * POST {
 *   dd          : string   – dictionary name (must be connected)
 *   type        : string   – "proc" | "function"
 *   name        : string   – object name (case-insensitive)
 *   body        : string   – full SQL body
 *   input_params: string   – serialised "Name1,TYPE1;Name2,TYPE2;"
 *   return_type : string   – function return type (functions only)
 * }
 *
 * Strategy:
 *   The full SQL body is always written to the companion .am memo file.
 *   The inline property area is cleared (spaces after CRLF) so that the
 *   engine reads: "" (inline) + full body (.am) = full body.  This avoids
 *   the 273-byte inline limit and keeps both sources consistent.
 *
 *   .am block encoding: byte_offset = block_num * 8.
 *   If the existing block can hold the new body we overwrite in place;
 *   otherwise we append at an 8-byte-aligned offset at end of file.
 */
header('Content-Type: application/json');
session_start();

$req         = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName      = trim($req['dd']           ?? '');
$type        = trim($req['type']         ?? '');
$name        = trim($req['name']         ?? '');
$newBody     = $req['body']              ?? '';
$newInParams = trim($req['input_params'] ?? '');
$newRetType  = trim($req['return_type']  ?? '');

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

// ── Binary helpers ────────────────────────────────────────────────────────────
function sp_le32(string $d, int $o): int { return unpack('V', substr($d, $o, 4))[1]; }
function sp_le16(string $d, int $o): int { return unpack('v', substr($d, $o, 2))[1]; }

// ── Read .add file ─────────────────────────────────────────────────────────────
$data = @file_get_contents($addPath);
if ($data === false) {
    http_response_code(500);
    echo json_encode(['error' => 'Cannot read dictionary file']);
    exit;
}

$hdrLen = sp_le32($data, 0x20);
$recLen = sp_le32($data, 0x24);
if ($recLen === 0) {
    http_response_code(500);
    echo json_encode(['error' => 'Corrupt dictionary header']);
    exit;
}
$total = (int)((strlen($data) - $hdrLen) / $recLen);
$PL    = 273;   // property area size in bytes

$found = false;

for ($i = 0; $i < $total; $i++) {
    $base   = $hdrLen + $i * $recLen;
    if (ord($data[$base]) !== 0x04) continue;

    $objType = rtrim(substr($data, $base + 13, 10), " \0");
    if ($type === 'function') {
        if ($objType !== 'Function') continue;
    } else {
        if ($objType !== 'StoredProc' && $objType !== 'Procedure') continue;
    }

    $objName = rtrim(substr($data, $base + 23, 200), " \0");
    if (strcasecmp($objName, $name) !== 0) continue;

    // ── Found the record ──────────────────────────────────────────────────────
    $found = true;
    $PS = $base + 225;

    $plenRaw  = sp_le16($data, $base + 223);
    $propNull = ($plenRaw === 0xFFFF);
    $plenOrig = $propNull ? 0 : $plenRaw;

    // Read the existing 273-byte property area
    $propArea = substr($data, $PS, $PL);

    // ── Locate the "binary header" bytes (between params/preamble and CRLF) ──
    // For StoredProc: [input_params\0][0xFF×N][binary bytes][CRLF][body]
    // For Function:   [preamble bytes][0xFF×N][lstr section]
    $posAfterPreamble = $plenOrig;
    while ($posAfterPreamble < $PL && ord($propArea[$posAfterPreamble]) === 0xFF)
        $posAfterPreamble++;

    if ($type !== 'function') {
        // ── StoredProc ────────────────────────────────────────────────────────
        // Bytes between end-of-0xFF and CRLF are the "binary header" — preserve them.
        $binHdrStart = $posAfterPreamble;
        $crlfPos     = $binHdrStart;
        while ($crlfPos < $PL - 1) {
            if (ord($propArea[$crlfPos]) === 0x0D && ord($propArea[$crlfPos + 1]) === 0x0A)
                break;
            $crlfPos++;
        }
        $binHeader = substr($propArea, $binHdrStart, $crlfPos - $binHdrStart);

        // Construct new input_params bytes (name,TYPE;name,TYPE;…)
        // Normalise: ensure trailing semicolon then add NUL
        $ipStr = rtrim($newInParams, ';');
        $ipStr = ($ipStr !== '') ? ($ipStr . ';' . "\0") : '';
        $newPlen = strlen($ipStr);   // 0 if no params

        // Build new 273-byte property area:
        //   [input_params\0] [0xFF×6] [binary header] [CRLF] [spaces]
        $ffMarkers  = str_repeat("\xFF", 6);
        $crlf       = "\x0D\x0A";
        $prefix     = $ipStr . $ffMarkers . $binHeader . $crlf;
        if (strlen($prefix) > $PL) {
            http_response_code(400);
            echo json_encode(['error' => 'Input parameters too long for inline area']);
            exit;
        }
        // Pad the rest with spaces — this clears the old inline body
        $newPropArea = str_pad($prefix, $PL, ' ');
        $newPlen = ($newPlen === 0) ? 0 : $newPlen;  // plen = param bytes (incl. \0)

        // Update plen field: 0xFFFF if no params, else actual length
        $newPlenField = ($newPlen === 0) ? 0xFFFF : $newPlen;

    } else {
        // ── Function ──────────────────────────────────────────────────────────
        // Preserve everything before the lstr section (preamble + 0xFF markers).
        $lstrStart = $posAfterPreamble;
        $preamble  = substr($propArea, 0, $lstrStart);

        // Serialise new lstr section: [le16+rettype\0] [le16+inparams\0] [le16+0]
        // Function params format: "name TYPE, name TYPE" — NO trailing semicolon.
        $rtStr = $newRetType . "\0";
        $ipStr = ($newInParams !== '') ? ($newInParams . "\0") : "\0";
        $newLstrs  = pack('v', strlen($rtStr)) . $rtStr
                   . pack('v', strlen($ipStr)) . $ipStr
                   . pack('v', 0);   // body length = 0  (full body in .am)

        $newPropArea = substr(
            $preamble . str_pad($newLstrs, $PL - strlen($preamble), ' '),
            0, $PL
        );
        $newPlenField = $plenRaw;  // preserve preamble length unchanged
    }

    // ── Write full body to .am file ───────────────────────────────────────────
    $newAmBlock = 0;
    $newAmLen   = strlen($newBody);

    if ($newAmLen > 0) {
        // Read existing more_property for current .am reference
        $mp       = substr($data, $base + 498, 9);
        $oldBlock = unpack('V', substr($mp, 0, 4))[1];
        $oldLen   = unpack('V', substr($mp, 4, 4))[1];

        $amContent = @file_get_contents($amPath);
        if ($amContent === false) $amContent = '';

        if ($oldBlock > 0 && $oldLen >= $newAmLen) {
            // Reuse existing block: overwrite in place, pad remainder with spaces
            $amOffset      = $oldBlock * 8;
            $newAmBlock    = $oldBlock;
            $padded        = str_pad($newBody, $oldLen, ' ');
            $amContent     = substr_replace($amContent, $padded, $amOffset, $oldLen);
        } else {
            // Append at 8-byte-aligned offset after current end
            $curLen   = strlen($amContent);
            $aligned  = (int)(ceil($curLen / 8.0) * 8);
            if ($aligned > $curLen)
                $amContent .= str_repeat("\0", $aligned - $curLen);
            $newAmBlock = $aligned / 8;
            $amContent .= $newBody;
        }

        if (@file_put_contents($amPath, $amContent) === false) {
            http_response_code(500);
            echo json_encode(['error' => 'Failed to write .am memo file']);
            exit;
        }
    }

    // ── Patch the binary .add record in memory ────────────────────────────────
    // Property area (273 bytes)
    $data = substr_replace($data, $newPropArea, $PS, $PL);
    // plen field (2 bytes at base+223)
    $data = substr_replace($data, pack('v', $newPlenField), $base + 223, 2);
    // more_property (9 bytes at base+498): [block_num LE32][data_len LE32][0x00]
    $newMp = pack('V', $newAmBlock) . pack('V', $newAmLen) . "\x00";
    $data  = substr_replace($data, $newMp, $base + 498, 9);

    break;
}

if (!$found) {
    http_response_code(404);
    echo json_encode(['error' => "Object '$name' not found in '$ddName'"]);
    exit;
}

// ── Write updated .add file ────────────────────────────────────────────────────
if (@file_put_contents($addPath, $data) === false) {
    http_response_code(500);
    echo json_encode(['error' => 'Failed to write dictionary file']);
    exit;
}

echo json_encode(['ok' => true]);
