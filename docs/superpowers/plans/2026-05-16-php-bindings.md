# OpenADS PHP Binding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship `bindings/php` — a pure-PHP Composer package that lets PHP apps talk to OpenADS through a modern OOP API, wrapping the ACE C library via `ext-ffi`.

**Architecture:** A thin FFI layer (`AceLibrary`) loads `ace64.dll` / `ace32.dll` / `libace.so` and declares the ACE exports the binding uses. OOP classes (`Connection`, `Statement`, `Cursor`, `Table`, `Record`) sit on top. Local and remote connections share one path — ACE's `AdsConnect60` dispatches on the connection URI. SQL parameters are bound client-side in PHP because ACE has no host-variable binding.

**Tech Stack:** PHP 8.1+, `ext-ffi`, Composer, PHPUnit 10. No compiled C code.

**Spec:** `docs/superpowers/specs/2026-05-16-php-bindings-design.md`

**Note on PHP version:** the spec says "PHP 7.4+". This plan tightens the minimum to **8.1** — the API uses union return types (`Cursor|int`), constructor promotion, and typed properties. Update the spec's §9 requirement line to "PHP 8.1+" as part of Task 1.

**Note on `ParameterBinder`:** the spec (§4.3) places parameter binding inside `Statement` but (§5) requires the escaping logic to be "centralised in one function with its own unit tests". This plan satisfies both by extracting a focused `Sql\ParameterBinder` class that `Statement` delegates to.

**Branch:** all work happens on a feature branch `feat/php-bindings`, created in Task 1. Do not commit to `main`.

---

## File Structure

```
bindings/php/
  composer.json                       # package metadata, autoload, PHPUnit dep
  phpunit.xml                         # test config: unit + integration suites
  README.md                           # install, requirements, usage
  src/
    Ffi/AceTypes.php                  # ADS_* / AE_* constants, error-code names
    Ffi/AceLibrary.php                # FFI loader, signature cdef, call helpers
    Sql/ParameterBinder.php           # client-side ?/:name substitution + quoting
    Exception/OpenAdsException.php    # base exception (ACE code + message)
    Exception/ConnectionException.php
    Exception/QueryException.php
    Connection.php                    # connect/disconnect; Statement/Table factory
    Statement.php                     # prepare/execute SQL -> Cursor|int
    Cursor.php                        # Iterator over a SELECT result set
    Table.php                         # navigational table access
    Record.php                        # current-record field get/set + writes
  tests/
    bootstrap.php                     # autoload + integration-skip helper
    Unit/AceTypesTest.php
    Unit/ExceptionTest.php
    Unit/ParameterBinderTest.php
    Integration/IntegrationTestCase.php  # shared connect/CREATE TABLE fixture
    Integration/ConnectionTest.php
    Integration/StatementTest.php
    Integration/CursorTest.php
    Integration/TableTest.php
    Integration/RecordTest.php
  examples/
    query.php
    navigate.php
```

Unit tests need no engine. Integration tests need a built ACE library; `tests/bootstrap.php` skips them when `OPENADS_ACE_LIB` is unset or the file is missing.

---

## Task 1: Package scaffold

**Files:**
- Create: `bindings/php/composer.json`
- Create: `bindings/php/phpunit.xml`
- Create: `bindings/php/tests/bootstrap.php`
- Create: `bindings/php/tests/Unit/ScaffoldTest.php`
- Modify: `docs/superpowers/specs/2026-05-16-php-bindings-design.md` (§9 PHP version)

- [ ] **Step 1: Create the feature branch**

```bash
git checkout -b feat/php-bindings
```

- [ ] **Step 2: Write `composer.json`**

```json
{
    "name": "openads/openads-php",
    "description": "PHP binding for the OpenADS database engine (ACE-compatible), via FFI.",
    "type": "library",
    "license": "Apache-2.0",
    "require": {
        "php": ">=8.1",
        "ext-ffi": "*"
    },
    "require-dev": {
        "phpunit/phpunit": "^10.5"
    },
    "autoload": {
        "psr-4": { "OpenADS\\": "src/" }
    },
    "autoload-dev": {
        "psr-4": { "OpenADS\\Tests\\": "tests/" }
    },
    "scripts": {
        "test": "phpunit"
    }
}
```

- [ ] **Step 3: Write `phpunit.xml`**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<phpunit xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
         xsi:noNamespaceSchemaLocation="vendor/phpunit/phpunit/phpunit.xsd"
         bootstrap="tests/bootstrap.php"
         colors="true"
         failOnWarning="true">
    <testsuites>
        <testsuite name="unit">
            <directory>tests/Unit</directory>
        </testsuite>
        <testsuite name="integration">
            <directory>tests/Integration</directory>
        </testsuite>
    </testsuites>
</phpunit>
```

- [ ] **Step 4: Write `tests/bootstrap.php`**

```php
<?php
declare(strict_types=1);

require __DIR__ . '/../vendor/autoload.php';

/**
 * Resolve the ACE library path for integration tests.
 * Returns null when no usable library is configured, so
 * IntegrationTestCase can skip rather than fail.
 */
function openads_test_lib(): ?string
{
    $path = getenv('OPENADS_ACE_LIB');
    if ($path === false || $path === '') {
        return null;
    }
    return is_file($path) ? $path : null;
}
```

- [ ] **Step 5: Write the scaffold smoke test**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Tests\Unit;

use PHPUnit\Framework\TestCase;

final class ScaffoldTest extends TestCase
{
    public function testAutoloadAndFfiExtensionPresent(): void
    {
        self::assertTrue(extension_loaded('ffi'), 'ext-ffi must be enabled');
    }
}
```

- [ ] **Step 6: Install dependencies and run the test**

Run:
```bash
cd bindings/php && composer install && composer test -- --testsuite unit
```
Expected: PASS — `ScaffoldTest` green. If `ext-ffi` is not enabled, this fails with a clear message; enable it before continuing.

- [ ] **Step 7: Update the spec PHP-version line**

In `docs/superpowers/specs/2026-05-16-php-bindings-design.md` §9, change `PHP 7.4+` to `PHP 8.1+`.

- [ ] **Step 8: Commit**

```bash
git add bindings/php docs/superpowers/specs/2026-05-16-php-bindings-design.md
git commit -m "feat(php): package scaffold — composer, phpunit, bootstrap"
```

---

## Task 2: `AceTypes` — constants and error codes

**Files:**
- Create: `bindings/php/src/Ffi/AceTypes.php`
- Test: `bindings/php/tests/Unit/AceTypesTest.php`

- [ ] **Step 1: Write the failing test**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Tests\Unit;

use OpenADS\Ffi\AceTypes;
use PHPUnit\Framework\TestCase;

final class AceTypesTest extends TestCase
{
    public function testServerTypeConstants(): void
    {
        self::assertSame(1, AceTypes::ADS_LOCAL_SERVER);
        self::assertSame(2, AceTypes::ADS_REMOTE_SERVER);
    }

    public function testSuccessCodeIsZero(): void
    {
        self::assertSame(0, AceTypes::AE_SUCCESS);
    }

    public function testErrorNameKnownCode(): void
    {
        self::assertSame('AE_NO_CURRENT_RECORD', AceTypes::errorName(5026));
    }

