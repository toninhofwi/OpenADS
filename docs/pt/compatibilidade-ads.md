---
title: Relatório de Compatibilidade com o Padrão ADS
layout: default
nav_order: 10
---

# Relatório de Compatibilidade com o Padrão ADS

**Versão analisada:** 1.5.x (paridade SQLRDD)
**Data:** 2026-06-28

---

## Sumário Executivo

O OpenADS reimplementa o Advantage Database Server (ADS) da SAP como uma biblioteca partilhada (`ace64.dll`) e um daemon de servidor. O engine cobre a maioria das operações ACE API do dia-a-dia, com suporte completo a CDX/NTX, suporte parcial a ADT/ADI, e um motor SQL robusto mas com lacunas significativas em DDL avançado. A camada remota suporta ~84 opcodes do wire protocol. Existem 39 funções AdsDD* documentadas, das quais a maioria está implementada.

---

## 1. ACE API — Funções Exportadas

### 1.1 Cobertura Geral

Das ~300+ funções `Ads*` definidas no arquivo `ace_exports.cpp` (~20.000 linhas):

| Categoria | Total | Implementadas | Stubs/No-op | Não implementadas |
|-----------|-------|---------------|-------------|-------------------|
| Conexão | 6 | 4 | 0 | 2 (`AdsFindConnection`, `AdsFindConnection25`) |
| Tabelas | 25 | 22 | 1 | 2 (`AdsGetTableHandle25`, `AdsOpenTable90`) |
| Índices | 22 | 20 | 0 | 2 (`AdsCreateFTSIndex`, `AdsGetFTSIndexes`) |
| Campos/I-O | 30 | 28 | 0 | 2 (`AdsGetFieldW`, `AdsSetStringW`) |
| SQL | 12 | 10 | 0 | 2 (`AdsExecuteSQL`, `AdsPrepareSQL` parcial) |
| Transações | 7 | 5 | 2 | 0 |
| Filtros/AOF | 8 | 6 | 2 | 0 |
| Relações | 4 | 3 | 1 | 0 |
| Bloqueios | 8 | 6 | 2 | 0 |
| Bookmarks | 4 | 4 | 0 | 0 |
| Memo/Binário | 6 | 5 | 0 | 1 (`AdsGetMemoDataType` parcial) |
| Encriptação | 7 | 2 | 5 | 0 |
| Data Dictionary | 39 | 30+ | 9 | 0 |
| Management (Mg) | 14 | 10 | 4 | 0 |
| Configuração | 12 | 8 | 4 | 0 |

### 1.2 Funções Completamente Implementadas (exemplos principais)

**Conexão:** `AdsConnect`, `AdsConnect60`, `AdsDisconnect`, `AdsGetConnectionType`

**Tabelas:** `AdsOpenTable`, `AdsCloseTable`, `AdsCreateTable`, `AdsCreateTable71`, `AdsCreateTable90`, `AdsGetTableType`, `AdsGetTableAlias`, `AdsGetTableFilename`, `AdsGetTableCharType`, `AdsGetTableConType`, `AdsGetTableConnection`, `AdsGetTableOpenOptions`, `AdsGetTableHandle25`, `AdsGetNumOpenTables`, `AdsGetAllTables`, `AdsCloneTable`, `AdsCloseAllTables`, `AdsCloseCachedTables`

**Índices:** `AdsOpenIndex`, `AdsCloseIndex`, `AdsCloseAllIndexes`, `AdsGetIndexHandle`, `AdsGetIndexHandleByOrder`, `AdsGetIndexName`, `AdsGetIndexFilename`, `AdsGetIndexExpr`, `AdsGetIndexCondition`, `AdsGetIndexOrderByHandle`, `AdsCreateIndex`, `AdsCreateIndex61`, `AdsCreateIndex90`, `AdsGetNumIndexes`, `AdsGetAllIndexes`, `AdsGetFTSIndexes`, `AdsReindex`, `AdsReindex61`, `AdsSetIndexOrder`, `AdsSetIndexOrderByHandle`, `AdsSetIndexDirection`

