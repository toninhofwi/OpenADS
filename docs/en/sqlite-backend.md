---
title: SQLite backend
layout: default
parent: Home (EN)
nav_order: 8
permalink: /en/sqlite-backend/
---

# SQLite-backed tables

OpenADS can open and drive a **SQLite** database through the same
ACE / rddads surface used for DBF / ADT tables. From the point of
view of a Harbour / Clipper / X# application the SQLite table
behaves like an ordinary work area — navigation (`Skip`,
`GoTop`, `GoBottom`), field read/write, and the standard `Ads*`
calls all work.

## Requirements

- OpenADS built with `OPENADS_WITH_SQLITE` — this is **ON by
  default** in `CMakeLists.txt` (the SQLite amalgamation is
  vendored via FetchContent).
- The connection URI must start with `sqlite://` followed by the
  path to the `.db` file.

## How it works

The SQLite path is selected entirely by the **connection URI**.
`AdsConnect60` calls `parse_sqlite_uri()`; when the URI matches
`sqlite://…` it opens a `SqliteConnection` instead of the native
DBF/CDX engine. Every later ACE call (`AdsOpenTable90`,
`AdsGetField`, `AdsSetField`, `AdsSkip`, `AdsSeek`,
`AdsCreateIndex61`, …) is routed to the SQLite backend.

### 1. Connect with a `sqlite://` URI

```clipper
LOCAL hConn
AdsConnect60( "sqlite:///path/to/database.db", ;
              ADS_LOCAL_SERVER, NIL, NIL, 0, @hConn )
```

The third slash is the leading `/` of the absolute file path, so
`sqlite:///tmp/app.db` opens `/tmp/app.db`.

### 2. Open an existing table

Through rddads the table opens like any other work area:

```clipper
USE customers VIA "ADSCDX" NEW SHARED
```

(X# applications use the `AXDBFCDX` RDD; the ACE-level routing is
identical.)

## Field-type mapping

The field type is inferred from the SQLite column's declared type
(case-insensitive substring match):

| SQLite declared type contains | OpenADS field type | Length |
|-------------------------------|--------------------|--------|
| `INT`                         | Integer            | 4      |
| `REAL` / `FLOA` / `DOUB`      | Double             | 8 (6 dec) |
| `BLOB`                        | Binary             | 10     |
| anything else (e.g. `TEXT`)   | Character          | 64     |

## Encryption

A cipher key can be supplied as a query parameter; it is
URL-decoded before use:

```clipper
AdsConnect60( "sqlite:///path/db.sqlite?key=mypassword", ;
              ADS_LOCAL_SERVER, NIL, NIL, 0, @hConn )
```

## Current limitations

- **Open only** — `AdsCreateTable` does not create SQLite tables.
  Passing a SQLite connection handle falls back to the native DBF
  path; create the schema in SQLite directly instead.
- **Indexes** are exposed as `SqliteIndex` with basic
  seek / next / prev that map to `ORDER BY` queries.
- **Transactions** map to ordinary SQLite transactions.