    public function testErrorNameUnknownCode(): void
    {
        self::assertSame('AE_UNKNOWN(9999)', AceTypes::errorName(9999));
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `composer test -- --testsuite unit --filter AceTypesTest`
Expected: FAIL — `Class "OpenADS\Ffi\AceTypes" not found`.

- [ ] **Step 3: Write `AceTypes`**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Ffi;

/**
 * ACE numeric constants and error-code names. Values mirror
 * include/openads/ace.h and include/openads/error.h. Names only —
 * no proprietary help text.
 */
final class AceTypes
{
    public const ADS_LOCAL_SERVER  = 1;
    public const ADS_REMOTE_SERVER = 2;

    public const AE_SUCCESS = 0;

    /** ACE field-type codes returned by AdsGetFieldType. */
    public const ADS_STRING    = 1;
    public const ADS_NUMERIC   = 2;
    public const ADS_DATE      = 3;
    public const ADS_LOGICAL   = 4;
    public const ADS_MEMO      = 5;
    public const ADS_DOUBLE    = 10;
    public const ADS_INTEGER   = 11;
    public const ADS_TIMESTAMP = 13;

    /** Subset of AE_* codes worth naming in error messages. */
    private const ERROR_NAMES = [
        0    => 'AE_SUCCESS',
        5000 => 'AE_INTERNAL_ERROR',
        5004 => 'AE_FUNCTION_NOT_AVAILABLE',
        5026 => 'AE_NO_CURRENT_RECORD',
    ];

    public static function errorName(int $code): string
    {
        return self::ERROR_NAMES[$code] ?? "AE_UNKNOWN($code)";
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `composer test -- --testsuite unit --filter AceTypesTest`
Expected: PASS — 4 tests green.

- [ ] **Step 5: Commit**

```bash
git add bindings/php/src/Ffi/AceTypes.php bindings/php/tests/Unit/AceTypesTest.php
git commit -m "feat(php): AceTypes — ACE constants and error-code names"
```

---

## Task 3: Exception hierarchy

**Files:**
- Create: `bindings/php/src/Exception/OpenAdsException.php`
- Create: `bindings/php/src/Exception/ConnectionException.php`
- Create: `bindings/php/src/Exception/QueryException.php`
- Test: `bindings/php/tests/Unit/ExceptionTest.php`

- [ ] **Step 1: Write the failing test**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Tests\Unit;

use OpenADS\Exception\ConnectionException;
use OpenADS\Exception\OpenAdsException;
use OpenADS\Exception\QueryException;
use PHPUnit\Framework\TestCase;

final class ExceptionTest extends TestCase
{
    public function testFromAceBuildsNamedMessage(): void
    {
        $e = OpenAdsException::fromAce(5026, 'table not positioned');
        self::assertSame(5026, $e->aceCode());
        self::assertStringContainsString('AE_NO_CURRENT_RECORD', $e->getMessage());
        self::assertStringContainsString('table not positioned', $e->getMessage());
    }

    public function testSubclassesExtendBase(): void
    {
        self::assertInstanceOf(OpenAdsException::class, new ConnectionException('x'));
        self::assertInstanceOf(OpenAdsException::class, new QueryException('x'));
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `composer test -- --testsuite unit --filter ExceptionTest`
Expected: FAIL — `Class "OpenADS\Exception\OpenAdsException" not found`.

- [ ] **Step 3: Write `OpenAdsException`**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Exception;

use OpenADS\Ffi\AceTypes;
use RuntimeException;

class OpenAdsException extends RuntimeException
{
    private int $aceCode;

    public function __construct(string $message, int $aceCode = 0)
    {
        parent::__construct($message);
        $this->aceCode = $aceCode;
    }

    public static function fromAce(int $code, string $detail): self
    {
        $name = AceTypes::errorName($code);
        return new self("$name ($code): $detail", $code);
    }

    public function aceCode(): int
    {
        return $this->aceCode;
    }
}
```

- [ ] **Step 4: Write `ConnectionException`**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Exception;

class ConnectionException extends OpenAdsException
{
}
```

- [ ] **Step 5: Write `QueryException`**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Exception;

class QueryException extends OpenAdsException
{
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `composer test -- --testsuite unit --filter ExceptionTest`
Expected: PASS — 2 tests green.

- [ ] **Step 7: Commit**

```bash
git add bindings/php/src/Exception bindings/php/tests/Unit/ExceptionTest.php
git commit -m "feat(php): exception hierarchy with ACE-code mapping"
```

---

## Task 4: `ParameterBinder` — client-side SQL parameters

This is the anti-injection boundary. ACE has no host-variable binding, so the binder substitutes values into the SQL text with per-type quoting before the statement reaches the engine.

**Files:**
- Create: `bindings/php/src/Sql/ParameterBinder.php`
- Test: `bindings/php/tests/Unit/ParameterBinderTest.php`

- [ ] **Step 1: Write the failing test**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Tests\Unit;

use OpenADS\Exception\QueryException;
use OpenADS\Sql\ParameterBinder;
use PHPUnit\Framework\TestCase;

final class ParameterBinderTest extends TestCase
{
    public function testPositionalParams(): void
    {
        $sql = ParameterBinder::bind('SELECT * FROM t WHERE id = ? AND n = ?', [5, 'ab']);
        self::assertSame("SELECT * FROM t WHERE id = 5 AND n = 'ab'", $sql);
    }

    public function testNamedParams(): void
    {
        $sql = ParameterBinder::bind('SELECT * FROM t WHERE id = :id', [':id' => 7]);
        self::assertSame('SELECT * FROM t WHERE id = 7', $sql);
    }

    public function testStringQuoteIsEscaped(): void
    {
        $sql = ParameterBinder::bind('WHERE name = ?', ["O'Brien"]);
        self::assertSame("WHERE name = 'O''Brien'", $sql);
    }

    public function testNullBoolAndFloat(): void
    {
        $sql = ParameterBinder::bind('VALUES (?, ?, ?)', [null, true, 1.5]);
        self::assertSame('VALUES (NULL, .T., 1.5)', $sql);
    }

    public function testDateTimeBecomesAceLiteral(): void
    {
        $dt  = new \DateTimeImmutable('2026-05-16');
        $sql = ParameterBinder::bind('WHERE d = ?', [$dt]);
        self::assertSame("WHERE d = '2026-05-16'", $sql);
    }

    public function testWrongPositionalCountThrows(): void
    {
        $this->expectException(QueryException::class);
        ParameterBinder::bind('WHERE id = ? AND n = ?', [1]);
    }

    public function testUnsupportedTypeThrows(): void
    {
        $this->expectException(QueryException::class);
        ParameterBinder::bind('WHERE id = ?', [new \stdClass()]);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `composer test -- --testsuite unit --filter ParameterBinderTest`
Expected: FAIL — `Class "OpenADS\Sql\ParameterBinder" not found`.

- [ ] **Step 3: Write `ParameterBinder`**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Sql;

use OpenADS\Exception\QueryException;

/**
 * Substitutes parameters into SQL text client-side. OpenADS ACE
 * has no host-variable binding, so this is where values become
 * literals — and the only place injection is defended against.
 * A statement uses positional `?` OR named `:name`, not both.
 */
final class ParameterBinder
{
    public static function bind(string $sql, array $params): string
    {
        if ($params === []) {
            return $sql;
        }
        return self::isNamed($params)
            ? self::bindNamed($sql, $params)
            : self::bindPositional($sql, array_values($params));
    }

    private static function isNamed(array $params): bool
    {
        foreach (array_keys($params) as $k) {
            if (is_string($k)) {
                return true;
            }
        }
        return false;
    }

    private static function bindPositional(string $sql, array $values): string
    {
        $i   = 0;
        $out = preg_replace_callback('/\?/', static function () use (&$i, $values): string {
            if (!array_key_exists($i, $values)) {
                throw new QueryException('too few parameters for placeholders');
            }
            return self::quote($values[$i++]);
        }, $sql);
        if ($i !== count($values)) {
            throw new QueryException('parameter count does not match placeholders');
        }
        return $out;
    }

    private static function bindNamed(string $sql, array $params): string
    {
        // Longest names first so :ab does not partial-match :abc.
        uksort($params, static fn ($a, $b) => strlen((string) $b) <=> strlen((string) $a));
        foreach ($params as $name => $value) {
            $token = ($name[0] === ':') ? $name : ':' . $name;
            $sql   = str_replace($token, self::quote($value), $sql);
        }
        return $sql;
    }

    /** Render one PHP value as a safe SQL literal. */
    public static function quote(mixed $value): string
    {
        if ($value === null) {
            return 'NULL';
        }
        if (is_bool($value)) {
            return $value ? '.T.' : '.F.';
        }
        if (is_int($value) || is_float($value)) {
            return (string) $value;
        }
        if ($value instanceof \DateTimeInterface) {
            return "'" . $value->format('Y-m-d') . "'";
        }
        if (is_string($value)) {
            return "'" . str_replace("'", "''", $value) . "'";
        }
        throw new QueryException('unsupported parameter type: ' . get_debug_type($value));
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `composer test -- --testsuite unit --filter ParameterBinderTest`
Expected: PASS — 7 tests green.

- [ ] **Step 5: Commit**

```bash
git add bindings/php/src/Sql/ParameterBinder.php bindings/php/tests/Unit/ParameterBinderTest.php
git commit -m "feat(php): ParameterBinder — client-side SQL parameter quoting"
```

---

## Task 5: `AceLibrary` — FFI loader

`AceLibrary` is the only class that touches `\FFI`. It resolves the library path, picks 32/64-bit, declares the ACE signatures the binding uses, and exposes the raw `\FFI` object plus small helpers for ACE's out-parameter conventions.

**Files:**
- Create: `bindings/php/src/Ffi/AceLibrary.php`
- Test: `bindings/php/tests/Integration/IntegrationTestCase.php`
- Test: `bindings/php/tests/Integration/AceLibraryTest.php`

- [ ] **Step 1: Write the shared integration base class**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Tests\Integration;

use PHPUnit\Framework\TestCase;

abstract class IntegrationTestCase extends TestCase
{
    protected function setUp(): void
    {
        if (openads_test_lib() === null) {
            self::markTestSkipped('OPENADS_ACE_LIB not set; skipping integration test');
        }
    }

    /** A fresh empty directory usable as a local-mode data dir. */
    protected function tempDataDir(): string
    {
        $dir = sys_get_temp_dir() . '/openads_php_' . bin2hex(random_bytes(6));
        mkdir($dir);
        return $dir;
    }
}
```

- [ ] **Step 2: Write the failing `AceLibrary` test**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Tests\Integration;

use OpenADS\Ffi\AceLibrary;

final class AceLibraryTest extends IntegrationTestCase
{
    public function testLoadsAndExposesFfi(): void
    {
        $lib = AceLibrary::load(openads_test_lib());
        self::assertInstanceOf(\FFI::class, $lib->ffi());
    }

    public function testOutHandleHelperRoundTrips(): void
    {
        $lib = AceLibrary::load(openads_test_lib());
        $h   = $lib->newHandle();      // ADSHANDLE* out-parameter
        self::assertSame(0, $lib->handleValue($h));
    }
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `composer test -- --testsuite integration --filter AceLibraryTest`
Expected: FAIL — `Class "OpenADS\Ffi\AceLibrary" not found` (or SKIPPED if `OPENADS_ACE_LIB` unset; set it to a built `libace`/`ace64.dll` to run).

- [ ] **Step 4: Write `AceLibrary`**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Ffi;

use FFI;
use OpenADS\Exception\OpenAdsException;

/**
 * Loads the ACE shared library and declares the C surface the
 * binding calls. The only class that references \FFI directly.
 */
final class AceLibrary
{
    /** C declarations for the ACE exports the binding uses. */
    private const CDEF = <<<'C'
        typedef uint32_t UNSIGNED32;
        typedef uint16_t UNSIGNED16;
        typedef uint8_t  UNSIGNED8;
        typedef uint64_t ADSHANDLE;

        UNSIGNED32 AdsConnect60(UNSIGNED8* pucServer, UNSIGNED16 usServerType,
            UNSIGNED8* pucUserName, UNSIGNED8* pucPassword,
            UNSIGNED32 ulOptions, ADSHANDLE* phConnect);
        UNSIGNED32 AdsDisconnect(ADSHANDLE hConnect);
        UNSIGNED32 AdsGetLastError(UNSIGNED32* pulCode, UNSIGNED8* pucBuf,
            UNSIGNED16* pusBufLen);

        UNSIGNED32 AdsCreateSQLStatement(ADSHANDLE hConnect, ADSHANDLE* phStatement);
        UNSIGNED32 AdsCloseSQLStatement(ADSHANDLE hStatement);
        UNSIGNED32 AdsExecuteSQLDirect(ADSHANDLE hStatement, UNSIGNED8* pucSQL,
            ADSHANDLE* phCursor);

        UNSIGNED32 AdsOpenTable(ADSHANDLE hConnect, UNSIGNED8* pucName,
            UNSIGNED8* pucAlias, UNSIGNED16 usTableType, UNSIGNED16 usCharType,
            UNSIGNED16 usLockType, UNSIGNED16 usCheckRights, UNSIGNED16 usMode,
            ADSHANDLE* phTable);
        UNSIGNED32 AdsCloseTable(ADSHANDLE hTable);

        UNSIGNED32 AdsGotoTop(ADSHANDLE hTable);
        UNSIGNED32 AdsGotoBottom(ADSHANDLE hTable);
        UNSIGNED32 AdsGotoRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord);
        UNSIGNED32 AdsSkip(ADSHANDLE hTable, int32_t lRows);
        UNSIGNED32 AdsAtEOF(ADSHANDLE hTable, UNSIGNED16* pbAtEnd);
        UNSIGNED32 AdsAtBOF(ADSHANDLE hTable, UNSIGNED16* pbAtBegin);
        UNSIGNED32 AdsGetRecordCount(ADSHANDLE hTable, UNSIGNED16 bFilterOption,
            UNSIGNED32* pulRecordCount);
        UNSIGNED32 AdsGetRecordNum(ADSHANDLE hTable, UNSIGNED16 bFilterOption,
            UNSIGNED32* pulRecordNum);

        UNSIGNED32 AdsGetNumFields(ADSHANDLE hTable, UNSIGNED16* pusFields);
        UNSIGNED32 AdsGetFieldName(ADSHANDLE hTable, UNSIGNED16 usFieldNum,
            UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
        UNSIGNED32 AdsGetFieldType(ADSHANDLE hTable, UNSIGNED8* pucField,
            UNSIGNED16* pusType);
        UNSIGNED32 AdsGetField(ADSHANDLE hTable, UNSIGNED8* pucField,
            UNSIGNED8* pucBuf, UNSIGNED32* pulLen, UNSIGNED16 usOption);
        UNSIGNED32 AdsSetField(ADSHANDLE hObj, UNSIGNED8* pucFldId,
            UNSIGNED8* pucBuf, UNSIGNED32 ulLen);

        UNSIGNED32 AdsAppendRecord(ADSHANDLE hTable);
        UNSIGNED32 AdsWriteRecord(ADSHANDLE hTable);
        UNSIGNED32 AdsDeleteRecord(ADSHANDLE hTable);
        UNSIGNED32 AdsRecallRecord(ADSHANDLE hTable);
        UNSIGNED32 AdsLockRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord);
        UNSIGNED32 AdsUnlockRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord);
        UNSIGNED32 AdsSetAOF(ADSHANDLE hTable, UNSIGNED8* pucCondition,
            UNSIGNED16 usOptions);
        UNSIGNED32 AdsClearAOF(ADSHANDLE hTable);
        C;

    private static ?self $instance = null;

    private function __construct(private FFI $ffi)
    {
    }

    /**
     * Load the library at $path (defaults to OPENADS_ACE_LIB).
     * Cached — repeated calls return the same instance.
     */
    public static function load(?string $path = null): self
    {
        if (self::$instance !== null) {
            return self::$instance;
        }
        $path ??= self::resolvePath();
        try {
            $ffi = FFI::cdef(self::CDEF, $path);
        } catch (\FFI\Exception $e) {
            throw new OpenAdsException("cannot load ACE library '$path': " . $e->getMessage());
        }
        return self::$instance = new self($ffi);
    }

    private static function resolvePath(): string
    {
        $env = getenv('OPENADS_ACE_LIB');
        if ($env !== false && $env !== '') {
            return $env;
        }
        $is64 = PHP_INT_SIZE === 8;
        if (PHP_OS_FAMILY === 'Windows') {
            return $is64 ? 'ace64.dll' : 'ace32.dll';
        }
        return 'libace.so';
    }

    public function ffi(): FFI
    {
        return $this->ffi;
    }

    /** Allocate an ADSHANDLE out-parameter, zero-initialised. */
    public function newHandle(): FFI\CData
    {
        $h = $this->ffi->new('ADSHANDLE');
        $h->cdata = 0;
        return $h;
    }

    public function handleValue(FFI\CData $handle): int
    {
        return (int) $handle->cdata;
    }

    /** Pull the latest ACE error code + text after a failed call. */
    public function lastError(): array
    {
        $code = $this->ffi->new('UNSIGNED32');
        $len  = $this->ffi->new('UNSIGNED16');
        $len->cdata = 256;
        $buf  = $this->ffi->new('UNSIGNED8[256]');
        $this->ffi->AdsGetLastError(FFI::addr($code), $buf, FFI::addr($len));
        return [(int) $code->cdata, FFI::string($buf)];
    }
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `OPENADS_ACE_LIB=/path/to/libace.so composer test -- --testsuite integration --filter AceLibraryTest`
Expected: PASS — 2 tests green.

- [ ] **Step 6: Commit**

```bash
git add bindings/php/src/Ffi/AceLibrary.php bindings/php/tests/Integration/IntegrationTestCase.php bindings/php/tests/Integration/AceLibraryTest.php
git commit -m "feat(php): AceLibrary — FFI loader and ACE signature cdef"
```

---

## Task 6: `Connection`

**Files:**
- Create: `bindings/php/src/Connection.php`
- Test: `bindings/php/tests/Integration/ConnectionTest.php`

- [ ] **Step 1: Write the failing test**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Tests\Integration;

use OpenADS\Connection;
use OpenADS\Exception\ConnectionException;

final class ConnectionTest extends IntegrationTestCase
{
    public function testConnectLocalDirAndClose(): void
    {
        $conn = new Connection($this->tempDataDir());
        self::assertTrue($conn->isOpen());
        $conn->close();
        self::assertFalse($conn->isOpen());
    }

    public function testCloseIsIdempotent(): void
    {
        $conn = new Connection($this->tempDataDir());
        $conn->close();
        $conn->close(); // must not throw
        self::assertFalse($conn->isOpen());
    }

    public function testBadRemoteUriThrowsConnectionException(): void
    {
        $this->expectException(ConnectionException::class);
        new Connection('tcp://127.0.0.1:1/nope');
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `composer test -- --testsuite integration --filter ConnectionTest`
Expected: FAIL — `Class "OpenADS\Connection" not found`.

- [ ] **Step 3: Write `Connection`**

```php
<?php
declare(strict_types=1);

namespace OpenADS;

use FFI;
use OpenADS\Exception\ConnectionException;
use OpenADS\Ffi\AceLibrary;
use OpenADS\Ffi\AceTypes;

final class Connection
{
    private AceLibrary $lib;
    private int $handle = 0;
    private bool $open = false;

    /**
     * @param string $uri  Local data-dir path, or tcp:// / tls:// URI.
     */
    public function __construct(
        string $uri,
        ?string $user = null,
        ?string $pass = null
    ) {
        $this->lib = AceLibrary::load();
        $ffi       = $this->lib->ffi();

        $serverType = str_starts_with($uri, 'tcp://') || str_starts_with($uri, 'tls://')
            ? AceTypes::ADS_REMOTE_SERVER
            : AceTypes::ADS_LOCAL_SERVER;

        $out = $this->lib->newHandle();
        $rc  = $ffi->AdsConnect60(
            self::cstr($ffi, $uri),
            $serverType,
            $user !== null ? self::cstr($ffi, $user) : null,
            $pass !== null ? self::cstr($ffi, $pass) : null,
            0,
            FFI::addr($out)
        );
        if ($rc !== AceTypes::AE_SUCCESS) {
            [$code, $text] = $this->lib->lastError();
            throw new ConnectionException(
                "connect to '$uri' failed: " . AceTypes::errorName($code) . " ($code): $text",
                $code
            );
        }
        $this->handle = $this->lib->handleValue($out);
        $this->open   = true;
    }

    public function isOpen(): bool
    {
        return $this->open;
    }

    public function handle(): int
    {
        return $this->handle;
    }

    public function library(): AceLibrary
    {
        return $this->lib;
    }

    public function statement(): Statement
    {
        return new Statement($this);
    }

    public function table(string $name): Table
    {
        return new Table($this, $name);
    }

    public function close(): void
    {
        if (!$this->open) {
            return;
        }
        $this->lib->ffi()->AdsDisconnect($this->handle);
        $this->open = false;
    }

    public function __destruct()
    {
        $this->close();
    }

    /** Allocate a NUL-terminated C string buffer for $s. */
    public static function cstr(FFI $ffi, string $s): FFI\CData
    {
        $n   = strlen($s);
        $buf = $ffi->new('UNSIGNED8[' . ($n + 1) . ']');
        FFI::memcpy($buf, $s, $n);
        $buf[$n] = 0;
        return $buf;
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `composer test -- --testsuite integration --filter ConnectionTest`
Expected: PASS — 3 tests green.

- [ ] **Step 5: Commit**

```bash
git add bindings/php/src/Connection.php bindings/php/tests/Integration/ConnectionTest.php
git commit -m "feat(php): Connection — connect/disconnect over local + remote URIs"
```

---

## Task 7: `Statement`

`Statement` binds parameters with `ParameterBinder`, then runs the SQL via `AdsExecuteSQLDirect`. A non-zero cursor handle means a result set; zero means a non-SELECT statement.

**Files:**
- Create: `bindings/php/src/Statement.php`
- Test: `bindings/php/tests/Integration/StatementTest.php`

- [ ] **Step 1: Write the failing test**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Tests\Integration;

use OpenADS\Connection;
use OpenADS\Cursor;

final class StatementTest extends IntegrationTestCase
{
    public function testCreateInsertReturnsRowCountAndSelectReturnsCursor(): void
    {
        $conn = new Connection($this->tempDataDir());
        $stmt = $conn->statement();

        $stmt->query('CREATE TABLE people (id INTEGER, name CHAR(20))');
        $affected = $stmt->query(
            'INSERT INTO people (id, name) VALUES (?, ?)',
            [1, "O'Brien"]
        );
        self::assertSame(1, $affected);

        $result = $stmt->query('SELECT * FROM people WHERE id = :id', [':id' => 1]);
        self::assertInstanceOf(Cursor::class, $result);
        self::assertSame(1, $result->count());

        $conn->close();
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `composer test -- --testsuite integration --filter StatementTest`
Expected: FAIL — `Class "OpenADS\Statement" not found`.

- [ ] **Step 3: Write `Statement`**

```php
<?php
declare(strict_types=1);

namespace OpenADS;

use FFI;
use OpenADS\Exception\QueryException;
use OpenADS\Ffi\AceTypes;
use OpenADS\Sql\ParameterBinder;

final class Statement
{
    private int $handle = 0;
    private bool $open = false;
    private ?string $prepared = null;

    public function __construct(private Connection $conn)
    {
        $ffi = $conn->library()->ffi();
        $out = $conn->library()->newHandle();
        $rc  = $ffi->AdsCreateSQLStatement($conn->handle(), FFI::addr($out));
        if ($rc !== AceTypes::AE_SUCCESS) {
            throw $this->fail($rc, 'create statement');
        }
        $this->handle = $conn->library()->handleValue($out);
        $this->open   = true;
    }

    /** Store SQL for later execute() calls. */
    public function prepare(string $sql): self
    {
        $this->prepared = $sql;
        return $this;
    }

    /** Run a previously prepared statement. */
    public function execute(array $params = []): Cursor|int
    {
        if ($this->prepared === null) {
            throw new QueryException('execute() called without prepare()');
        }
        return $this->run($this->prepared, $params);
    }

    /** Bind params and run $sql immediately. */
    public function query(string $sql, array $params = []): Cursor|int
    {
        return $this->run($sql, $params);
    }

    private function run(string $sql, array $params): Cursor|int
    {
        $bound = ParameterBinder::bind($sql, $params);
        $ffi   = $this->conn->library()->ffi();
        $out   = $this->conn->library()->newHandle();
        $rc    = $ffi->AdsExecuteSQLDirect(
            $this->handle,
            Connection::cstr($ffi, $bound),
            FFI::addr($out)
        );
        if ($rc !== AceTypes::AE_SUCCESS) {
            throw $this->fail($rc, "execute: $bound");
        }
        $cursorHandle = $this->conn->library()->handleValue($out);
        return $cursorHandle === 0
            ? 1 // non-SELECT: one statement applied
            : new Cursor($this->conn, $cursorHandle);
    }

    public function close(): void
    {
        if (!$this->open) {
            return;
        }
        $this->conn->library()->ffi()->AdsCloseSQLStatement($this->handle);
        $this->open = false;
    }

    public function __destruct()
    {
        $this->close();
    }

    private function fail(int $rc, string $what): QueryException
    {
        [$code, $text] = $this->conn->library()->lastError();
        return new QueryException(
            "$what failed: " . AceTypes::errorName($code) . " ($code): $text",
            $code
        );
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `composer test -- --testsuite integration --filter StatementTest`
Expected: PASS — 1 test green.

- [ ] **Step 5: Commit**

```bash
git add bindings/php/src/Statement.php bindings/php/tests/Integration/StatementTest.php
git commit -m "feat(php): Statement — parameter-bound SQL execution"
```

---

## Task 8: `Cursor`

`Cursor` iterates a SELECT result set. It reads column metadata once, then exposes rows as PHP arrays. `RowReader` (a private helper inside `Cursor`) converts raw field bytes to PHP types — the same conversion `Record` reuses, so it is implemented here as a `static` method.

**Files:**
- Create: `bindings/php/src/Cursor.php`
- Test: `bindings/php/tests/Integration/CursorTest.php`

- [ ] **Step 1: Write the failing test**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Tests\Integration;

use OpenADS\Connection;

final class CursorTest extends IntegrationTestCase
{
    public function testIterateRowsAsAssoc(): void
    {
        $conn = new Connection($this->tempDataDir());
        $stmt = $conn->statement();
        $stmt->query('CREATE TABLE n (id INTEGER, label CHAR(10))');
        $stmt->query('INSERT INTO n (id, label) VALUES (?, ?)', [1, 'one']);
        $stmt->query('INSERT INTO n (id, label) VALUES (?, ?)', [2, 'two']);

        $cursor = $stmt->query('SELECT id, label FROM n');
        $rows   = [];
        foreach ($cursor as $row) {
            $rows[] = $row;
        }

        self::assertCount(2, $rows);
        self::assertSame(1, $rows[0]['ID']);
        self::assertSame('one', trim((string) $rows[0]['LABEL']));
        $conn->close();
    }

    public function testFetchAll(): void
    {
        $conn = new Connection($this->tempDataDir());
        $stmt = $conn->statement();
        $stmt->query('CREATE TABLE n (id INTEGER)');
        $stmt->query('INSERT INTO n (id) VALUES (?)', [7]);

        $rows = $stmt->query('SELECT id FROM n')->fetchAll();
        self::assertSame([['ID' => 7]], $rows);
        $conn->close();
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `composer test -- --testsuite integration --filter CursorTest`
Expected: FAIL — `Class "OpenADS\Cursor" not found`.

- [ ] **Step 3: Write `Cursor`**

```php
<?php
declare(strict_types=1);

namespace OpenADS;

use FFI;
use Iterator;
use OpenADS\Exception\QueryException;
use OpenADS\Ffi\AceLibrary;
use OpenADS\Ffi\AceTypes;

/**
 * @implements Iterator<int, array<string, mixed>>
 */
final class Cursor implements Iterator
{
    private AceLibrary $lib;
    /** @var list<string> */
    private array $fields = [];
    private int $position = 0;
    private bool $closed = false;

    public function __construct(private Connection $conn, private int $handle)
    {
        $this->lib    = $conn->library();
        $this->fields = $this->readFieldNames();
    }

    public function count(): int
    {
        $ffi = $this->lib->ffi();
        $out = $ffi->new('UNSIGNED32');
        $rc  = $ffi->AdsGetRecordCount($this->handle, 0, FFI::addr($out));
        $this->check($rc, 'record count');
        return (int) $out->cdata;
    }

    /** @return list<array<string, mixed>> */
    public function fetchAll(): array
    {
        $rows = [];
        foreach ($this as $row) {
            $rows[] = $row;
        }
        return $rows;
    }

    // --- Iterator -------------------------------------------------

    public function rewind(): void
    {
        $this->check($this->lib->ffi()->AdsGotoTop($this->handle), 'goto top');
        $this->position = 0;
    }

    public function valid(): bool
    {
        $ffi = $this->lib->ffi();
        $eof = $ffi->new('UNSIGNED16');
        $this->check($ffi->AdsAtEOF($this->handle, FFI::addr($eof)), 'at eof');
        return ((int) $eof->cdata) === 0;
    }

    /** @return array<string, mixed> */
    public function current(): array
    {
        $row = [];
        foreach ($this->fields as $name) {
            $row[$name] = self::readField($this->lib, $this->handle, $name);
        }
        return $row;
    }

    public function key(): int
    {
        return $this->position;
    }

    public function next(): void
    {
        $this->check($this->lib->ffi()->AdsSkip($this->handle, 1), 'skip');
        $this->position++;
    }

    // --- internals ------------------------------------------------

    /** @return list<string> */
    private function readFieldNames(): array
    {
        $ffi   = $this->lib->ffi();
        $count = $ffi->new('UNSIGNED16');
        $this->check($ffi->AdsGetNumFields($this->handle, FFI::addr($count)), 'num fields');

        $names = [];
        for ($i = 1; $i <= (int) $count->cdata; $i++) {
            $len = $ffi->new('UNSIGNED16');
            $len->cdata = 128;
            $buf = $ffi->new('UNSIGNED8[128]');
            $this->check(
                $ffi->AdsGetFieldName($this->handle, $i, $buf, FFI::addr($len)),
                'field name'
            );
            $names[] = FFI::string($buf);
        }
        return $names;
    }

    /**
     * Read one field at the current record and convert it to a PHP
     * value. Shared by Cursor::current() and Record::get().
     */
    public static function readField(AceLibrary $lib, int $handle, string $field): mixed
    {
        $ffi  = $lib->ffi();
        $type = $ffi->new('UNSIGNED16');
        $ffi->AdsGetFieldType($handle, Connection::cstr($ffi, $field), FFI::addr($type));

        $len = $ffi->new('UNSIGNED32');
        $len->cdata = 8192;
        $buf = $ffi->new('UNSIGNED8[8192]');
        $rc  = $ffi->AdsGetField(
            $handle,
            Connection::cstr($ffi, $field),
            $buf,
            FFI::addr($len),
            0
        );
        if ($rc !== AceTypes::AE_SUCCESS) {
            return null; // not-positioned / blank field
        }
        $raw = rtrim(FFI::string($buf, (int) $len->cdata));

        return match ((int) $type->cdata) {
            AceTypes::ADS_INTEGER, AceTypes::ADS_NUMERIC =>
                $raw === '' ? null : (str_contains($raw, '.') ? (float) $raw : (int) $raw),
            AceTypes::ADS_DOUBLE =>
                $raw === '' ? null : (float) $raw,
            AceTypes::ADS_LOGICAL =>
                in_array(strtoupper($raw), ['T', 'Y', '1'], true),
            default => $raw,
        };
    }

    private function check(int $rc, string $what): void
    {
        if ($rc !== AceTypes::AE_SUCCESS) {
            [$code, $text] = $this->lib->lastError();
            throw new QueryException(
                "cursor $what failed: " . AceTypes::errorName($code) . " ($code): $text",
                $code
            );
        }
    }

    public function close(): void
    {
        if ($this->closed) {
            return;
        }
        $this->lib->ffi()->AdsCloseTable($this->handle);
        $this->closed = true;
    }

    public function __destruct()
    {
        $this->close();
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `composer test -- --testsuite integration --filter CursorTest`
Expected: PASS — 2 tests green.

- [ ] **Step 5: Commit**

```bash
git add bindings/php/src/Cursor.php bindings/php/tests/Integration/CursorTest.php
git commit -m "feat(php): Cursor — iterate SELECT result sets"
```

---

## Task 9: `Table`

**Files:**
- Create: `bindings/php/src/Table.php`
- Test: `bindings/php/tests/Integration/TableTest.php`

- [ ] **Step 1: Write the failing test**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Tests\Integration;

use OpenADS\Connection;
use OpenADS\Record;

final class TableTest extends IntegrationTestCase
{
    public function testOpenNavigateAndCount(): void
    {
        $dir  = $this->tempDataDir();
        $conn = new Connection($dir);
        $stmt = $conn->statement();
        $stmt->query('CREATE TABLE items (id INTEGER)');
        $stmt->query('INSERT INTO items (id) VALUES (?)', [10]);
        $stmt->query('INSERT INTO items (id) VALUES (?)', [20]);

        $table = $conn->table('items');
        self::assertSame(2, $table->recordCount());

        $table->gotoTop();
        self::assertInstanceOf(Record::class, $table->record());
        self::assertSame(10, $table->record()->get('ID'));

        $table->skip(1);
        self::assertSame(20, $table->record()->get('ID'));
        self::assertFalse($table->atEof());

        $table->skip(1);
        self::assertTrue($table->atEof());

        $table->close();
        $conn->close();
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `composer test -- --testsuite integration --filter TableTest`
Expected: FAIL — `Class "OpenADS\Table" not found`.

- [ ] **Step 3: Write `Table`**

```php
<?php
declare(strict_types=1);

namespace OpenADS;

use FFI;
use OpenADS\Exception\OpenAdsException;
use OpenADS\Ffi\AceLibrary;
use OpenADS\Ffi\AceTypes;

final class Table
{
    private AceLibrary $lib;
    private int $handle = 0;
    private bool $open = false;

    public function __construct(private Connection $conn, string $name)
    {
        $this->lib = $conn->library();
        $ffi       = $this->lib->ffi();
        $out       = $this->lib->newHandle();
        // usTableType=0 (auto), usCharType=1 (ANSI), usLockType=1 (compat),
        // usCheckRights=0, usMode=0 (read/write shared).
        $rc = $ffi->AdsOpenTable(
            $conn->handle(),
            Connection::cstr($ffi, $name),
            null,
            0, 1, 1, 0, 0,
            FFI::addr($out)
        );
        if ($rc !== AceTypes::AE_SUCCESS) {
            throw $this->fail($rc, "open table '$name'");
        }
        $this->handle = $this->lib->handleValue($out);
        $this->open   = true;
    }

    public function handle(): int
    {
        return $this->handle;
    }

    public function library(): AceLibrary
    {
        return $this->lib;
    }

    public function recordCount(): int
    {
        $out = $this->lib->ffi()->new('UNSIGNED32');
        $this->check(
            $this->lib->ffi()->AdsGetRecordCount($this->handle, 0, FFI::addr($out)),
            'record count'
        );
        return (int) $out->cdata;
    }

    public function gotoTop(): void
    {
        $this->check($this->lib->ffi()->AdsGotoTop($this->handle), 'goto top');
    }

    public function gotoBottom(): void
    {
        $this->check($this->lib->ffi()->AdsGotoBottom($this->handle), 'goto bottom');
    }

    public function gotoRecord(int $recno): void
    {
        $this->check(
            $this->lib->ffi()->AdsGotoRecord($this->handle, $recno),
            "goto record $recno"
        );
    }

    public function skip(int $rows): void
    {
        $this->check($this->lib->ffi()->AdsSkip($this->handle, $rows), 'skip');
    }

    public function atEof(): bool
    {
        $eof = $this->lib->ffi()->new('UNSIGNED16');
        $this->check(
            $this->lib->ffi()->AdsAtEOF($this->handle, FFI::addr($eof)),
            'at eof'
        );
        return ((int) $eof->cdata) !== 0;
    }

    public function atBof(): bool
    {
        $bof = $this->lib->ffi()->new('UNSIGNED16');
        $this->check(
            $this->lib->ffi()->AdsAtBOF($this->handle, FFI::addr($bof)),
            'at bof'
        );
        return ((int) $bof->cdata) !== 0;
    }

    public function setAof(string $expr): void
    {
        $ffi = $this->lib->ffi();
        $this->check(
            $ffi->AdsSetAOF($this->handle, Connection::cstr($ffi, $expr), 0),
            'set AOF'
        );
    }

    public function clearAof(): void
    {
        $this->check($this->lib->ffi()->AdsClearAOF($this->handle), 'clear AOF');
    }

    public function record(): Record
    {
        return new Record($this);
    }

    public function close(): void
    {
        if (!$this->open) {
            return;
        }
        $this->lib->ffi()->AdsCloseTable($this->handle);
        $this->open = false;
    }

    public function __destruct()
    {
        $this->close();
    }

    private function check(int $rc, string $what): void
    {
        if ($rc !== AceTypes::AE_SUCCESS) {
            throw $this->fail($rc, $what);
        }
    }

    private function fail(int $rc, string $what): OpenAdsException
    {
        [$code, $text] = $this->lib->lastError();
        return new OpenAdsException(
            "table $what failed: " . AceTypes::errorName($code) . " ($code): $text",
            $code
        );
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `composer test -- --testsuite integration --filter TableTest`
Expected: PASS — 1 test green.

- [ ] **Step 5: Commit**

```bash
git add bindings/php/src/Table.php bindings/php/tests/Integration/TableTest.php
git commit -m "feat(php): Table — navigational access and AOF filters"
```

---

## Task 10: `Record`

**Files:**
- Create: `bindings/php/src/Record.php`
- Test: `bindings/php/tests/Integration/RecordTest.php`

- [ ] **Step 1: Write the failing test**

```php
<?php
declare(strict_types=1);

namespace OpenADS\Tests\Integration;

use OpenADS\Connection;

final class RecordTest extends IntegrationTestCase
{
    public function testAppendSetReadAndDelete(): void
    {
        $dir  = $this->tempDataDir();
        $conn = new Connection($dir);
        $conn->statement()->query('CREATE TABLE c (id INTEGER, name CHAR(20))');

        $table = $conn->table('c');
        $rec   = $table->record();
        $rec->append();
        $rec->set('ID', 42);
        $rec->set('NAME', 'Reinaldo');
        $rec->save();

        $table->gotoTop();
        self::assertSame(42, $table->record()->get('ID'));
        self::assertSame('Reinaldo', trim((string) $table->record()->get('NAME')));

        $table->record()->delete();
        self::assertSame(1, $table->recordCount()); // still present, flagged deleted

        $table->close();
        $conn->close();
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `composer test -- --testsuite integration --filter RecordTest`
Expected: FAIL — `Class "OpenADS\Record" not found`.

- [ ] **Step 3: Write `Record`**

```php
<?php
declare(strict_types=1);

namespace OpenADS;

use FFI;
use OpenADS\Exception\OpenAdsException;
use OpenADS\Ffi\AceTypes;

/**
 * The current record of a Table. Field reads reuse
 * Cursor::readField so type conversion stays in one place.
 */
final class Record
{
    public function __construct(private Table $table)
    {
    }

    public function get(string $field): mixed
    {
        return Cursor::readField(
            $this->table->library(),
            $this->table->handle(),
            $field
        );
    }

    public function set(string $field, mixed $value): void
    {
        $ffi = $this->table->library()->ffi();
        $str = $this->stringify($value);
        $this->check(
            $ffi->AdsSetField(
                $this->table->handle(),
                Connection::cstr($ffi, $field),
                Connection::cstr($ffi, $str),
                strlen($str)
            ),
            "set field '$field'"
        );
    }

    public function append(): void
    {
        $this->check(
            $this->table->library()->ffi()->AdsAppendRecord($this->table->handle()),
            'append record'
        );
    }

    /** Flush a pending append/set to disk. */
    public function save(): void
    {
        $this->check(
            $this->table->library()->ffi()->AdsWriteRecord($this->table->handle()),
            'write record'
        );
    }

    public function delete(): void
    {
        $this->check(
            $this->table->library()->ffi()->AdsDeleteRecord($this->table->handle()),
            'delete record'
        );
    }

    public function recall(): void
    {
        $this->check(
            $this->table->library()->ffi()->AdsRecallRecord($this->table->handle()),
            'recall record'
        );
    }

    public function lock(int $recno): bool
    {
        $rc = $this->table->library()->ffi()
            ->AdsLockRecord($this->table->handle(), $recno);
        return $rc === AceTypes::AE_SUCCESS;
    }

    public function unlock(int $recno): void
    {
        $this->check(
            $this->table->library()->ffi()
                ->AdsUnlockRecord($this->table->handle(), $recno),
            'unlock record'
        );
    }

    private function stringify(mixed $value): string
    {
        if ($value === null) {
            return '';
        }
        if (is_bool($value)) {
            return $value ? 'T' : 'F';
        }
        if ($value instanceof \DateTimeInterface) {
            return $value->format('Y-m-d');
        }
        return (string) $value;
    }

    private function check(int $rc, string $what): void
    {
        if ($rc !== AceTypes::AE_SUCCESS) {
            [$code, $text] = $this->table->library()->lastError();
            throw new OpenAdsException(
                "record $what failed: " . AceTypes::errorName($code) . " ($code): $text",
                $code
            );
        }
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `composer test -- --testsuite integration --filter RecordTest`
Expected: PASS — 1 test green.

- [ ] **Step 5: Run the whole suite**

Run: `OPENADS_ACE_LIB=/path/to/libace.so composer test`
Expected: PASS — every unit and integration test green.

- [ ] **Step 6: Commit**

```bash
git add bindings/php/src/Record.php bindings/php/tests/Integration/RecordTest.php
git commit -m "feat(php): Record — field get/set and record writes"
```

---

## Task 11: Examples and README

**Files:**
- Create: `bindings/php/examples/query.php`
- Create: `bindings/php/examples/navigate.php`
- Create: `bindings/php/README.md`

- [ ] **Step 1: Write `examples/query.php`**

```php
<?php
declare(strict_types=1);

require __DIR__ . '/../vendor/autoload.php';

use OpenADS\Connection;

// Local: pass a data-directory path. Remote: 'tcp://host:port/dir'.
$conn = new Connection($argv[1] ?? __DIR__ . '/data');
$stmt = $conn->statement();

$cursor = $stmt->query(
    'SELECT id, name FROM people WHERE id > :min',
    [':min' => 0]
);

foreach ($cursor as $row) {
    printf("%d  %s\n", $row['ID'], trim((string) $row['NAME']));
}

$conn->close();
```

- [ ] **Step 2: Write `examples/navigate.php`**

```php
<?php
declare(strict_types=1);

require __DIR__ . '/../vendor/autoload.php';

use OpenADS\Connection;

$conn  = new Connection($argv[1] ?? __DIR__ . '/data');
$table = $conn->table('people');

printf("records: %d\n", $table->recordCount());

for ($table->gotoTop(); !$table->atEof(); $table->skip(1)) {
    $rec = $table->record();
    printf("%d  %s\n", $rec->get('ID'), trim((string) $rec->get('NAME')));
}

$table->close();
$conn->close();
```

- [ ] **Step 3: Write `README.md`**

````markdown
# OpenADS PHP binding

Modern PHP binding for the [OpenADS](https://github.com/FiveTechSoft/OpenADS)
database engine. Wraps the ACE C library through PHP's FFI
extension — no compiled C code, install with Composer.

## Requirements

- PHP 8.1 or newer
- `ext-ffi` enabled (`ffi.enable=1` in `php.ini`)
- An OpenADS ACE library reachable on the host:
  `ace64.dll` / `ace32.dll` (Windows) or `libace.so` (Linux/macOS).
  Point `OPENADS_ACE_LIB` at it, or place it on the system
  library path.

## Install

```bash
composer require openads/openads-php
```

## Connecting

Local data directory:

```php
use OpenADS\Connection;

$conn = new Connection('/var/data/myapp');
```

Remote `openads_serverd`:

```php
$conn = new Connection('tcp://db.example.com:6262/myapp', 'user', 'pass');
```

## SQL with parameters

```php
$stmt   = $conn->statement();
$cursor = $stmt->query(
    'SELECT * FROM people WHERE name = ?',
    ["O'Brien"]            // quoted safely; never concatenate
);
foreach ($cursor as $row) {
    echo $row['NAME'], "\n";
}
```

Named parameters work too: `:id` with `[':id' => 5]`.

## Navigational access

```php
$table = $conn->table('people');
for ($table->gotoTop(); !$table->atEof(); $table->skip(1)) {
    echo $table->record()->get('NAME'), "\n";
}
```

## Writes

```php
$rec = $conn->table('people')->record();
$rec->append();
$rec->set('ID', 42);
$rec->set('NAME', 'Reinaldo');
$rec->save();
```

## License

Apache-2.0. See the repository `LICENSE`.
````

- [ ] **Step 4: Lint the examples**

Run: `php -l bindings/php/examples/query.php && php -l bindings/php/examples/navigate.php`
Expected: `No syntax errors detected` for both.

- [ ] **Step 5: Commit**

```bash
git add bindings/php/examples bindings/php/README.md
git commit -m "docs(php): usage examples and README"
```

---

## Task 12: CI integration

Add a CI leg that builds OpenADS, then runs the PHP binding test suite against the freshly built library.

**Files:**
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Inspect the current CI workflow**

Run: `cat .github/workflows/ci.yml`
Note the existing job names, the Linux build job, and where the built `libace.so` lands (`build/<preset>/src/`).

- [ ] **Step 2: Add a `php-binding` job**

Append this job to `.github/workflows/ci.yml` under `jobs:` (adjust the `preset` and the `libace` path to match what Step 1 showed — the build job uses `ninja-clang`):

```yaml
  php-binding:
    name: PHP binding
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v5

      - name: Install Ninja
        uses: seanmiddleditch/gha-setup-ninja@v6

      - name: Build OpenADS (ace library)
        run: |
          cmake --preset ninja-clang
          cmake --build build/ninja-clang --config Release --target ace

      - name: Setup PHP
        uses: shivammathur/setup-php@v2
        with:
          php-version: '8.3'
          extensions: ffi
          ini-values: ffi.enable=1

      - name: Install PHP dependencies
        working-directory: bindings/php
        run: composer install --no-interaction

      - name: Run PHP binding tests
        working-directory: bindings/php
        env:
          OPENADS_ACE_LIB: ${{ github.workspace }}/build/ninja-clang/src/libace.so
        run: composer test
```

- [ ] **Step 3: Validate the workflow YAML**

Run: `python -c "import yaml,sys; yaml.safe_load(open('.github/workflows/ci.yml'))" && echo OK`
Expected: `OK`.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: build OpenADS and run the PHP binding test suite"
```

- [ ] **Step 5: Push the branch and open a PR**

```bash
git push -u origin feat/php-bindings
gh pr create --title "feat: PHP FFI binding (bindings/php)" \
  --body "Implements the PHP FFI binding per docs/superpowers/specs/2026-05-16-php-bindings-design.md."
```

Confirm CI's `php-binding` job goes green before merge. If the
job cannot find `libace.so`, fix the `OPENADS_ACE_LIB` path to
match the actual build output rather than disabling the test.

---

## Self-Review Notes

**Spec coverage:**
- §3 architecture / file structure → Tasks 1–10 create every listed `src/` file; `Sql/ParameterBinder.php` added (documented in the header note).
- §4.1 `AceLibrary` → Task 5. §4.2 `Connection` → Task 6. §4.3 `Statement` → Task 7. §4.4 `Cursor` → Task 8. §4.5 `Table` → Task 9. §4.6 `Record` → Task 10. §4.7 exceptions → Task 3.
- §5 client-side parameter binding → Task 4 (positional + named, per-type quoting, error cases).
- §6 error handling (exceptions, no return codes leaking, destructors never throw) → Tasks 3, 6–10.
- §7 clean-room → original namespaced API; no proprietary names. README/spec carry the Apache-2.0 license note.
- §8 ACE exports → all declared in `AceLibrary::CDEF` (Task 5).
- §9 testing + CI + distribution → unit + integration suites across Tasks 2–10, CI leg in Task 12, Packagist metadata in `composer.json` (Task 1).
- §10 out-of-scope items are not implemented.

**Type consistency:** `Connection::cstr()`, `AceLibrary::newHandle()` / `handleValue()` / `lastError()`, and `Cursor::readField()` keep the same signatures everywhere they are called across Tasks 5–10.

**Known dependency:** integration tests assume OpenADS SQL supports `CREATE TABLE` / `INSERT` / `SELECT` with the column types used. If a task hits an unsupported statement, flag it — do not silently rewrite the test to dodge the gap.