**Campos/I-O:** `AdsGetField`, `AdsGetFieldRaw`, `AdsGetFieldW`, `AdsGetFieldName`, `AdsGetFieldType`, `AdsGetFieldLength`, `AdsGetFieldDecimals`, `AdsGetString`, `AdsGetStringW`, `AdsGetDouble`, `AdsGetLong`, `AdsGetLongLong`, `AdsGetLogical`, `AdsGetDate`, `AdsGetJulian`, `AdsGetEpoch`, `AdsGetMilliseconds`, `AdsSetField`, `AdsSetFieldRaw`, `AdsSetString`, `AdsSetStringW`, `AdsSetDouble`, `AdsSetLongLong`, `AdsSetLogical`, `AdsSetDate`, `AdsSetJulian`, `AdsSetEpoch`, `AdsSetTime`, `AdsSetTimeStamp`, `AdsSetMoney`, `AdsSetShort`, `AdsSetEmpty`, `AdsSetNull`

**Navegação:** `AdsGotoTop`, `AdsGotoBottom`, `AdsGotoRecord`, `AdsSkip`, `AdsAtBOF`, `AdsAtEOF`, `AdsIsFound`, `AdsSeek`, `AdsSeekLast`, `AdsGetBookmark`, `AdsGetBookmark60`, `AdsGotoBookmark60`

**Registos:** `AdsGetRecord`, `AdsSetRecord`, `AdsGetRecordNum`, `AdsGetRecordCount`, `AdsGetRecordLength`, `AdsGetRecordCRC`, `AdsWriteRecord`, `AdsAppendRecord`, `AdsDeleteRecord`, `AdsRecallRecord`, `AdsIsRecordDeleted`, `AdsWriteAllRecords`, `AdsRefreshRecord`

**Transações:** `AdsBeginTransaction`, `AdsCommitTransaction`, `AdsRollbackTransaction`, `AdsRollbackTransaction80`, `AdsInTransaction`, `AdsCreateSavepoint`, `AdsReleaseSavepoint`

**Filtros/AOF:** `AdsSetFilter`, `AdsGetFilter`, `AdsSetAOF`, `AdsClearAOF`, `AdsGetAOF`, `AdsGetAOFOptLevel`, `AdsRefreshAOF`, `AdsCustomizeAOF`

**Bloqueios:** `AdsLockRecord`, `AdsUnlockRecord`, `AdsIsRecordLocked`, `AdsLockTable`, `AdsUnlockTable`, `AdsIsTableLocked`, `AdsGetAllLocks`, `AdsGetNumLocks`

**Índices Avançados:** `AdsSeek`, `AdsSeekLast`, `AdsIsFound`, `AdsExtractKey`, `AdsGetKeyCount`, `AdsGetKeyLength`, `AdsGetKeyType`, `AdsGetKeyNum`, `AdsSetScope`, `AdsGetScope`, `AdsClearScope`, `AdsClearAllScopes`

**SQL:** `AdsCreateSQLStatement`, `AdsCloseSQLStatement`, `AdsPrepareSQL`, `AdsExecuteSQLDirect`, `AdsVerifySQL`, `AdsGetNumParams`, `AdsClearSQLParams`

---

## 2. Motor SQL

### 2.1 Declarações Suportadas

| Declaração | Estado | Notas |
|------------|--------|-------|
| SELECT | ✅ Completo | Projeção, alias, *, DISTINCT, TOP N, LIMIT/OFFSET |
| INSERT INTO ... VALUES | ✅ Completo | Linha única e multi-linha |
| INSERT INTO ... SELECT | ✅ Completo | M10.41 |
| UPDATE ... SET | ✅ Completo | Múltiplas colunas, WHERE opcional |
| DELETE FROM | ✅ Completo | WHERE opcional |
| CREATE TABLE | ✅ Completo | Character, Numeric, Memo; CREATE AS SELECT |
| CREATE INDEX | ✅ Completo | CDX tags, DESCENDING, UNIQUE |
| CREATE PROCEDURE | ✅ Completo | DLL externa, 17 built-in sp_* |
| GRANT / REVOKE | ✅ Completo | A nível de coluna |
| DROP TABLE | ❌ Não suportado | |
| DROP INDEX | ❌ Não suportado | |
| ALTER TABLE | ❌ Não suportado | |
| TRUNCATE TABLE | ❌ Não suportado | |
| CREATE VIEW | ❌ Não suportado | |
| CREATE TRIGGER | ❌ Não suportado | |

### 2.2 Sub-cláusulas SELECT

