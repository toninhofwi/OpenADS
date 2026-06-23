# Field types

OpenADS stores data in two file formats with different field sets, and the
companion Harbour ORM maps stored values to native Harbour types through a
"casts" layer. This page covers all three so you know what you get back
when you read a column.

## DBF field types (the classic format)

When you create a DBF table you pass a list of `{ name, type, len, dec }`
entries to `DbCreate`. The `type` is a single letter:

| Letter | Type | Stored as |
|--------|------|-----------|
| `C` | character | fixed width, **space-padded** to `len` |
| `N` | numeric | number with `dec` decimal places |
| `D` | date | the engine's date type (stored `yyyymmdd`) |
| `L` | logical | true / false |
| `M` | memo | variable-length text in a sibling memo file |

This is the lowest common denominator and works everywhere.

## ADT typed-file field types (the richer format)

The ADT typed-file format supports a wider set of field types. Memo and
binary values live in a sibling memo store that the engine creates
automatically when needed. The available types include:

- character
- case-insensitive character
- logical
- date
- double
- integer
- short integer
- memo
- binary
- time
- timestamp
- autoincrement
- money

Reach for ADT when DBF's five types are too coarse -- for example when you
want a true integer or timestamp column rather than a wide `N` field. See
[`connection-strings.md`](connection-strings.md) for how to select the ADT
format (`AdsSetFileType( ADS_ADT )`).

## ORM casts (how values arrive in Harbour)

The companion Harbour ORM hydrates each column to a native Harbour value
using a **casts** layer. You name the cast on the model and the ORM
converts the stored value for you. A database `NULL` becomes `NIL`.

| Cast name | Harbour value you get | Typical column |
|-----------|-----------------------|----------------|
| `integer` | numeric (whole) | integer / autoincrement |
| `decimal:N` | numeric with `N` decimal places | numeric / money |
| `boolean` | logical (`.T.` / `.F.`) | logical |
| `date` | date | date |
| `datetime` | date-time | timestamp |
| `string` | character string | character / memo |
| *(none)* + `NULL` | `NIL` | any nullable column |

So a `decimal:2` cast on a money column gives you a numeric value rounded
to two places, and a `date` cast on a date column gives you a real Harbour
date you can compare and format directly.

## Fixed-width CHARACTER -- trim only for display

`CHARACTER` (DBF `C`, and ADT character) columns are **fixed width and
space-padded**. The stored value always keeps its full width. When you
show it in a grid or a report, wrap it in `AllTrim()`:

```harbour
? AllTrim( oRow:name )      // "Maria" instead of "Maria          "
```

Trim only at display time. Do not strip the padding before storing -- the
stored value is meant to keep its width, and a later read will pad it back
out anyway. If strings look truncated in a grid, that is the fixed width
showing, not lost data.

Dates appear in your application however you format them; store them as the
engine's date type and format on the way out.

See [`../README.md`](../README.md) for the bigger picture and
[`building-and-running.md`](building-and-running.md) to compile the
examples that use these types.
