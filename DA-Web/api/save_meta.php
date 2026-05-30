<?php
/**
 * api/save_meta.php — save field property changes back to the DD.
 *
 * POST { dd, table, rows: [{ Field, Required, Default }] }
 *
 * Writes ADS_DD_FIELD_REQUIRED (305) and ADS_DD_FIELD_DEFAULT (306)
 * for each supplied row via AdsDictionary::setFieldProperty.
 */
header('Content-Type: application/json');
session_start();

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName = trim($body['dd']    ?? '');
$table  = trim($body['table'] ?? '');
$rows   = $body['rows'] ?? [];

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '' || !preg_match('/^[A-Za-z_][A-Za-z0-9_ ]*$/', $table) || !is_array($rows)) {
    http_response_code(400);
    echo json_encode(['error' => 'invalid parameters']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

try {
    $conn = AdsConnection::connect($opts);
    $dict = AdsDictionary::fromConnection($conn);
    $saved = 0;
    $errors = [];

    foreach ($rows as $row) {
        $field = trim($row['Field'] ?? '');
        if ($field === '') continue;

        // ADS_DD_FIELD_REQUIRED = 305 ('True' = required / cannot be null)
        if (isset($row['Required'])) {
            $val = (strcasecmp(trim($row['Required']), 'True') === 0) ? 'True' : 'False';
            try {
                $dict->setFieldProperty($table, $field, 305, $val);
                $saved++;
            } catch (Throwable $e) {
                $errors[] = "$field Required: " . $e->getMessage();
            }
        }

        // ADS_DD_FIELD_DEFAULT = 306
        if (array_key_exists('Default', $row)) {
            $val = trim($row['Default'] ?? '');
            try {
                $dict->setFieldProperty($table, $field, 306, $val);
                $saved++;
            } catch (Throwable $e) {
                $errors[] = "$field Default: " . $e->getMessage();
            }
        }
    }

    $conn->close();

    if (!empty($errors)) {
        echo json_encode(['saved' => $saved, 'errors' => $errors]);
    } else {
        echo json_encode(['saved' => $saved]);
    }
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => $e->getMessage()]);
}