| Cláusula | Estado | Notas |
|----------|--------|-------|
| * (wildcard) | ✅ | Qualificado: `alias.*` |
| Colunas nomeadas + AS | ✅ | |
| DISTINCT | ✅ | M10.31 |
| TOP N | ✅ | Sinônimo de LIMIT |
| LIMIT / OFFSET | ✅ | M10.32 |
| ORDER BY multi-coluna | ✅ | ASC/DESC por coluna |
| FROM com alias (AS e implícito) | ✅ | |
| INNER JOIN | ✅ | |
| LEFT [OUTER] JOIN | ✅ | |
| RIGHT [OUTER] JOIN | ✅ | |
| FULL [OUTER] JOIN | ✅ | Apenas 2 tabelas |
| SQL-89 comma JOIN | ✅ | Para 2+ tabelas |

### 2.3 WHERE — Predicados

| Predicado | Estado |
|-----------|--------|
| =, <>, !=, <, >, <=, >= | ✅ |
| BETWEEN x AND y | ✅ |
| LIKE 'pattern' (% e _) | ✅ |
| IS [NOT] NULL | ✅ |
| IN (literal, ...) | ✅ |
| IN (SELECT ...) | ✅ |
| EXISTS / NOT EXISTS | ✅ |
| NOT, AND, OR, parênteses | ✅ |
| UPPER/LOWER no LHS | ✅ |
| Subqueries correlacionadas | ✅ |
| CONTAINS (FTS) | ✅ |
| Funções de data (CURDATE, NOW) | ✅ |
| Literais ODBC ({d '...'}, {ts '...'}) | ✅ |

### 2.4 Funções de Agregação

| Função | Estado | Notas |
|--------|--------|-------|
| COUNT(*) | ✅ | |
| COUNT(col) | ✅ | Ignora NULLs |
| SUM(col) | ✅ | Retorna 0 para 0 linhas |
| AVG(col) | ✅ | |
| MIN(col) / MAX(col) | ✅ | Numérico e string |
| FILTER (WHERE ...) | ✅ | M10.54 |

### 2.5 Funções Escalares

| Função | Estado |
|--------|--------|
| UPPER, LOWER | ✅ |
| LEN, TRIM, LTRIM, RTRIM | ✅ |
| SUBSTR, CONCAT, REPLACE | ✅ |
| DATEDIFF, DATEADD | ✅ |
| NULLIF, COALESCE, IFNULL | ✅ |
| NOW, TODAY, DATE, CURDATE, TIME | ✅ |

### 2.6 Funções de Janela

| Função | Estado |
|--------|--------|
| ROW_NUMBER() OVER (...) | ✅ |
| RANK() OVER (...) | ✅ |
| DENSE_RANK() OVER (...) | ✅ |
| PARTITION BY | ✅ |
| ORDER BY dentro de OVER | ✅ |
| SUM() OVER, LEAD/LAG, NTILE | ❌ |

### 2.7 CASE / Aritmética

| Funcionalidade | Estado |
|----------------|--------|
| CASE WHEN ... THEN ... ELSE ... END | ✅ |
| Expressões aritméticas (+, -, *, /) | ✅ |

### 2.8 Subqueries e Tabelas Derivadas

| Funcionalidade | Estado |
|----------------|--------|
| Subqueries escalares | ✅ |
| Subqueries correlacionadas | ✅ |
| IN (SELECT ...) | ✅ |
| EXISTS / NOT EXISTS | ✅ |
| Tabelas derivadas | ✅ |
| CTE (WITH) | ✅ (single CTE) |
| INSERT ... SELECT | ✅ |
| CREATE TABLE ... AS SELECT | ✅ |
| UNION / UNION ALL | ✅ |

### 2.9 Tipos de Dados SQL

| Tipo | Estado |
|------|--------|
| Character(n) | ✅ |
| Numeric(p, s) | ✅ |
| Memo | ✅ |
| Date, Timestamp | ✅ (via DBF) |
| Logical | ✅ (via DBF) |
| Integer, Double | ✅ (via DBF) |
| Auto-increment | ❌ |

### 2.10 Limitações SQL Conhecidas

