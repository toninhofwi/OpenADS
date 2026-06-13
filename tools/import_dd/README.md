# openads_import_dd

One-shot migration tool that reads all group memberships and fine-grained ACL
permissions from an existing **SAP ADS** `.add` file using the SAP ACE runtime,
then writes them into a copy of that file using the OpenADS native engine.
After the import the copy is a self-contained OpenADS DD — it no longer
requires the SAP DLL for permission enforcement.

## Motivation

SAP ADS stores fine-grained permissions and DB:-group memberships in encrypted
binary blobs inside the `.add` file that OpenADS cannot decode without the SAP
DLL. When OpenADS opens a SAP-created DD it returns error **5174
(`AE_SAP_PERMS_NEED_IMPORT`)** from `AdsConnect60` to signal that the
permissions must be imported before OpenADS can enforce them.

Run this tool once. The resulting `.add` file is OpenADS-native and no longer
needs SAP ACE at any point — not at runtime and not ever again.

---

## Location in the repository

```
tools/import_dd/
├── CMakeLists.txt
├── main.cpp
└── README.md        ← this file
```

---

## Platform / built output

| Platform | Preset / build dir | Output binary |
|---|---|---|
| Windows x64 (MSVC Debug) | `build/msvc-x64` | `build/msvc-x64/tools/import_dd/Debug/openads_import_dd.exe` |
| Windows x64 (MSVC Release) | `build/msvc-x64` | `build/msvc-x64/tools/import_dd/Release/openads_import_dd.exe` |
| Linux x64 (GCC/Clang) | `build/ninja-linux` | `build/ninja-linux/tools/import_dd/openads_import_dd` |
| macOS x64/arm64 | `build/ninja-macos` | `build/ninja-macos/tools/import_dd/openads_import_dd` |

> **macOS note — the tool BUILDS but CANNOT RUN on macOS.**
> SAP never released an ACE shared library for macOS. Without `libace64.dylib`
> the tool exits immediately with "Cannot load SAP ACE library."
> If you need to migrate a DD for use on macOS, run the import on a Windows or
> Linux machine first, then copy the resulting `.add` file to the Mac.

---

## How to build

### Windows (MSVC 2022)

```powershell
# From the repository root. Requires cmake in PATH (VS installer provides it).
cmake -B build/msvc-x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/msvc-x64 --target openads_import_dd --config Release
```

The `.exe` is statically linked against `openads_core` — no OpenADS installation
needed on the target machine.

### Linux (GCC or Clang + Ninja)

```bash
cmake -B build/ninja-linux -G Ninja \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build/ninja-linux --target openads_import_dd
```

`libdl` is linked automatically by CMake (`${CMAKE_DL_LIBS}`). No other system
library is required beyond the standard C++ runtime.

### macOS (builds but cannot perform imports — see note above)

```bash
cmake -B build/ninja-macos -G Ninja \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build/ninja-macos --target openads_import_dd
```

---

## What it does

1. Copies `--source` → `--dest` (unless `--no-copy`), including the companion
   `.am` memo file (holds SQL body continuations for long stored procedures).
2. Loads the SAP ACE shared library (`ace64.dll` / `libace64.so`).
3. Connects to the **source** DD via the SAP LOCAL server.
4. Queries `system.usergroupmembers WHERE Group_Name LIKE 'DB:%'` — collects
   every user that belongs to a DB:-built-in group (DB:Admin, DB:Backup,
   DB:Debug, DB:Public).
5. Queries `system.functions` — collects the return type, input parameters,
   and SQL body for every user-defined function (SAP decrypts these on the fly).
6. Queries `SELECT * FROM system.permissions` — collects every fine-grained
   ACL grant (table, view, stored procedure, function, database-level).
7. Opens the **destination** copy with OpenADS `DataDict::open()`.
8. Calls `add_user_to_group()` for each collected membership (auto-creates the
   Group record for DB: built-ins if none exists).
9. Calls `grant_permission()` for each ACL row, deactivating the SAP
   `0x80000000`-sentinel record and writing an OpenADS-native bitmask record.
10. Binary-patches each Function record in the dest `.add` file: overwrites the
    SAP-encrypted property blob with a plain `lstr` layout (plen=0, followed by
    length-prefixed strings for return type, input parameters, and body) so that
    `proc_body.php` can display the function body without needing the SAP DLL.
11. Prints a JSON result to **stdout** and exits.

---

## Usage

```
openads_import_dd --source <sap.add>  --dest <copy.add>
                  --user   <name>     --password <pw>
                  [--sap-lib <path/to/ace64.dll>]
                  [--no-copy]
```

