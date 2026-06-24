---
title: Historia del Proyecto
layout: default
parent: Inicio (ES)
nav_order: 1
permalink: /es/historia/
---

# Historia del Proyecto

OpenADS creció desde un esqueleto mínimo hasta un motor completo
compatible con ADS a lo largo de **868 commits** organizados en
lotes por hitos. Esta página traza las fases principales.

---

## M0–M2: Cimientos (2025)

Los primeros commits establecieron el sistema de build, los
thunks ABI y el ciclo de vida básico de las tablas.

| Hito | Qué llegó |
|------|-----------|
| **M0** | Andamiaje CMake, `HandleRegistry`, `Connection`, framework doctest |
| **M1** | Parser de cabecera DBF compartido por CDX/NTX/VFP, máquina de estados `Cursor` (Bof/Positioned/Eof/Limbo), `CdxDriver` de solo lectura |
| **M2** | Append / update / delete de registros, `LockMgr` con bloqueo por byte-ranges (modo Compatible), driver NTX como label-only |

---

## M3: Índices — el corazón de xBase (2025)

Implementaciones de B+tree para los dos formatos de índice
xBase.

- **NTX** — read + write + create + split multi-level
- **CDX** — compound multi-tag, leaf splits, sibling links
- `AdsSeek`, `AdsSetOrder`, `AdsSetScope` / `AdsClearScope`
- Primer 100% pass de `rddtst.prg` de Harbour (442 / 422)

---

## M4–M6: Formatos nativos y transacciones (2025)

| Hito | Qué llegó |
|------|-----------|
| **M4** | ADT / ADI create/read/write/seek nativo, memo ADM, campos VFP (V/Q), tablas cifradas AES-256-CTR |
| **M5** | WAL con crash recovery ARIES-lite, BEGIN/COMMIT anidados, savepoints |
| **M6** | Index keys para Date / Time / Timestamp / Money / Logical en ADI |

---

## M10: Motor SQL (2025–2026)

Una serie masiva de ~53 hitos que construyó una capa SQL de
producción:

- SELECT, JOINs (INNER / LEFT / RIGHT / FULL), subqueries, CTEs
- GROUP BY + HAVING, UNION / UNION ALL, DISTINCT, LIMIT / OFFSET
- CASE WHEN, ROW_NUMBER(), RANK / DENSE_RANK, PARTITION BY
- INSERT INTO … SELECT, CREATE TABLE AS SELECT
- Búsqueda de texto completo (CONTAINS, LIKE, BETWEEN)
- Funciones escalares, alias de columnas, tablas virtuales `system.*`

---

## M11: Características avanzadas (2026)

Collation, conversión OEM / UTF-8, bitmap de NULLs, AEP host
(CREATE / EXECUTE PROCEDURE) y DBF cifrado OpenADS
(AES-256-CTR).

---

## M12: La red — de local a remoto (2026)

La fase más ambiciosa: convertir OpenADS en un servidor de base
de datos de red.

| Hito | Qué llegó |
|------|-----------|
| M12.1–M12.3 | Socket layer, accept loop del server, dispatch Hello / Connect |
| M12.4–M12.5 | DLL dual-mode — URI `tcp://` rutea ABI al server |
| M12.6–M12.9 | Superficie de escritura remota, SQL exec, reindex, auth |
| M12.10–M12.13 | Propagación de errores, batch fetch, **TLS real** (mbedtls 3.6) |
| M12.14–M12.19 | Metadata de campos remotos, handles de índice, row cache, cache de record-count |
| M12.22–M12.23 | Compatibilidad total con X# Advantage RDD (local + remoto) |

---

## Studio: consola web embebida (2026)

14 iteraciones (`studio.web.0.1` → `0.14`) produjeron una SPA
completa:

- Browse paginado con CRUD inline, editor SQL, pestaña Sessions
- Visor de Data Dictionary, wizard CREATE INDEX, Reindex / Pack / Zap
- HTTP Basic auth, toggle de tema, backup ZIP, visor hex de memo
- Exportación a JSON, barra de filtros AOF (Rushmore)

---

## OpenADS Plus: múltiples backends SQL (2026)

Un registry `BackendTableOps` permitió agregar backends sin
duplicar lógica ABI:

| Backend | Driver |
|---------|--------|
| SQLite | integrado (`OPENADS_WITH_SQLITE`) |
| SQLCipher | SQLite3 Multiple Ciphers |
| PostgreSQL | libpq nativo |
| MariaDB / MySQL | nativo |
| ODBC | cualquier driver (Firebird, SQL Server, …) |
| MSSQL | TDS 7.4 nativo (PR #53) |

---

## DA-Web: Data Architect en el navegador (2026)

Un frontend PHP completo para administrar OpenADS:

- Editor SQL con syntax highlighting, edición inline de tablas
- CRUD de Data Dictionary (usuarios, permisos, triggers, SPs, vistas)
- Import wizard desde archivos `.add` binarios de SAP
- Editor visual de Referential Integrity, visor de permisos efectivos

---

## Ecosistema (2026)

| Componente | Descripción |
|------------|-------------|
| **PHP Bindings** | `openads/openads-php` — paquete FFI puro con API OOP |
| **Harbour ORM** | ORM ActiveRecord para Harbour sobre la ABI ACE |
| **openmonitor** | TUI + dashboard web para `openads_serverd` |
| **Cookbook** | Ejemplos Harbour ejecutables: console, ORM, FiveWin |
| **Docs** | Sitio trilingüe (EN / ES / PT) vía GitHub Pages |

---

## Cronología de releases

| Versión | Fecha | Destacados |
|---------|-------|------------|
| 0.3.0 | 2025 | Motor SQL M10, features M11 |
| 0.3.6 | 2026-01 | Wire skeleton M12, TLS, serverd |
| v1.0.0-rc1 | 2026-03 | AOF (Rushmore), Studio web |
| v1.0.0-rc9 | 2026-04 | Studio embebido (LocalServer) |
| v1.0.0-rc14 | 2026-04 | Servicio Windows + systemd |
| v1.0.0-rc19 | 2026-05 | Compat X# RDD |
| v1.0.0-rc25 | 2026-05 | Barrido de corrección de índices |
| v1.0.0-rc27 | 2026-06 | Padding CHAR en AdsGetField |
| v1.0.1 | 2026-06 | Cookbook, Studio responsive |
| v1.0.3 | 2026-06 | Performance de scan remoto, endurecimiento ODBC |
| v1.0.4 | 2026-06 | Fix de record-count stale en CDX |
| v1.1.0 | 2026-06 | Backends PostgreSQL / MariaDB / ODBC |
| v1.2.0 | 2026-06 | Bulk insert deferred-flush (528×), MSSQL TDS 7.4 |
| v1.2.1 | 2026-06 | Tests unitarios, benchmarks remotos, fix NTX numeric key |

---

## Patrón de desarrollo

El proyecto sigue un enfoque **impulsado por hitos**: cada `M`
representa una funcionalidad completa con tests. Los PRs
numerados (#4–#67) llevan fixes y features específicas. La
calidad se mantiene con `-Werror` en MSVC, GCC y Clang, y la
suite de tests creció de 0 a **720 / 720** aserciones pasando.