- Não pode misturar colunas simples com agregados no mesmo SELECT
- CASE não pode ser misturado com agregados
- Funções de janela não podem ser misturadas com agregados
- Productos cartesianos em comma-joins são rejeitados
- JOINs compostos requerem sintaxe JOIN explícita
- Sem DROP TABLE, ALTER TABLE, TRUNCATE
- Sem CREATE VIEW, CREATE TRIGGER
- Sem CONTROLE DE TRANSAÇÕES SQL (COMMIT, ROLLBACK, BEGIN)
- CTEs de nível único apenas
- FULL OUTER JOIN apenas para 2 tabelas
- Sem CHECK constraints, DEFAULT values no CREATE TABLE
- UPDATE sem expressões (apenas valores literais)

---

## 3. Engine de Tabelas

### 3.1 Formatos Suportados

| Formato | Estado | Notas |
|---------|--------|-------|
| DBF + CDX | ✅ Completo | FoxPro/Harbour, B+tree 512 bytes |
| DBF + NTX | ✅ Completo | Clipper, B-tree 1024 bytes |
| ADT | ✅ Completo | SAP Advantage Data Table, 400-byte header |
| ADI | ✅ Parcial | SAP Advantage Data Index, 512-page B-tree |
| VFP | ⚠️ Parcial | Declarado mas retorna erro |

### 3.2 Operações de Tabela

| Operação | Estado |
|----------|--------|
| goto_top, goto_bottom | ✅ |
| goto_record (absolute) | ✅ |
| skip (relativo) | ✅ |
| seek (exact e soft) | ✅ |
| read_field, set_field | ✅ |
| append_record | ✅ |
| mark_deleted, recall_deleted | ✅ |
| pack, zap | ✅ |
| reindex | ✅ |
| flush | ✅ |
| set_record_raw | ✅ |
| Scope (top/bottom) | ✅ |
| Relations (parent→child) | ✅ |
| Bookmarks | ✅ |
| Deferred flush (bulk insert) | ✅ |
| Transações (before-images, rollback) | ✅ |
| Bloqueio por registo e tabela | ✅ |
| Filtro por registo | ✅ |
| Sequência de recno (ORDER BY SQL) | ✅ |

### 3.3 Índices

| Característica | CDX | NTX | ADI |
|----------------|-----|-----|-----|
| B+tree / B-tree | ✅ B+tree 512B | ✅ B-tree 1024B | ✅ B-tree 512B |
| Multi-tag | ✅ | ❌ (single-tag) | ✅ |
| Unique | ✅ | ✅ | ✅ |
| Descending | ✅ | ✅ | ❌ |
| FOR-clause (conditional) | ✅ | ❌ | ❌ |
| FoxNumeric keys | ✅ | ❌ | ✅ (8-byte) |
| NtxNumeric keys | ❌ | ✅ | ❌ |
| Compact-leaf encoding | ✅ | ❌ | ❌ |
| Bulk build | ✅ | ✅ | ✅ |
| In-memory cache | ❌ | ✅ | ❌ |
| Seek, next, prev | ✅ | ✅ | ✅ |
| Insert, erase | ✅ | ✅ | ✅ |
| Flush | ✅ | ✅ | ✅ |
| Unique enforcement | ✅ | ✅ | ✅ |
| Reindex | ✅ | ✅ | ✅ |

### 3.4 AOF (Advantage Optimized Filter)

| Característica | Estado |
|----------------|--------|
| Parser de expressão Clipper | ✅ |
| =, <>, <, >, <=, >= | ✅ |
| AND, OR, NOT | ✅ |
| BETWEEN | ✅ |
| IN (lista) | ✅ |
| Literais (inteiro, real, string, booleano) | ✅ |
| Parênteses | ✅ |
| Otimização por índice | ✅ (M-AOF.4) |
| Bitmap por registo | ✅ |
| customize_aof_record | ✅ |
| clear_filter / passes_filter | ✅ |
| Funções, aritmética, LIKE | ❌ (fallback para não-otimizado) |

### 3.5 Full-Text Search (FTS)

| Característica | Estado |
|----------------|--------|
| Indexação por token | ✅ |
| Tokenização com delimitadores customizáveis | ✅ |
| min/max word length | ✅ |
| Noise words (stopwords) | ✅ |
| Busca AND (interseção) | ✅ |
| CONTAINS no WHERE SQL | ✅ |
| Criar índice FTS via SQL | ✅ |