| Flag | Required | Description |
|---|---|---|
| `--source` | Yes | Path to the original SAP-created `.add` file. Never modified. |
| `--dest` | Yes | Path for the new OpenADS-compatible copy. Overwritten unless `--no-copy`. |
| `--user` | Yes | SAP DD administrator username (needs SELECT on `system.*`). |
| `--password` | No | Password. Omit or pass empty string for password-less accounts. |
| `--sap-lib` | No | Explicit path to the SAP ACE shared library. Falls back to platform defaults when omitted (see Per-OS Behavior). |
| `--no-copy` | No | Skip the copy step — import into an already-existing `--dest`. |

---

## What is imported

| Data | Source | OpenADS action |
|---|---|---|
| `.am` memo file | filesystem copy of `source.am` | copied to `dest.am` |
| DB: group memberships | `system.usergroupmembers WHERE Group_Name LIKE 'DB:%'` | `add_user_to_group(user, group)` |
| Function bodies | `system.functions` (SAP decrypts on the fly) | binary-patch dest `.add` with lstr layout |
| Table / View / StoredProc / Function / Database ACL grants | `system.permissions` | `grant_permission(obj_type, obj_name, grantee, bitmask)` |

The `ADS_PERMISSION_*` bitmask bits imported per ACL row:

| Bit | Value | Right |
|---|---|---|
| 0 | `0x001` | SELECT / READ |
| 1 | `0x002` | UPDATE |
| 2 | `0x004` | EXECUTE |
| 4 | `0x010` | INSERT |
| 5 | `0x020` | DELETE |
| 6 | `0x040` | ACCESS |
| 7 | `0x080` | CREATE |
| 8 | `0x100` | ALTER |
| 9 | `0x200` | DROP |

Regular users, groups, table definitions, indexes, triggers, stored procedures,
views, and referential integrity rules are already compatible between SAP ADS
and OpenADS and are **not** touched by this tool.

---

## Per-OS behavior

### Windows

SAP ACE DLL: `ace64.dll` (64-bit). Default search order when `--sap-lib` is
omitted:

1. `f:\ads11\ace64.dll`
2. `C:\Program Files (x86)\Advantage 11.10\ace64.dll`
3. `C:\Program Files (x86)\Advantage 10.10\ace64.dll`
4. `ace64.dll` in `%PATH%`

Functions are resolved by **name** (`AdsConnect60`, `AdsDisconnect`, etc.) on
all platforms. Named exports are stable across all SAP ACE versions; ACE v8,
v9, v10, and v11 all export the same public API names.

### Linux

SAP ACE shared object: `libace64.so`. Default search order:

1. `/usr/lib/libace64.so`
2. `/opt/ads/lib/libace64.so`
3. `libace64.so` in `LD_LIBRARY_PATH`

Functions are resolved by **name** — no ordinals on ELF. `libdl` is linked by
CMake and is part of glibc.

### macOS

> **Not supported.** SAP never published a macOS build of ACE. The binary
> compiles cleanly but fails at runtime when it cannot load the SAP library.
> **Perform the import on Windows or Linux, then copy the resulting `.add`
> file to your Mac.**

---

## Output

The tool writes a single JSON object to **stdout**. Exit code 0 = success,
exit code 1 = fatal error.

### Success

```json
{
  "ok": true,
  "memberships": 25,
  "permissions": 559,
  "function_bodies": 9,
  "warnings": []
}
```

### Fatal error

```json
{
  "ok": false,
  "error": "SAP connect failed (rc=7077). Check credentials and source path.",
  "warnings": []
}
```

`warnings` is always present and collects non-fatal per-row failures
(e.g. a grantee in the SAP DD that does not exist in the destination DD).
The import continues past individual warnings.

---

## Standalone / Dependencies

The binary is **fully self-contained**. There is no installer or framework.

| Dependency | How satisfied |
|---|---|
| OpenADS engine (`openads_core`) | **Statically linked** at build time |
| SAP ACE shared library | **Loaded at runtime** via `LoadLibrary`/`dlopen`. Tool exits gracefully if not found. Not needed at link time. |
| C++ standard runtime | MSVC: `vcruntime140.dll` (always present); Linux/macOS: `libc++`/`libstdc++` |
| `libdl` (Linux only) | Linked by CMake; part of glibc |

Copy the single binary to any machine of the same OS/arch and run it — no
OpenADS installation required on the target.

---

## Calling from PHP (using php_ads — SAP ACE extension)

`php_ads` / `php_advantage` (`F:\php_advantage`) provides direct PHP bindings
to the SAP ACE engine. The `openads_import_dd` tool is a **separate native
binary** that handles the SAP connection internally, so you invoke it via a
subprocess call — not through the extension. Use `php_ads` to obtain or verify
SAP credentials before invoking the tool.

