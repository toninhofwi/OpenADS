---
title: História do Projeto
layout: default
parent: Início (PT)
nav_order: 1
permalink: /pt/historia/
---

# História do Projeto

OpenADS cresceu de um esqueleto mínimo até um motor completo
compatível com ADS ao longo de **868 commits** organizados em
lotes por marcos. Esta página traça as fases principais.

---

## M0–M2: Fundações (2025)

Os primeiros commits estabeleceram o sistema de build, os thunks
ABI e o ciclo de vida básico das tabelas.

| Marco | O que chegou |
|-------|--------------|
| **M0** | Scaffold CMake, `HandleRegistry`, `Connection`, framework doctest |
| **M1** | Parser de cabeçalho DBF compartilhado por CDX/NTX/VFP, máquina de estados `Cursor` (Bof/Positioned/Eof/Limbo), `CdxDriver` somente leitura |
| **M2** | Append / update / delete de registros, `LockMgr` com bloqueio por byte-ranges (modo Compatible), driver NTX como label-only |

---

## M3: Índices — o coração do xBase (2025)

Implementações de B+tree para os dois formatos de índice xBase.

- **NTX** — read + write + create + split multi-level
- **CDX** — compound multi-tag, leaf splits, sibling links
- `AdsSeek`, `AdsSetOrder`, `AdsSetScope` / `AdsClearScope`
- Primeiro 100% pass do `rddtst.prg` do Harbour (442 / 442)

---

## M4–M6: Formatos nativos e transações (2025)

| Marco | O que chegou |
|-------|--------------|
| **M4** | ADT / ADI create/read/write/seek nativo, memo ADM, tipos de campo VFP (V/Q), tabelas criptografadas AES-256-CTR |
| **M5** | WAL com crash recovery ARIES-lite, BEGIN/COMMIT aninhados, savepoints |
| **M6** | Index keys para Date / Time / Timestamp / Money / Logical no ADI |

---

## M10: Motor SQL (2025–2026)

Uma série massiva de ~53 marcos que construiu uma camada SQL
de produção:

- SELECT, JOINs (INNER / LEFT / RIGHT / FULL), subqueries, CTEs
- GROUP BY + HAVING, UNION / UNION ALL, DISTINCT, LIMIT / OFFSET
- CASE WHEN, ROW_NUMBER(), RANK / DENSE_RANK, PARTITION BY
- INSERT INTO … SELECT, CREATE TABLE AS SELECT
- Busca em texto completo (CONTAINS, LIKE, BETWEEN)
- Funções escalares, aliases de colunas, tabelas virtuais `system.*`

---

## M11: Funcionalidades avançadas (2026)

Collation, conversão OEM / UTF-8, bitmap de NULLs, AEP host
(CREATE / EXECUTE PROCEDURE) e DBF criptografado OpenADS
(AES-256-CTR).

---

## M12: A rede — de local para remoto (2026)

A fase mais ambiciosa: transformar OpenADS em um servidor de
banco de dados em rede.

| Marco | O que chegou |
|-------|--------------|
| M12.1–M12.3 | Socket layer, accept loop do server, dispatch Hello / Connect |
| M12.4–M12.5 | DLL dual-mode — URI `tcp://` roteia ABI para o server |
| M12.6–M12.9 | Superfície de escrita remota, SQL exec, reindex, auth |
| M12.10–M12.13 | Propagação de erros, batch fetch, **TLS real** (mbedtls 3.6) |
| M12.14–M12.19 | Metadados de campos remotos, handles de índice, row cache, cache de record-count |
| M12.22–M12.23 | Compatibilidade total com X# Advantage RDD (local + remoto) |

---

## Studio: console web embutida (2026)

14 iterações (`studio.web.0.1` → `0.14`) produziram uma SPA
completa:

- Browse paginado com CRUD inline, editor SQL, aba Sessions
- Visor de Data Dictionary, wizard CREATE INDEX, Reindex / Pack / Zap
- HTTP Basic auth, toggle de tema, backup ZIP, visor hex de memo
- Exportação JSON, barra de filtros AOF (Rushmore)

---

## OpenADS Plus: múltiplos backends SQL (2026)

Um registry `BackendTableOps` permitiu adicionar backends sem
duplicar lógica ABI:

| Backend | Driver |
|---------|--------|
| SQLite | embutido (`OPENADS_WITH_SQLITE`) |
| SQLCipher | SQLite3 Multiple Ciphers |
| PostgreSQL | libpq nativo |
| MariaDB / MySQL | nativo |
| ODBC | qualquer driver (Firebird, SQL Server, …) |
| MSSQL | TDS 7.4 nativo (PR #53) |

---

## DA-Web: Data Architect no navegador (2026)

Um frontend PHP completo para administrar OpenADS:

- Editor SQL com syntax highlighting, edição inline de tabelas
- CRUD de Data Dictionary (usuários, permissões, triggers, SPs, views)
- Import wizard de arquivos `.add` binários da SAP
- Editor visual de Referential Integrity, visor de permissões efetivas

---

## Ecossistema (2026)

| Componente | Descrição |
|------------|-----------|
| **PHP Bindings** | `openads/openads-php` — pacote FFI puro com API OOP |
| **Harbour ORM** | ORM ActiveRecord para Harbour sobre a ABI ACE |
| **openmonitor** | TUI + dashboard web para `openads_serverd` |
| **Cookbook** | Exemplos Harbour executáveis: console, ORM, FiveWin |
| **Docs** | Site trilingue (EN / ES / PT) via GitHub Pages |

---

## Cronologia de releases

| Versão | Data | Destaques |
|--------|------|-----------|
| 0.3.0 | 2025 | Motor SQL M10, features M11 |
| 0.3.6 | 2026-01 | Wire skeleton M12, TLS, serverd |
| v1.0.0-rc1 | 2026-03 | AOF (Rushmore), Studio web |
| v1.0.0-rc9 | 2026-04 | Studio embutido (LocalServer) |
| v1.0.0-rc14 | 2026-04 | Serviço Windows + systemd |
| v1.0.0-rc19 | 2026-05 | Compat X# RDD |
| v1.0.0-rc25 | 2026-05 | Varredura de correção de índices |
| v1.0.0-rc27 | 2026-06 | Padding CHAR no AdsGetField |
| v1.0.1 | 2026-06 | Cookbook, Studio responsivo |
| v1.0.3 | 2026-06 | Performance de scan remoto, hardening ODBC |
| v1.0.4 | 2026-06 | Fix de record-count stale no CDX |
| v1.1.0 | 2026-06 | Backends PostgreSQL / MariaDB / ODBC |
| v1.2.0 | 2026-06 | Bulk insert deferred-flush (528×), MSSQL TDS 7.4 |
| v1.2.1 | 2026-06 | Testes unitários, benchmarks remotos, fix NTX numeric key |

---

## Padrão de desenvolvimento

O projeto segue uma abordagem **impulsionada por marcos**: cada
`M` representa uma funcionalidade completa com testes. Os PRs
numerados (#4–#67) carregam fixes e features específicas. A
qualidade é mantida com `-Werror` em MSVC, GCC e Clang, e a
suíte de testes cresceu de 0 a **720 / 720** asserções passando.