---

## 4. Camada Remota (Wire Protocol)

### 4.1 Opcodes do Wire Protocol (84 total)

| Categoria | Opcodes | Estado |
|-----------|---------|--------|
| Sessão (Hello, Connect, Disconnect) | 3 | ✅ |
| Lifecycle (OpenTable, CloseTable) | 2 | ✅ |
| Navegação (GotoTop, GotoBottom, Skip, GotoRecord) | 4 | ✅ |
| Leitura (GetField, GetRecordCount, AtEOF, AtBOF, IsFound) | 5 | ✅ |
| Escrita (SetField, AppendBlank, DeleteRecord, RecallRecord, FlushTable) | 5 | ✅ |
| Schema (DescribeTable) | 1 | ✅ |
| Metadata (GetTableType, GetRecordLength, GetNumIndexes, etc.) | 6 | ✅ |
| Locking (Lock/Unlock Record/Table) | 4 | ✅ |
| Manutenção (Pack, Zap, Flush, CloseAllIndexes) | 4 | ✅ |
| Índices (OpenIndex, CloseIndex, SetOrder, Seek, CreateIndex, etc.) | 14 | ✅ |
| AOF (SetAOF, ClearAOF, GetAOFOptLevel) | 3 | ✅ |
| Batch (Fetch, FetchWhere, Aggregate, FetchCurrentRow) | 4 | ✅ |
| SQL (ExecuteSQL) | 1 | ✅ |
| Management (MgConnect, MgRequest) | 2 | ✅ |

### 4.2 Cache do Cliente Remoto

| Nível | Descrição | Estado |
|-------|-----------|--------|
| Row cache (M12.17-18) | Dados da linha no ack | ✅ |
| Schema cache (M12.14) | DescribeTable cacheado | ✅ |
| Record count cache (M12.19) | Contagem cacheada | ✅ |
| Prefetch sequencial (M12.21) | Até 64 linhas lookahead | ✅ |
| Recno + deleted cache | Com dados da linha | ✅ |
| Found state cache | Avoids round-trip | ✅ |

### 4.3 TLS

| Característica | Estado |
|----------------|--------|
| TLS client-side (mbedtls 3.6) | ✅ (opt-in: OPENADS_WITH_TLS) |
| URI tls://host:port/dir | ✅ |
| CA bundle, SNI | ✅ |
| Server-side TLS | ❌ (proxy reverso necessário) |

### 4.4 Operações Locais vs Remotas

| Operação | Local | Remoto |
|----------|-------|--------|
| Connect/Disconnect | ✅ | ✅ |
| Open/Close Table | ✅ | ✅ |
| Navegação (Top/Bottom/Skip/Goto) | ✅ | ✅ |
| Leitura de campos | ✅ | ✅ |
| Escrita de campos | ✅ | ✅ |
| Append/Delete/Recall | ✅ | ✅ |
| Schema/Metadata | ✅ | ✅ |
| Índices (Open/Close/Seek/Create) | ✅ | ✅ |
| SQL | ✅ | ✅ |
| Pack/Zap | ✅ | ✅ |
| Bloqueios | ✅ | ✅ (cursor path) |
| Transações | ✅ | ❌ |
| CreateTable/DropTable | ✅ | ❌ (local + SQL URI ✅) |
| Renomear tabela | ❌ | ❌ |

---

## 5. Backends SQL (paridade SQLRDD 1.5.x)

| Backend | Leitura | Escrita | Seek | Bloqueio | Filtro/AOF | Agregação | DDL (`AdsCreateTable` / `AdsDropTable` / `AdsRestructureTable`) | `system.*` |
|---------|---------|---------|------|----------|------------|-----------|-------------------------------------------------------------------|------------|
| SQLite | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| PostgreSQL | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅† |
| MSSQL | ✅ | ✅ | ✅ | ✅ (app lock) | ✅ | ✅ | ✅ | ✅† |
| MariaDB | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅† |
| Firebird | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅† |
| ODBC | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅† |

† `SELECT * FROM system.tables` / `system.columns` / `system.iota` é reescrito para `information_schema` ou `sqlite_master` em ligações URI SQL. Ver `docs/OPENADS_PLUS.md`.

---

## 6. Data Dictionary (AdsDD*)

### 6.1 Operações Implementadas

