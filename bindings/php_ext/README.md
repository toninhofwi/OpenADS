# php_openads — Native PHP Extension for OpenADS

Native PHP 8 extension for the **OpenADS** database engine
([github.com/FiveTechSoft/OpenADS](https://github.com/FiveTechSoft/OpenADS)).
Provides a high-performance, object-oriented API over the OpenADS ACE C library
via a compiled Zend extension — no FFI, no Composer, direct native calls.

---

## Two PHP bindings — which one to use?

The OpenADS repository ships two separate PHP bindings under `bindings/`:

| | `bindings/php` | `bindings/php_ext` |
|---|---|---|
| **Technology** | PHP FFI (`ext-ffi`) | Native Zend C extension (compiled DLL) |
| **Install** | `composer require openads/openads-php` | build + copy DLL, add to `php.ini` |
| **Compilation** | None — pure PHP | VS2022 + NMake on Windows |
| **PHP requirement** | PHP 8.1+, `ffi.enable=1` in php.ini | PHP 8.0+ x64 ZTS |
| **Namespace** | `OpenADS\Connection`, `OpenADS\Table`, … | `AdsConnection`, `AdsTable`, … |
| **API style** | OOP with namespaces, Composer autoload | Global classes, identical to `php_advantage` |
| **Performance** | FFI dispatch overhead per call | Zero overhead — direct ACE SDK calls |
| **Portability** | Cross-platform (Windows + Linux + macOS) | Windows x64 only |
| **Best for** | Development, Composer-based projects, Linux | Production web servers, maximum throughput |

### Why `php_ext` exists

`bindings/php` uses PHP's built-in FFI extension to call the ACE C library at runtime,
with no compilation step. This is convenient for development environments and cross-platform
deployments — it runs anywhere PHP 8.1 and the ACE shared library are available.

`bindings/php_ext` is a **pure C Zend extension** built the same way every first-party
PHP extension is built (e.g. `ext/pdo`, `ext/mysqli`, `ext/imagick`). The C compiler
sees the Zend internals directly — no FFI indirection, no marshalling layer, no
`ffi.enable` requirement. Every method call from PHP reaches the ACE SDK through
a single compiled C function. This matches the architecture of `php_advantage`
(the equivalent extension for SAP Advantage Database Server) so both extensions
present an identical API surface regardless of the underlying engine.

### API compatibility with `php_advantage`

`php_ext` and `php_advantage` expose the **same classes and method signatures**.
Code written against one works against the other by swapping the loaded extension
and adjusting the connection path:

```php
// Works with both php_openads and php_advantage:
$conn = AdsConnection::connect(['path' => $dbPath, 'user' => $u, 'password' => $p]);
$stmt = $conn->query("SELECT * FROM customers WHERE active = 1");
while ($row = $stmt->fetchAssoc()) { ... }
```

The only behavioural differences are engine-specific (e.g. `AdsDictionary::setTriggerProperty`
is supported by OpenADS but throws on SAP ACE; trigger creation requires fewer arguments on
OpenADS — see [AdsDictionary differences](#adsdictionary-differences-vs-php_advantage)).

---

## Requirements

| Component | Version |
|---|---|
| PHP | 8.0.x x64 ZTS (Thread Safe) |
| OpenADS ACE library | `openace64.dll` from the OpenADS build |
| Compiler | VS 2022 x64 |
| OS | Windows x64 |
| PHP Dev Pack | `php-8.0.1-devel-vs16-x64` |

---

## Directory layout

```
bindings/php_ext/
├── src/
│   ├── php_ads.h           # Shared header — structs, macros, class-entry externs
│   ├── php_ads.c           # Module entry (MINIT/RINIT), constants, shared helpers
│   ├── ads_connection.c    # AdsConnection class
│   ├── ads_statement.c     # AdsStatement class
│   ├── ads_table.c         # AdsTable class
│   ├── ads_misc.c          # AdsTransaction + AdsDictionary classes
│   ├── ads_prepared.c      # AdsPreparedStatement class
│   └── ads_arginfo.h       # PHP 8 arginfo descriptors for all methods
├── bin/                    # Build output: php_openads.dll
├── obj/                    # Intermediate object files
├── openace64.lib           # OpenADS ACE import library
├── openace64.def           # OpenADS ACE export definitions
├── Makefile.win            # NMake build file
└── build.bat               # One-shot build + PE linker patch script
```

---

## Building from source

### 1. Prerequisites

- Install **Visual Studio 2022** (Community or Build Tools) with the *Desktop development with C++* workload.
- Install **PHP 8.0.x ZTS x64** to `C:\php\`.
- Build **OpenADS** first — `openace64.dll` is expected at
  `F:\OpenADS\build\msvc-x64\src\Release\ace64.dll`.

### 2. Build

```bat
cd F:\OpenADS\bindings\php_ext
build.bat
```

The script:
1. Initialises the VS2022 x64 toolchain via `vcvars64.bat`.
2. Runs `nmake /f Makefile.win` — produces `bin\php_openads.dll`.
3. Patches the PE linker version from 14.4x → 14.28 (to match PHP 8.0.1 VS16).

### 3. Install

```bat
nmake /f Makefile.win install
```

This copies `php_openads.dll` to `C:\php\ext\` and `openace64.dll` to `C:\php\`.

### 4. Enable in php.ini

```ini
extension=php_openads
```

### 5. Verify

```bat
C:\php\php.exe -m | findstr openads
```

Expected: `openads`

---

## API reference

All errors throw `AdsException` (extends `RuntimeException`).
`getCode()` returns the native ACE error number.

---

### AdsConnection

Static factory — do not use `new`.

```php
$conn = AdsConnection::connect([
    'path'       => '\\\\server\\share\\mydb.add',  // required
    'user'       => 'admin',                         // optional
    'password'   => 'secret',                        // optional
    'serverType' => ADS_REMOTE_SERVER,               // optional, default LOCAL|REMOTE
    'options'    => 0,                               // optional ulOptions bitmask
]);
```

| Method | Returns | Description |
|---|---|---|
| `connect(array $opts)` | `AdsConnection` | Open connection (static) |
| `close()` | `void` | Disconnect |
| `query(string $sql)` | `AdsStatement` | Execute SELECT |
| `execute(string $sql)` | `bool` | Execute INSERT / UPDATE / DELETE / DDL |
| `prepare(string $sql)` | `AdsPreparedStatement` | Prepare a parameterised statement |
| `beginTransaction()` | `AdsTransaction` | Start a transaction |
| `isAlive()` | `bool` | Check connection health |

---

### AdsStatement

Returned by `AdsConnection::query()`.

| Method | Returns | Description |
|---|---|---|
| `fetchAssoc()` | `array\|false` | Next row as associative array, `false` at EOF |
| `fetchRow()` | `array\|false` | Next row as indexed array, `false` at EOF |
| `fetchAll()` | `array` | All remaining rows as array of associative arrays |
| `columnCount()` | `int` | Number of columns |
| `rowCount()` | `int` | Total rows in result set |
| `close()` | `void` | Free statement and cursor |

---

### AdsPreparedStatement

Returned by `AdsConnection::prepare()`. Use for any query with user-supplied values.

Parameter names accept both `:name` and `name` forms. Positional `?` is not supported.

| Method | Description |
|---|---|
| `bind(string $name, mixed $value)` | Auto-detect PHP type (null/bool/int/float/string) |
| `bindString(string $name, string $value)` | String / memo |
| `bindInt(string $name, int $value)` | Integer |
| `bindDouble(string $name, float $value)` | Double / numeric |
| `bindBool(string $name, bool $value)` | Logical |
| `bindDate(string $name, string $value)` | Date — format `CCYYMMDD` |
| `bindTimestamp(string $name, string $value)` | Timestamp — `YYYY-MM-DD HH:MM:SS` |
| `bindMoney(string $name, int $value)` | SIGNED64 scaled integer (e.g. $10.00 = 100000 for 4dp) |
| `bindBinary(string $name, string $data, int $type = ADS_BINARY)` | Binary blob / image |
| `bindNull(string $name)` | NULL |
| `execute()` | `AdsStatement` (SELECT) or `true` (DML/DDL) |
| `paramCount()` | `int` |
| `close()` | `void` |

`bind()` type mapping:

| PHP type | ACE call |
|---|---|
| `null` | `AdsSetNull` |
| `bool` | `AdsSetLogical` |
| `int` | `AdsSetLong` |
| `float` | `AdsSetDouble` |
| `string` | `AdsSetString` |

After a SELECT `execute()`, the prepared handle transfers to the returned `AdsStatement`.
Call `prepare()` again before the next iteration.

---

### AdsTransaction

Returned by `AdsConnection::beginTransaction()`.

| Method | Description |
|---|---|
| `commit()` | Commit and deactivate |
| `rollback()` | Roll back and deactivate |
| `isActive()` | `bool` |

---

### AdsTable

Direct (non-SQL) table access — bypasses the SQL parser.

```php
$tbl = AdsTable::open(
    $conn,
    'C:\\data\\customers.adt',
    ADS_ADT,                    // table type
    ADS_COMPATIBLE_LOCKING,     // lock type
    ADS_ANSI,                   // character set
    ADS_SHARED                  // open mode
);
```

| Method | Returns | Description |
|---|---|---|
| `gotoTop()` | `void` | Move to first record |
| `gotoBottom()` | `void` | Move to last record |
| `gotoRecord(int $n)` | `void` | Jump to absolute record number |
| `skip(int $n = 1)` | `void` | Skip forward (or backward if negative) |
| `atEOF()` | `bool` | Past the last record |
| `atBOF()` | `bool` | Before the first record |
| `recordCount()` | `int` | Total records |
| `recordNum()` | `int` | Current record number |
| `getRecord()` | `array` | All fields as associative array |
| `getString(string $field)` | `string` | |
| `getLong(string $field)` | `int` | |
| `getDouble(string $field)` | `float` | |
| `getLogical(string $field)` | `bool` | |
| `setString(string $field, string $value)` | `void` | Buffer change (call `writeRecord()` to flush) |
| `setLong(string $field, int $value)` | `void` | |
| `setDouble(string $field, float $value)` | `void` | |
| `setLogical(string $field, bool $value)` | `void` | |
| `appendRecord()` | `void` | Append blank record and lock for editing |
| `writeRecord()` | `void` | Flush pending changes to disk |
| `cancelUpdate()` | `void` | Discard pending changes |
| `deleteRecord()` | `void` | Mark current record deleted |
| `close()` | `void` | Close the table |

---

### AdsDictionary

Full CRUD access to the OpenADS data dictionary. Open against a `.add` file
directly or borrow an existing connection handle.

```php
// Open independently
$dd = AdsDictionary::open('\\\\srv\\data\\mydb.add', 'admin', 'secret');

// Or borrow an existing connection (close() is a no-op — caller owns the handle)
$dd = AdsDictionary::fromConnection($conn);

$dd->close();
```

#### Database

```php
$dd->getDatabaseProperty(int $prop)                       : string
$dd->setDatabaseProperty(int $prop, string $value)        : void
```

#### Tables

```php
$dd->addTable(string $alias, string $path,
              int $tableType = ADS_ADT, int $charType = ADS_ANSI,
              string $indexPath = '', string $comment = '')  : void
$dd->removeTable(string $alias, bool $deleteFiles = false)  : void
$dd->getTableProperty(string $table, int $prop)             : string
$dd->setTableProperty(string $table, int $prop, string $val): void
```

#### Fields

```php
$dd->getFieldProperty(string $table, string $field, int $prop)             : string
$dd->setFieldProperty(string $table, string $field, int $prop, string $val): void
```

#### Indexes

```php
$dd->addIndexFile(string $table, string $indexPath, string $comment = '')  : void
$dd->removeIndexFile(string $table, string $indexPath, bool $del = false)  : void
$dd->getIndexProperty(string $table, string $index, int $prop)             : string
$dd->setIndexProperty(string $table, string $index, int $prop, string $val): void
```

#### Users

```php
$dd->createUser(string $user, string $password = '',
                string $group = '', string $desc = '')          : void
$dd->deleteUser(string $user)                                   : void
$dd->getUserProperty(string $user, int $prop)                   : string
$dd->setUserProperty(string $user, int $prop, string $val)      : void
$dd->addUserToGroup(string $user, string $group)                : void
$dd->removeUserFromGroup(string $user, string $group)           : void
$dd->getUserTableRights(string $table, string $user)            : int   // bitmask
$dd->setUserTableRights(string $table, string $user, int $rights): void  // direct set (no revoke needed)
```

#### Views

```php
$dd->createView(string $name, string $sql, string $comment = '') : void
$dd->dropView(string $name)                                      : void
$dd->getViewProperty(string $view, int $prop)                    : string
$dd->setViewProperty(string $view, int $prop, string $val)       : void
```

#### Stored Procedures

```php
// OpenADS: AdsDDCreateProcedure — 5 args (no invokeOption, no comment)
$dd->createProcedure(string $name, string $container, string $procedure,
                     string $input = '', string $output = '')   : void
$dd->dropProcedure(string $name)                                : void
$dd->getProcProperty(string $name, int $prop)                   : string
$dd->setProcProperty(string $name, int $prop, string $val)      : void
```

#### Triggers

OpenADS `createTrigger` has a simpler signature than SAP ACE — only 3 args are
required; container, procedure, and priority are optional.

```php
// 3 required; container, procedure, priority optional
$dd->createTrigger(string $name, string $table, int $type,
                   string $container = '', string $procedure = '',
                   int $priority = 1)               : void
$dd->dropTrigger(string $name)                      : void
$dd->getTriggerProperty(string $name, int $prop)    : string
$dd->setTriggerProperty(string $name, int $prop, string $val): void  // supported (unlike SAP ACE)
```

#### Referential Integrity

```php
$dd->createRefIntegrity(string $name, string $failTable,
                        string $parent, string $parentTag,
                        string $child,  string $childTag,
                        int $updateRule = 0, int $deleteRule = 0): void
$dd->removeRefIntegrity(string $name)                             : void
```

#### Links (cross-dictionary)

```php
$dd->createLink(string $alias, string $path,
                string $user = '', string $password = '') : void
$dd->dropLink(string $alias)                             : void
$dd->modifyLink(string $alias, string $path = '',
                string $user = '', string $password = '') : void
```

---

### AdsException

```php
try {
    $conn = AdsConnection::connect(['path' => '\\\\srv\\data\\missing.add']);
} catch (AdsException $e) {
    echo $e->getMessage();  // human-readable ACE message
    echo $e->getCode();     // native ACE error code
}
```

---

## AdsDictionary differences vs php_advantage

`php_openads` and `php_advantage` expose the same PHP-level method names, but the
underlying ACE SDK functions differ between OpenADS and SAP ACE. This table shows
only the points where the two extensions diverge internally:

| PHP method | OpenADS ACE function | SAP ACE function |
|---|---|---|
| `createView` | `AdsDDCreateView` | `AdsDDAddView` |
| `dropView` | `AdsDDDropView` | `AdsDDRemoveView` |
| `createProcedure` | `AdsDDCreateProcedure` (5 args, no comment) | `AdsDDAddProcedure` (8 args, invokeOption + comment) |
| `dropProcedure` | `AdsDDDropProcedure` | `AdsDDRemoveProcedure` |
| `getProcProperty` | `AdsDDGetProcProperty` | `AdsDDGetProcedureProperty` |
| `setProcProperty` | `AdsDDSetProcProperty` | `AdsDDSetProcedureProperty` |
| `createTrigger` | `AdsDDCreateTrigger` — **3 required** + 3 optional | `AdsDDCreateTrigger` — **7 required** + 3 optional |
| `dropTrigger` | `AdsDDDropTrigger` | `AdsDDRemoveTrigger` |
| `setTriggerProperty` | ✅ `AdsDDSetTriggerProperty` — works | ❌ throws "not supported by SAP ACE" |
| `getUserTableRights` | `AdsDDGetUserTableRights` (direct) | `AdsDDGetPermissions` (with object-type constant) |
| `setUserTableRights` | `AdsDDSetUserTableRights` (direct) | `AdsDDRevokePermission` + `AdsDDGrantPermission` |
| `setTableProperty` | `AdsDDSetTableProperty` (5 args) | `AdsDDSetTableProperty` (7 args, validateOption=0, failTable=NULL) |
| `setFieldProperty` | `AdsDDSetFieldProperty` (6 args) | `AdsDDSetFieldProperty` (8 args, validateOption=0, failTable=NULL) |

---

## Registered constants

| Category | Constants |
|---|---|
| Server type | `ADS_LOCAL_SERVER`, `ADS_REMOTE_SERVER` |
| Table type | `ADS_NTX`, `ADS_CDX`, `ADS_ADT`, `ADS_VFP` |
| Character set | `ADS_ANSI`, `ADS_OEM` |
| Open mode | `ADS_SHARED`, `ADS_EXCLUSIVE` |
| Locking | `ADS_COMPATIBLE_LOCKING`, `ADS_PROPRIETARY_LOCKING` |
| Rights | `ADS_CHECKRIGHTS`, `ADS_IGNORERIGHTS` |
| Filters | `ADS_RESPECTFILTERS`, `ADS_IGNOREFILTERS` |
| Field types | `ADS_LOGICAL`, `ADS_NUMERIC`, `ADS_DATE`, `ADS_STRING`, `ADS_MEMO`, `ADS_BINARY`, `ADS_IMAGE`, `ADS_VARCHAR`, `ADS_DOUBLE`, `ADS_INTEGER`, `ADS_SHORTINT`, `ADS_TIME`, `ADS_TIMESTAMP`, `ADS_AUTOINC`, `ADS_RAW`, `ADS_CURDOUBLE`, `ADS_MONEY`, `ADS_NCHAR`, `ADS_NMEMO` |

---

## Examples

### Connect and query

```php
<?php
$conn = AdsConnection::connect([
    'path'       => '\\\\192.168.0.10:6262\\share\\mydb.add',
    'user'       => 'admin',
    'password'   => 'secret',
    'serverType' => ADS_REMOTE_SERVER,
]);

$stmt = $conn->query(
    "SELECT TOP 10 CustomerID, Name, Balance FROM customers ORDER BY Balance DESC"
);

echo $stmt->columnCount() . " columns, " . $stmt->rowCount() . " rows\n";

while (($row = $stmt->fetchAssoc()) !== false) {
    printf("%-6d  %-30s  %10.2f\n",
           $row['CustomerID'], trim($row['Name']), $row['Balance']);
}

$stmt->close();
$conn->close();
```

### Transaction with rollback on error

```php
<?php
$conn = AdsConnection::connect(['path' => '\\\\srv\\data\\mydb.add']);
$tx   = $conn->beginTransaction();

try {
    $conn->execute("UPDATE accounts SET balance = balance - 100 WHERE id = 1");
    $conn->execute("UPDATE accounts SET balance = balance + 100 WHERE id = 2");
    $tx->commit();
} catch (AdsException $e) {
    $tx->rollback();
    throw $e;
}

$conn->close();
```

### Parameterised SELECT

```php
<?php
$conn = AdsConnection::connect(['path' => '\\\\srv\\data\\mydb.add']);

$prep = $conn->prepare(
    "SELECT CustomerID, Name, Balance
     FROM   customers
     WHERE  Status = :status AND Balance > :minbal
     ORDER  BY Balance DESC"
);

$prep->bindString(':status', 'ACTIVE');
$prep->bindDouble(':minbal', 1000.00);

$stmt = $prep->execute();

while (($row = $stmt->fetchAssoc()) !== false) {
    printf("%6d  %-30s  %10.2f\n",
           $row['CustomerID'], trim($row['Name']), $row['Balance']);
}
$stmt->close();
$conn->close();
```

### Parameterised INSERT in a transaction

```php
<?php
$conn = AdsConnection::connect(['path' => '\\\\srv\\data\\mydb.add']);
$tx   = $conn->beginTransaction();

$products = [
    ['SKU' => 'AAA-01', 'Description' => 'Alpha Widget', 'Price' => 9.99,  'Stock' => 100],
    ['SKU' => 'BBB-02', 'Description' => 'Beta Widget',  'Price' => 14.99, 'Stock' => 50],
    ['SKU' => 'CCC-03', 'Description' => 'Gamma Widget', 'Price' => 4.99,  'Stock' => 200],
];

try {
    foreach ($products as $p) {
        $prep = $conn->prepare(
            "INSERT INTO products (SKU, Description, Price, Stock)
             VALUES (:sku, :desc, :price, :stock)"
        );
        $prep->bindString('sku',   $p['SKU']);
        $prep->bindString('desc',  $p['Description']);
        $prep->bindDouble('price', $p['Price']);
        $prep->bindInt   ('stock', $p['Stock']);
        $prep->execute();

        // Re-prepare after each execute() — handle transfers to AdsStatement
        $prep = $conn->prepare(
            "INSERT INTO products (SKU, Description, Price, Stock)
             VALUES (:sku, :desc, :price, :stock)"
        );
    }
    $tx->commit();
    echo count($products) . " products inserted.\n";
} catch (AdsException $e) {
    $tx->rollback();
    echo "Batch failed: " . $e->getMessage() . "\n";
}
$conn->close();
```

### Auto-bind with mixed types and NULL

```php
<?php
$conn = AdsConnection::connect(['path' => '\\\\srv\\data\\mydb.add']);

$prep = $conn->prepare(
    "UPDATE customers
     SET    Phone = :phone, Fax = :fax, CreditLimit = :limit, Active = :active
     WHERE  CustomerID = :id"
);

$prep->bind('phone',  '+1-555-0100');   // string  → AdsSetString
$prep->bind('fax',    null);            // null    → AdsSetNull
$prep->bind('limit',  5000.00);         // float   → AdsSetDouble
$prep->bind('active', true);            // bool    → AdsSetLogical
$prep->bind('id',     42);              // int     → AdsSetLong

$prep->execute();
echo "Customer updated.\n";
$conn->close();
```

### Direct table navigation

```php
<?php
$conn = AdsConnection::connect(['path' => '\\\\srv\\data\\mydb.add']);
$tbl  = AdsTable::open($conn, 'C:\\data\\inventory.adt', ADS_ADT,
                       ADS_COMPATIBLE_LOCKING, ADS_ANSI, ADS_SHARED);

// Scan all records
$tbl->gotoTop();
while (!$tbl->atEOF()) {
    $row = $tbl->getRecord();
    printf("%-12s  %-30s  qty %4d\n",
           $row['SKU'], trim($row['Description']), $row['Stock']);
    $tbl->skip();
}
$tbl->close();
$conn->close();
```

### Append a new record

```php
<?php
$conn = AdsConnection::connect(['path' => '\\\\srv\\data\\mydb.add']);
$tbl  = AdsTable::open($conn, 'C:\\data\\inventory.adt', ADS_ADT,
                       ADS_COMPATIBLE_LOCKING, ADS_ANSI, ADS_EXCLUSIVE);

$tbl->appendRecord();
$tbl->setString ('SKU',         'WIDGET-99');
$tbl->setString ('Description', 'Deluxe Widget');
$tbl->setDouble ('Price',       29.99);
$tbl->setLong   ('Stock',       50);
$tbl->setLogical('Active',      true);
$tbl->writeRecord();

$tbl->close();
$conn->close();
```

### Data dictionary — configure a new database

```php
<?php
$dd = AdsDictionary::open('\\\\srv\\data\\mydb.add', 'admin', 'secret');

// Register an existing physical table
$dd->addTable('invoices', 'C:\\data\\invoices.adt', ADS_ADT, ADS_ANSI,
              'C:\\data\\invoices.cdx', 'Customer invoices');

// Set a description on a field
$dd->setFieldProperty('invoices', 'amount', ADS_DD_FIELD_DESCRIPTION, 'Invoice total (pre-tax)');

// Create a view
$dd->createView('open_invoices',
    "SELECT id, customer_id, amount FROM invoices WHERE paid = FALSE",
    'Unpaid invoices');

// Register a stored procedure
// OpenADS createProcedure: name, container, procedure, [input, output]
$dd->createProcedure('sp_close_invoice', 'procs.dll', 'CloseInvoice',
    '@invoice_id INTEGER', '');

// Create a trigger (OpenADS: 3 required args — name, table, type)
$dd->createTrigger('trg_audit_insert', 'invoices', ADS_TRIGEVENT_AFTER,
    'audit.dll', 'AuditInsert', 1);

// setTriggerProperty is supported on OpenADS (unlike SAP ACE)
$dd->setTriggerProperty('trg_audit_insert', ADS_DD_TRIGGER_DESCRIPTION, 'Audit new invoices');

// Create a user and grant rights
$dd->createUser('reports', 'rpass', 'readers', 'Read-only reporting account');
$dd->setUserTableRights('invoices', 'reports', ADS_READ_RIGHT);  // direct set — no revoke needed

// Referential integrity: invoices.customer_id → customers.id
$dd->createRefIntegrity('ri_inv_cust', 'ri_errors',
    'customers', 'cust_pk', 'invoices', 'inv_fk',
    ADS_RI_RESTRICT, ADS_RI_RESTRICT);

$dd->close();
```

### Error handling

```php
<?php
try {
    $conn = AdsConnection::connect([
        'path'     => '\\\\missing_server\\data\\mydb.add',
        'user'     => 'admin',
        'password' => 'secret',
    ]);
} catch (AdsException $e) {
    printf("ACE error %d: %s\n", $e->getCode(), $e->getMessage());
}

// Field-level errors
$conn = AdsConnection::connect(['path' => '...']);
$stmt = $conn->query("SELECT * FROM products");
$row  = $stmt->fetchAssoc();
try {
    // Wrong field name — AdsException thrown inside the extension
    $stmt2 = $conn->query("SELECT nonexistent_column FROM products");
} catch (AdsException $e) {
    echo "Query error: " . $e->getMessage() . "\n";
}
$stmt->close();
$conn->close();
```

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `PHP Warning: Unable to load dynamic library 'php_openads'` | DLL not in `ext/` or ACE DLL missing | Copy `php_openads.dll` to `C:\php\ext\`; copy `openace64.dll` to `C:\php\` |
| Extension loads but `connect()` throws `[5131]` | Wrong path / server not running | Verify OpenADS server is running; check UNC path and port |
| Build: `C1083: Cannot open include file: 'php.h'` | `PHP_DEVPACK` path wrong in `Makefile.win` | Update `PHP_DEVPACK` to your php-devpack directory |
| Build: `C1083: Cannot open include file: 'ace.h'` | `ACE_INCLUDE` path wrong in `Makefile.win` | Update `ACE_INCLUDE` to `F:\OpenADS\include` |
| Extension crashes (0xC0000005) | ZTS TSRM cache not initialised | Ensure `ZEND_TSRMLS_CACHE_EXTERN()` is in `php_ads.h` |
| `AdsException [5107]` on table open | Table file not found at given path | Use absolute paths; verify the `.adt` file exists |

---

## See also

- [OpenADS repository](https://github.com/FiveTechSoft/OpenADS)
- [`bindings/php`](../php/README.md) — FFI-based companion binding (Composer, cross-platform)
- [`php_advantage`](https://github.com/reinaldocrespo/php_advantage) — identical API for SAP Advantage Database Server
- [ACE API Reference](https://devzone.advantagedatabase.com/dz/content.aspx?Key=20&Release=19&Product=5)
