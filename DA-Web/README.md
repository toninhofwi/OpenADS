# DA-Web — OpenADS Data Architect

Web-based replacement for SAP Data Architect. Manages OpenADS data dictionaries through a browser: browse tables, views, stored procedures, functions, triggers, RI objects, users, groups, and links. Execute ad-hoc SQL, view and edit table structure, and generate DDL scripts. All server-side operations go through the **php_openads** native PHP extension backed by **openace64.dll**.

---

## Dependencies

| Component | Role | Notes |
|-----------|------|-------|
| **openace64.dll** | OpenADS engine (ACE-compatible DLL) | Built from `F:\OpenADS\src`; deployed to `C:\php\openace64.dll` |
| **php_openads.dll** | PHP Zend extension wrapping openace64 | Deployed to `C:\php\ext\php_openads.dll` |
| **PHP 8.0 x64 (ZTS)** | Server-side language | Must be Thread-Safe build (`php8ts.dll`) |
| **Apache 2.4 x64** | Web server | mod_php or PHP-FPM |
| **jQuery 3.7.x** | DOM manipulation, jsTree dependency | Auto-downloaded by `setup.bat` |
| **jsTree 3.3.x** | Lazy-loading navigation tree | Auto-downloaded |
| **Tabulator 6.x** | Data grid for tables, fields, indexes, permissions | Auto-downloaded |
| **Split.js 1.6.x** | Resizable panes (tree ↔ content) | Auto-downloaded |
| **Ace Editor** | SQL code editor | Loaded from CDN (ace.js) |

### Why OpenAce64.dll and not Ace64.dll

OpenADS was originally built with the output name **ace64.dll** so it could act as a transparent drop-in replacement for SAP's Advantage Database Server engine — existing applications would load it automatically without recompilation. However, that naming strategy creates a real problem in mixed environments:

- Both SAP ACE and OpenADS export the exact same C function names (`AdsConnect`, `AdsDisconnect`, `AdsQuery`, etc.)
- If both `ace64.dll` files land in the same directory or on the same `PATH`, Windows loads whichever one it finds first — with no warning and unpredictable results
- It makes it impossible to run the SAP-backed `php_advantage` extension alongside the OpenADS-backed `php_openads` extension in the same PHP installation
- Debugging is harder: a crash or wrong-query result could come from either engine, with no way to tell them apart from the filename alone

**The fix: ship as `openace64.dll`.**

The OpenADS engine exports the identical ACE API surface but under a distinct DLL name. This means:

- `C:\php\ace64.dll` — SAP ACE engine (loaded by `php_advantage.dll`)
- `C:\php\openace64.dll` — OpenADS engine (loaded by `php_openads.dll`)

Both can coexist in the same PHP installation. PHP loads each extension independently via `php.ini`:

```ini
extension=php_advantage    ; links against ace64.dll   — SAP engine
extension=php_openads      ; links against openace64.dll — OpenADS engine
```

Windows resolves each extension's DLL imports to the correct file. Applications that genuinely need the drop-in-replacement behaviour (e.g. Harbour/RDDads, legacy Delphi apps) can still copy `openace64.dll` to the application directory and rename it `ace64.dll` locally — that remains a supported deployment pattern. The only change is that we no longer ship under that name by default.

---

## Installation

### 1. Build the engine

```bat
cd F:\OpenADS
build_windows.bat
:: Output: build\msvc-x64\src\Release\openace64.dll
```

### 2. Build the PHP extension

```bat
cd F:\OpenADS\bindings\php_ext
build.bat
:: Compiles php_openads.dll and patches the PE linker version for PHP 8.0 VS16 compatibility
```

### 3. Deploy

```bat
cd F:\OpenADS\bindings\php_ext
nmake /f Makefile.win install
:: Copies php_openads.dll  → C:\php\ext\
:: Copies openace64.dll    → C:\php\
```

### 4. Enable in php.ini

```ini
; add these two lines
extension=php_openads
```

Restart Apache after any DLL change.

### 5. Install DA-Web vendor files

```bat
cd F:\OpenADS\DA-Web
setup.bat
```

This downloads jQuery, jsTree, Tabulator, and Split.js into `vendor/`.

### 6. Configure Apache virtual host

Point the document root at `F:\OpenADS\DA-Web`. Example `httpd-vhosts.conf` snippet:

```apache
<VirtualHost *:80>
    DocumentRoot "F:/OpenADS/DA-Web"
    <Directory "F:/OpenADS/DA-Web">
        AllowOverride None
        Require all granted
    </Directory>
</VirtualHost>
```

---

## Connecting to a Data Dictionary

1. Open the browser at `http://localhost/` (or your configured host).
2. In the **Dictionaries** menu, click **Manage Dictionaries** to register a DD.
3. Click the DD name in the tree, then enter credentials when prompted.

### Sample: pmsys.add (Property Management System)

Register with these settings:

| Field | Value |
|-------|-------|
| Name | `Pmsys-TestData` |
| Path | `f:\OpenADS\testdata\pmsys\pmsys.add` |
| Username | `adssys` |
| Connection type | Local |
| Entry type | Data Dictionary |

Once connected, expand the tree to browse: Tables, Views, Stored Procedures, Functions, Users, Groups, RI Objects, Links.

### Sample: Aquarium (free-table directory)

Register with these settings:

| Field | Value |
|-------|-------|
| Name | `Aquarium` |
| Path | `f:\OpenADS\testdata\Aquarium\` |
| Entry type | Free Tables |

Free-table directories expose `.adt` and `.dbf` table files directly — no Data Dictionary file is needed.

---

## Feature Guide (using pmsys.add as sample)

### Tables

Click **Tables** → **leases** to open a browseable data grid (2,000-row limit with load-more).

### Fields (table structure)

Expand **leases** in the tree and click **Fields**. The grid shows:
- Field name, Type, Size, Decimals
- **Required** (Yes = cannot be null, sourced from DD `Field_Can_Be_Null` property)
- **Default** (default value stored in DD)
- **Index** membership (No / Yes / Primary)

All cells are editable. Click **Save Changes** to write Required and Default values back to the DD.

### Indexes

Expand **leases** → **Indexes**. The grid shows:
- Tag name, Expression, Descending, Unique, Binary
- Editable: Expression, Descending, Unique, Binary
- **Save Changes** drops the selected index tag and recreates it with new settings via `sp_CreateIndex90`.

### Triggers

Expand **leases** → **Triggers**. A split panel opens:
- **Top**: metadata grid with columns Name, Timing (BEFORE / INSTEAD OF / AFTER), Event (INSERT / UPDATE / DELETE), Enabled, Priority — all editable dropdowns
- **Bottom**: Ace SQL editor showing the selected trigger's full body (read from the `.add/.am` binary directly)
- **Save Changes** writes Timing, Event, Enabled, and the SQL body back to the DD

Leases has two triggers:
- `Insert AuditLog` — INSTEAD OF INSERT (fires on insert, manually performs the insert + logs)
- `Update AuditLog` — BEFORE UPDATE (logs changes before they are committed)

### Generate SQL

Expand **leases** → **Generate SQL** to open a full SAP-compatible DDL script in the SQL editor:
```sql
CREATE TABLE leases (
    leaseid CIChar( 13 ),
    ...
) IN DATABASE;
EXECUTE PROCEDURE sp_CreateIndex90(...);
...
CREATE TRIGGER [Update AuditLog]
   ON leases
   BEFORE
   UPDATE
BEGIN
   ...
END
   NO MEMOS
   PRIORITY 1;
```

### Stored Procedures & Functions

Click any stored procedure or function to open an Ace editor pre-loaded with its body. The **Parameters** grid below shows input/output parameters (editable). Click **Save to DD** to persist body and parameter changes.

### SQL Editor

Click the **+** button in the tab bar (or use **File → New SQL**) to open a blank SQL editor. Select a database from the dropdown, write SQL, and press **F5** or **Execute**.

### Users

Click any user name under **Users** to open a Group Memberships tab showing the groups this user belongs to. Use **+ Add Group** (dropdown of all groups not already assigned), **Remove Selected**, and **Save Changes** to manage memberships.

### Groups

Click any group name under **Groups** to open a Permissions grid showing per-object access rights (Select, Insert, Update, Delete, Execute) across all DD objects. Edit the Yes/No cells and click **Save Changes** to issue GRANT/REVOKE statements.

### RI Objects

Click any entry under **RI Objects** to open a form with:
- Parent Table + Primary Key tag (dropdown)
- Child Table + Foreign Key tag (dropdown)
- Update Rule / Delete Rule (Restrict / Cascade / SetNull)
- **Save RI** and **Delete RI** buttons

Tag dropdowns populate automatically when you select a table.

### Views

Click any view under **Views** to open its SQL definition in the editor.

---

## API Reference

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `api/connect.php` | POST | Open/close DD connection |
| `api/dictionaries.php` | GET/POST | CRUD for registered DDs |
| `api/tree.php` | GET | jsTree lazy-load node provider |
| `api/table_data.php` | GET | Fetch table rows (max 2000) |
| `api/table_meta.php` | GET | Field, index, or trigger metadata |
| `api/execute_sql.php` | POST | Execute arbitrary SQL |
| `api/row_ops.php` | POST | Insert / update / delete a row |
| `api/save_meta.php` | POST | Save field property changes to DD |
| `api/save_index.php` | POST | Drop and recreate an index tag |
| `api/trigger_body.php` | POST | Read full trigger body from .add/.am binary |
| `api/save_trigger.php` | POST | Save trigger event/timing/enabled/body to DD |
| `api/proc_body.php` | POST | Read stored procedure / function body |
| `api/save_proc.php` | POST | Save proc/function body and parameters |
| `api/gen_sql.php` | GET | Generate CREATE TABLE + index + trigger DDL |
| `api/group_meta.php` | GET | Group permissions from system.permissions |
| `api/save_group_meta.php` | POST | GRANT/REVOKE permissions for a group |
| `api/user_groups.php` | GET | User's group memberships |
| `api/save_user_groups.php` | POST | Add/remove user from groups |
| `api/ri_meta.php` | GET | RI object details, table list, tag list |
| `api/save_ri.php` | POST | Create / update / delete an RI object |
| `api/sql_scripts.php` | GET/POST | Saved SQL script CRUD |
| `api/session_state.php` | GET | Currently-open connections |
| `api/create_dd.php` | POST | Create a new Data Dictionary |

---

## Directory Structure

```
DA-Web/
├── index.php               Main application shell (HTML + layout)
├── setup.bat               Downloads all vendor files (run once)
├── api/                    PHP backend endpoints (see table above)
├── js/
│   └── app.js              All frontend logic (~1800 lines)
├── css/
│   └── app.css             Dark-theme stylesheet
├── config/
│   ├── dictionaries.json   Registered DD list (persisted across sessions)
│   └── sql_scripts.json    Saved SQL scripts
└── vendor/                 Auto-downloaded client libraries
    ├── jquery/
    ├── jstree/
    ├── tabulator/
    └── split.js/
```