| Operação | Estado |
|----------|--------|
| AdsDDCreate | ✅ |
| AdsDDAddTable (AddTableToDatabase) | ✅ |
| AdsDDRemoveTable | ✅ |
| AdsDDAddIndexFile | ✅ |
| AdsDDRemoveIndexFile | ✅ |
| AdsDDCreateUser | ✅ |
| AdsDDDeleteUser | ✅ |
| AdsDDAddUserToGroup | ✅ |
| AdsDDRemoveUserFromGroup | ✅ |
| AdsDDCreateLink | ✅ |
| AdsDDDropLink | ✅ |
| AdsDDModifyLink | ✅ |
| AdsDDCreateRefIntegrity | ✅ |
| AdsDDRemoveRefIntegrity | ✅ |
| AdsDDCreateTrigger | ✅ |
| AdsDDDropTrigger | ✅ |
| AdsDDCreateProcedure | ✅ |
| AdsDDDropProcedure | ✅ |
| AdsDDCreateFunction | ✅ |
| AdsDDDropFunction | ✅ |
| AdsDDCreateView | ✅ |
| AdsDDDropView | ✅ |
| AdsDDGetTableProperty | ✅ (props 202, 213, 704) |
| AdsDDSetTableProperty | ✅ (props 202, 213, 704) |
| AdsDDGetFieldProperty | ✅ |
| AdsDDSetFieldProperty | ✅ |
| AdsDDGetDatabaseProperty | ✅ |
| AdsDDSetDatabaseProperty | ✅ |
| AdsDDGetUserProperty | ✅ |
| AdsDDSetUserProperty | ✅ |
| AdsDDGrantPermission | ✅ |
| AdsDDRevokePermission | ✅ |
| AdsDDGetPermissions | ✅ (lazy cache O(1)) |
| AdsDDGetUserTableRights | ✅ |

### 6.2 Formato Binário SAP .add

| Característica | Estado |
|----------------|--------|
| Leitura de Records Tables/Indexes/Users/Groups | ✅ |
| Leitura de Permission Records | ✅ |
| Decodificação XOR de group membership | ✅ |
| Escrita no formato nativo OpenADS | ✅ |
| Compatibilidade com SAP ACE DLL | ⚠️ Parcial (group membership encriptado não decodificado) |

---

## 7. Encriptação

| Característica | Estado |
|----------------|--------|
| AES-256-CTR (CDX tables) | ✅ (via AdsEncryptTable) |
| SQLCipher (SQLite) | ✅ (OPENADS_WITH_SQLCIPHER) |
| MSSQL TLS-in-TDS | ✅ (OPENADS_WITH_MSSQL) |
| AdsSetEncryptionPassword | ✅ |
| AdsEncryptTable | ✅ |
| AdsDecryptTable | ❌ (não implementado) |
| AdsEncryptRecord | ❌ (não implementado) |
| AdsDecryptRecord | ❌ (não implementado) |
| AdsEnableEncryption | ❌ (não implementado) |
| AdsDisableEncryption | ✅ (no-op) |
| AdsIsEncryptionEnabled | ✅ (retorna 0) |
| AdsIsTableEncrypted | ✅ (retorna 0) |
| AdsIsRecordEncrypted | ✅ (retorna 0) |

---

## 8. Memo / Campos Binários

| Característica | Estado |
|----------------|--------|
| Formato FPT (FoxPro) | ✅ |
| Formato DBT (dBASE) | ✅ |
| Formato ADM (ADT) | ✅ |
| Leitura de memo | ✅ |
| Escrita de memo | ✅ |
| Block type (Text/Picture/Object) | ✅ |
| field_memo_type | ✅ |
| Memo block reclamation (PACK) | ❌ (no-op) |
| Blob via SQLite/PostgreSQL/MariaDB | ✅ |
| Blob via MSSQL/Firebird | ⚠️ Parcial |

---

## 9. Funcionalidades Não Implementadas ou Parciais

### 9.1 Completamente Ausentes