```php
<?php
/**
 * Import a SAP DD into OpenADS from a PHP script that has php_ads loaded.
 * The extension is NOT used for the import itself — it's used here only
 * to verify credentials before invoking the native tool.
 */

function importSapDD(
    string $source,
    string $dest,
    string $user,
    string $password,
    string $importBin = 'openads_import_dd',  // or full path to .exe on Windows
    string $sapLib   = ''                      // '' = use tool's default search
): array {
    $cmd = [
        $importBin,
        '--source',   $source,
        '--dest',     $dest,
        '--user',     $user,
        '--password', $password,
    ];
    if ($sapLib !== '') {
        $cmd[] = '--sap-lib';
        $cmd[] = $sapLib;
    }

    $descriptors = [
        0 => ['pipe', 'r'],   // stdin  (not used)
        1 => ['pipe', 'w'],   // stdout — JSON result
        2 => ['pipe', 'w'],   // stderr — ignored
    ];
    $proc = proc_open($cmd, $descriptors, $pipes);
    if (!is_resource($proc)) {
        return ['ok' => false, 'error' => 'Failed to launch openads_import_dd'];
    }
    fclose($pipes[0]);
    $output = stream_get_contents($pipes[1]);
    fclose($pipes[1]);
    fclose($pipes[2]);
    proc_close($proc);

    $result = json_decode($output, true);
    return is_array($result) ? $result : ['ok' => false, 'error' => 'No JSON output'];
}

// Example usage
$result = importSapDD(
    source:    'C:/Data/myapp.add',
    dest:      'C:/Data/myapp_openads.add',
    user:      'AdsSysAdmin',
    password:  'secret',
    importBin: 'C:/OpenADS/bin/openads_import_dd.exe',
    sapLib:    'C:/Program Files (x86)/Advantage 11.10/ace64.dll'
);

if ($result['ok']) {
    printf("Imported %d memberships, %d permissions.\n",
        $result['db_memberships'], $result['permissions']);
    foreach ($result['warnings'] as $w) {
        echo "Warning: $w\n";
    }
} else {
    echo "Error: " . $result['error'] . "\n";
}
```

> **Note:** `proc_open` with an array command never passes arguments through a
> shell, so no `escapeshellarg` wrapping is needed. Passing paths with spaces
> or special characters is safe.

---

## Calling from Python (using python_ads)

`python_ads` (`F:\python_ads`) provides Python bindings to SAP ACE. As with
PHP, the native tool handles the SAP connection on its own — use Python's
`subprocess` module to invoke it.

```python
import subprocess
import json
from pathlib import Path


def import_sap_dd(
    source: str,
    dest: str,
    user: str,
    password: str,
    import_bin: str = "openads_import_dd",  # or full path on Windows
    sap_lib: str = "",
) -> dict:
    """
    Run openads_import_dd and return the parsed JSON result dict.
    Raises subprocess.CalledProcessError on non-zero exit if check=True.
    """
    cmd = [
        import_bin,
        "--source",   source,
        "--dest",     dest,
        "--user",     user,
        "--password", password,
    ]
    if sap_lib:
        cmd += ["--sap-lib", sap_lib]

    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        # tool exits 0 on success, 1 on fatal error
    )
    if not result.stdout.strip():
        return {"ok": False, "error": f"No output (exit {result.returncode})"}

    return json.loads(result.stdout)


# Example usage
data = import_sap_dd(
    source    = r"C:\Data\myapp.add",
    dest      = r"C:\Data\myapp_openads.add",
    user      = "AdsSysAdmin",
    password  = "secret",
    import_bin= r"C:\OpenADS\bin\openads_import_dd.exe",
    sap_lib   = r"C:\Program Files (x86)\Advantage 11.10\ace64.dll",
)

if data.get("ok"):
    print(f"Imported {data['db_memberships']} memberships, "
          f"{data['permissions']} permissions.")
    for w in data.get("warnings", []):
        print(f"Warning: {w}")
else:
    print(f"Error: {data.get('error')}")
```

---

## Calling from DA-Web

DA-Web exposes a built-in wizard at **Tools → Import SAP DD…** that calls the
`api/import_sap_dd.php` endpoint. That endpoint locates the binary (via the
`OPENADS_IMPORT_DD_BIN` environment variable or a set of well-known paths),
runs it via `proc_open`, and registers the imported DD automatically.

To call the API directly from custom PHP code on the OpenADS FastCGI pool
(port 8080, `php_openads.dll` loaded):

```php
$payload = json_encode([
    'name'     => 'MyApp (imported)',
    'source'   => 'C:/Data/myapp.add',
    'dest'     => 'C:/Data/myapp_openads.add',
    'user'     => 'AdsSysAdmin',
    'password' => 'secret',
    'sapLib'   => '',   // leave empty to use tool defaults
]);

$ch = curl_init('http://localhost:8080/api/import_sap_dd.php');
curl_setopt_array($ch, [
    CURLOPT_POST           => true,
    CURLOPT_POSTFIELDS     => $payload,
    CURLOPT_RETURNTRANSFER => true,
    CURLOPT_HTTPHEADER     => ['Content-Type: application/json'],
]);
$json   = curl_exec($ch);
$result = json_decode($json, true);
curl_close($ch);

if ($result['ok']) {
    // DD is now registered and ready in DA-Web
}
```
