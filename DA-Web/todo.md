1. REMOTE server support follow-up.

   Done:
   - DA-Web connected-DD endpoints now use `api_ads_connect_opts($c)` instead of manually building local-only connection options.
   - `api_ads_connect_opts()` now sends the native `serverType` bitmask, so `connType=remote` reaches `AdsConnect60()` as `ADS_REMOTE_SERVER`.
   - RCB 06/30/2026: DA-Web's implicit remote URI uses port `6264` because local port `6262` is reserved for SAP ADS on this workstation.
   - `php_openads` was rebuilt so `AdsConnection::connect()` also accepts `server_type` / `connType` string aliases.
   - Rebuilt `php_openads.dll` was copied to `C:\php\ext` after Apache was stopped.
   - Rebuilt `openads_serverd.exe` after fixing remote server-side DD credential reuse for SQL/index ABI handles.
   - Rebuilt `openace64.dll` and copied the Release DLL to `C:\php\openace64.dll` and `C:\php\ext\openace64.dll`.
   - Restarted Apache service `Structured Systems ADS httpd`.
   - Started `openads_serverd` on `127.0.0.1:6264` with data root `C:\Pmsys\data`.
   - Verified DA-Web over Apache can remote-connect, expand tables, execute SQL, query `system.permissions`, and browse `catcodes`.

   Remaining:
   - Investigate table browse timeout for `attachments`; the endpoint works over REMOTE for normal tables, but this table hit the FastCGI 30-second timeout.

2. Metadata performance follow-up.

   Done:
   - RCB 06/30/2026: Added core `DataDict` permission indexes by grantee, object, and grantee/object/type.
   - RCB 06/30/2026: `system.permissions` now uses indexed permission sources and can push down simple `GRANTEE = ...` / `OBJ_NAME = ...` filters before materializing the memory table.
   - RCB 06/30/2026: DA-Web user/group permission endpoints now query scoped `system.permissions` rows instead of scanning the full compatibility matrix in PHP.
   - RCB 06/30/2026: DA-Web permission endpoints now return phase timing diagnostics, and the user/group permission tabs show compact timing badges with phase details in tooltips.
   - RCB 06/30/2026: Extended core DD metadata indexes to table index files, RI parent/child lookups, trigger table/event lookups, and reverse group membership.
   - RCB 06/30/2026: RI enforcement/snapshot and trigger firing now use scoped DD lookups instead of scanning all DD RI/trigger metadata on each operation.
   - RCB 06/30/2026: `system.indexes`, `system.primarykeys`, `system.triggers`, and `system.usergroupmembers` now use indexed/scoped DD paths when simple equality predicates are present.
   - RCB 06/30/2026: Rebuilt `openace64.dll` and `openads_serverd.exe`, deployed `openace64.dll` to `C:\php` and `C:\php\ext`, restarted Apache, restarted `openads_serverd` on `127.0.0.1:6264`, and verified unit/API/SQL smoke tests.

3. Administrator cockpit / DD Health.

   Done:
   - RCB 06/30/2026: Added a DD-level Health tree node and tab in DA-Web.
   - RCB 06/30/2026: Added `api/health.php` to report table/index file checks for local DDs, remote filesystem-skip notice, RI table references, trigger table references, mixed-case principal duplicates, temp table path validity, and orphan permission grants.
   - RCB 06/30/2026: Health results include summary counts, a findings grid, refresh, and timing diagnostics.
   - RCB 06/30/2026: Added core `system.permission_grants` and `system.permission_issues` catalogs so clients can inspect direct grants and permission health without materializing SAP-compatible zero rows from `system.permissions`.
   - RCB 06/30/2026: Health permission checks now run through core `system.permission_issues`; remote PMSys_OpenADS Health dropped from about 3.2 seconds to about 0.22 seconds.
   - RCB 07/01/2026: Health RI checks now validate parent/child tags, tag field references, field counts, and field type/length/decimal compatibility for RI tables.
   - RCB 07/01/2026: Health procedure/function checks now validate readable metadata, parameter strings, function return types, body balance, and source header names.
   - RCB 07/01/2026: SAP import warning history now has a DA-Web API endpoint and modal review panel for recent warning-bearing imports.

   Remaining:
   - None currently tracked.