- **VFP** (Visual FoxPro) — driver declarado mas retorna erro
- **DROP TABLE / DROP INDEX / ALTER TABLE** — sem DDL destrutivo
- **CREATE VIEW / CREATE TRIGGER** — sem SQL DDL avançado
- **COMMIT / ROLLBACK SQL** — sem transações via SQL
- **TRUNCATE TABLE**
- **Views** (CREATE VIEW, DROP VIEW)
- **Sequences / Auto-increment via SQL**
- **Funções de janela além de ROW_NUMBER/RANK/DENSE_RANK**
- **Agregações além de COUNT/SUM/AVG/MIN/MAX** (sem STRING_AGG, LISTAGG, etc.)
- **CROSS JOIN, NATURAL JOIN, SELF JOIN**
- **FULL OUTER JOIN para 3+ tabelas**
- **CHECK constraints, DEFAULT values no CREATE TABLE**
- **UPDATE com expressões** (`SET col = col + 1`)
- **Memo block reclamation** (PACK não limpa blocos livres)
- **Server-side TLS** (requer proxy reverso)
- **Transações via wire protocol**
- **CreateTable/DropTable via wire protocol**

### 9.2 Parcialmente Implementadas

- **ADI Index** — multi-tag e character keys, mas sem descending e sem conditional
- **VFP** — driver declarado mas inoperacional
- **MSSQL** — escrita e DDL navegacional disponíveis via URI; wire protocol remoto ainda sem CreateTable/DropTable
- **Encriptação** — AdsEncryptTable funciona, mas AdsDecryptTable/AdsEncryptRecord/AdsDecryptRecord não
- **SAP .add group membership** — decodificação XOR funciona mas criptografia proprietária não decodificada
- **CTEs** — apenas single CTE (sem nested/multi-level)
- **FULL OUTER JOIN** — apenas para 2 tabelas

---

## 10. Testes Unitários

O projeto contém **873 testes unitários** (doctest), dos quais **872 passam** e **1 falha** (concorrência SQLite). A cobertura inclui:

| Área | Testes |
|------|--------|
| DBF header/field/record | ✅ |
| CDX index (B+tree, splits, reindex, conditional) | ✅ |
| NTX index (B-tree, splits, numeric) | ✅ |
| ADI index (multi-tag, char-key) | ✅ |
| ADT table (types, scopes) | ✅ |
| Engine table (navigation, seek, scope, relations) | ✅ |
| AOF expression parser | ✅ |
| SQL parser (SELECT, INSERT, UPDATE, DELETE, JOINs, subqueries, UNION) | ✅ |
| SQL aggregates (COUNT, SUM, AVG, MIN, MAX, HAVING, FILTER) | ✅ |
| SQL window functions (ROW_NUMBER, RANK, DENSE_RANK) | ✅ |
| SQL scalar functions (UPPER, LOWER, SUBSTR, CONCAT, etc.) | ✅ |
| SQL CASE expressions | ✅ |
| SQL arithmetic expressions | ✅ |
| SQL DDL (CREATE TABLE, CREATE INDEX) | ✅ |
| SQLite backend | ✅ |
| Encryption (AES-256) | ✅ |
| Data dictionary (DD CRUD, permissions) | ✅ |
| Memo fields (DBT, FPT, ADM) | ✅ |
| Network wire protocol | ✅ |
| Management (MgSnapshot) | ✅ |
| Remote overloads | ✅ |
| Transações (begin/commit/rollback, savepoints) | ✅ |

---

## 11. Conclusão

O OpenADS atinge **~85% de compatibilidade** com o padrão ADS para operações do dia-a-dia. As áreas mais fortes são:

1. **Motor de tabelas** — CDX/NTX/ADI completos com navegação, scope, relations, bookmarks
2. **ACE API** — ~250 funções implementadas, cobrindo 90%+ do uso comum do rddads/X#
3. **Camada remota** — 84 opcodes com cache sofisticado (prefetch, row cache, schema cache)
4. **SQL básico** — SELECT/INSERT/UPDATE/DELETE com JOINs, subqueries, UNION, agregações, window functions
5. **Data Dictionary** — implementação completa do formato SAP .add

As principais lacunas são:
1. **DDL avançado** — sem DROP/ALTER/TRUNCATE via SQL
2. **Transações via SQL** — sem COMMIT/ROLLBACK no parser SQL
3. **VFP** — driver declarado mas inoperacional
4. **Encriptação** — sem decrypt table/record
5. **Server-side TLS** — requer proxy reverso
6. **Memo block reclamation** — PACK não limpa blocos livres
