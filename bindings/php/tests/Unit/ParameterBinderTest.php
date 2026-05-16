<?php
declare(strict_types=1);

namespace OpenADS\Tests\Unit;

use OpenADS\Exception\QueryException;
use OpenADS\Sql\ParameterBinder;
use PHPUnit\Framework\TestCase;

final class ParameterBinderTest extends TestCase
{
    // --- original tests ---------------------------------------------------

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

    // --- Fix 1: INF / NAN produce invalid SQL ----------------------------

    public function testInfThrowsQueryException(): void
    {
        $this->expectException(QueryException::class);
        $this->expectExceptionMessage('non-finite float');
        ParameterBinder::bind('VALUES (?)', [INF]);
    }

    public function testNanThrowsQueryException(): void
    {
        $this->expectException(QueryException::class);
        $this->expectExceptionMessage('non-finite float');
        ParameterBinder::bind('VALUES (?)', [NAN]);
    }

    // --- Fix 2: empty $params skips placeholder validation ----------------

    public function testEmptyParamsNoPlaceholderReturnsUnchanged(): void
    {
        // No placeholders, no params — must return the SQL untouched.
        $sql = ParameterBinder::bind('SELECT 1', []);
        self::assertSame('SELECT 1', $sql);
    }

    public function testEmptyParamsWithPlaceholderThrows(): void
    {
        // Placeholder present but no params — must throw, not silently return '?'.
        $this->expectException(QueryException::class);
        ParameterBinder::bind('WHERE id = ?', []);
    }

    // --- Fix 3: named-param re-substitution corrupts data ----------------

    public function testNamedParamValueContainingOtherToken(): void
    {
        // ':city' value contains ':co' — single-pass must not re-substitute.
        $sql = ParameterBinder::bind(
            'WHERE city = :city AND co = :co',
            [':city' => 'Mex:co City', ':co' => 'MX']
        );
        self::assertSame("WHERE city = 'Mex:co City' AND co = 'MX'", $sql);
    }

    // --- Fix 4: non-string / empty-string named keys ---------------------

    public function testEmptyStringNamedKeyThrows(): void
    {
        $this->expectException(QueryException::class);
        $this->expectExceptionMessage('non-empty strings');
        ParameterBinder::bind('WHERE x = :x', ['' => 1]);
    }

    public function testIntegerKeyInMixedArrayThrows(): void
    {
        // A string key is present so isNamed() returns true; integer key 0 must throw.
        $this->expectException(QueryException::class);
        $this->expectExceptionMessage('non-empty strings');
        ParameterBinder::bind('WHERE x = :x', [0 => 1, ':x' => 2]);
    }
}
