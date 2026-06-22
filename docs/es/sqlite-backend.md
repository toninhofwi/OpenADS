---
title: Backend SQLite
layout: default
parent: Inicio (ES)
nav_order: 8
permalink: /es/sqlite-backend/
---

# Tablas con backend SQLite

OpenADS puede abrir y operar una base de datos **SQLite** a
través de la misma superficie ACE / rddads usada para tablas
DBF / ADT. Desde el punto de vista de una aplicación Harbour /
Clipper / X#, la tabla SQLite se comporta como un área de trabajo
normal — navegación (`Skip`, `GoTop`, `GoBottom`),
lectura/escritura de campos y las llamadas `Ads*` estándar
funcionan.

## Requisitos

- OpenADS compilado con `OPENADS_WITH_SQLITE` — **activado por
  defecto** en `CMakeLists.txt` (la amalgama SQLite se incluye vía
  FetchContent).
- La URI de conexión debe empezar con `sqlite://` seguida del path
  al archivo `.db`.

## Cómo funciona

El camino SQLite se elige enteramente por la **URI de conexión**.
`AdsConnect60` llama a `parse_sqlite_uri()`; cuando la URI coincide
con `sqlite://…` abre una `SqliteConnection` en lugar del motor
DBF/CDX nativo. Cada llamada ACE posterior (`AdsOpenTable90`,
`AdsGetField`, `AdsSetField`, `AdsSkip`, `AdsSeek`,
`AdsCreateIndex61`, …) se redirige al backend SQLite.

### 1. Conectar con una URI `sqlite://`

```clipper
LOCAL hConn
AdsConnect60( "sqlite:///path/to/database.db", ;
              ADS_LOCAL_SERVER, NIL, NIL, 0, @hConn )
```

La tercera barra es el `/` inicial del path absoluto, así que
`sqlite:///tmp/app.db` abre `/tmp/app.db`.

### 2. Abrir una tabla existente

A través de rddads la tabla se abre como cualquier área de
trabajo:

```clipper
USE customers VIA "ADSCDX" NEW SHARED
```

(Las aplicaciones X# usan el RDD `AXDBFCDX`; el ruteo a nivel ACE
es idéntico.)

## Mapeo de tipos de campo

El tipo de campo se infiere del tipo declarado de la columna
SQLite (coincidencia de subcadena, sin distinguir mayúsculas):

| El tipo declarado SQLite contiene | Tipo de campo OpenADS | Longitud |
|-----------------------------------|-----------------------|----------|
| `INT`                             | Integer               | 4        |
| `REAL` / `FLOA` / `DOUB`          | Double                | 8 (6 dec) |
| `BLOB`                            | Binary                | 10       |
| cualquier otro (ej. `TEXT`)       | Character             | 64       |

## Cifrado

Se puede pasar una clave de cifrado como parámetro de query; se
decodifica de URL antes de usarse:

```clipper
AdsConnect60( "sqlite:///path/db.sqlite?key=mipassword", ;
              ADS_LOCAL_SERVER, NIL, NIL, 0, @hConn )
```

## Limitaciones actuales

- **Solo apertura** — `AdsCreateTable` no crea tablas SQLite. Pasar
  un handle de conexión SQLite cae al camino DBF nativo; crea el
  esquema directamente en SQLite.
- **Índices** se exponen como `SqliteIndex` con seek / next / prev
  básicos que mapean a consultas `ORDER BY`.
- **Transacciones** se mapean a transacciones SQLite normales.
