<?php
declare(strict_types=1);

namespace OpenADS\Sql;

use OpenADS\Exception\QueryException;

/**
 * Substitutes parameters into SQL text client-side. OpenADS ACE
 * has no host-variable binding, so this is where values become
 * literals — and the only place injection is defended against.
 * A statement uses positional `?` OR named `:name`, not both.
 *
 * LIMITATION: a literal `?` or `:name` that appears inside a quoted
 * string in the SQL template is not supported — it will be treated as
 * a placeholder, the same restriction as PDO emulated prepares.
 */
final class ParameterBinder
{
    public static function bind(string $sql, array $params): string
    {
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
        $tokens = [];           // token => value
        foreach ($params as $name => $value) {
            if (!is_string($name) || $name === '') {
                throw new QueryException('named parameter keys must be non-empty strings');
            }
            $token = ($name[0] === ':') ? $name : ':' . $name;
            $tokens[$token] = $value;
        }
        // Longest token first so :abc wins over :ab in the alternation.
        uksort($tokens, static fn ($a, $b) => strlen((string) $b) <=> strlen((string) $a));
        $pattern = '/' . implode('|', array_map(
            static fn ($t) => preg_quote((string) $t, '/'),
            array_keys($tokens)
        )) . '/';
        return preg_replace_callback(
            $pattern,
            static fn (array $m): string => self::quote($tokens[$m[0]]),
            $sql
        );
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
        if (is_float($value) && !is_finite($value)) {
            throw new QueryException('non-finite float cannot be a SQL parameter');
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
