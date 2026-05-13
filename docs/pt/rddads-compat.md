---
title: Compat rddads / X# RDD
layout: default
parent: Início (PT)
nav_order: 7
permalink: /pt/rddads-compat/
---

# Compatibilidade rddads / X# RDD

`ace64.dll` / `ace32.dll` do OpenADS é **substituto direto** do
Advantage Client Engine. Dois RDDs de terceiros linkam contra ela
por nome:

- **Harbour `contrib/rddads`** — coberto end-to-end desde
  v0.1.0-rc1.
- **`AXDBFCDX` do X#** (`ADSRDD.prg`) — coberto local + sobre a
  rede desde **v1.0.0-rc19** (M12.22 / M12.23).

## Como o X# vincula o OpenADS

`ADSRDD.prg` faz `LoadLibrary("ace32.dll")` e resolve cada entry
point **por nome** via `GetProcAddress`. O RDD Advantage do X#
chama uma superfície bem mais ampla que `contrib/rddads`,
incluindo overloads versionados (`AdsCreateTable90`,
`AdsCreateIndex90`, etc.) que absorvem os parâmetros charset /
collation / page-size que builds recentes do ACE adicionaram.

OpenADS exporta todos os nomes `Ads*NN` que X# busca; a maioria
reencaminha para a assinatura base, alguns são accept-and-ignore
para toggles que não aplicam a uma implementação clean-room, e os
genuinamente não-implementados retornam
`AE_FUNCTION_NOT_AVAILABLE` para que o runtime do X# caia no seu
próprio path cliente.

## M12.22 — overloads versionados de ACE

| Export                          | Comportamento |
|---------------------------------|---------------|
| `AdsConnect26`                  | Reencaminha para `AdsConnect60`. |
| `AdsCreateTable71` / `AdsCreateTable90` | Reencaminham para `AdsCreateTable` (descartam charset / collation / page-size). |
| `AdsOpenTable90`                | Reencaminha para `AdsOpenTable80`. |
| `AdsCreateIndex90`              | Reencaminha para `AdsCreateIndex61` após remapear flags. |
| `AdsDDAddTable90`               | Reencaminha para `AdsDDAddTable`. |
| `AdsDDCreateRefIntegrity62`     | Reencaminha para `AdsDDCreateRefIntegrity`. |
| `AdsFindFirstTable62` / `AdsFindNextTable62` | Reencaminham para base. |
| `AdsGetDateFormat60`            | Reencaminha para `AdsGetDateFormat`. |
| `AdsGetExact22`                 | Reencaminha para `AdsGetExact`. |
| `AdsReindex61`                  | Reencaminha para `AdsReindex`. |
| `AdsRestructureTable90`         | Reencaminha para `AdsRestructureTable`. |
| `AdsGetBookmark60` / `AdsGotoBookmark60` | Round-trip do recno como blob de 4 bytes. |
| `AdsCancelUpdate90` / `AdsSetProperty90` | No-ops aceitos. |
| `AdsFindConnection25` / `AdsGetTableHandle25` | Reportam not-found — OpenADS indexa por handle, não por path / nome. |

## M12.23 — fechar o gap de exports X#

Uma execução viva de `AXDBFCDX` contra a DLL OpenADS surfaceou
~45 entry points adicionais que `ADSRDD.prg` busca.
Comportamento:

- **Field setters** (`AdsSetField`, `AdsSetEmpty`, `AdsSetNull`,
  `AdsSetShort`, `AdsSetMoney`, `AdsSetTime`, `AdsSetTimeStamp`)
  — todos lidam com o idiom ACE "nome do campo *ou* ordinal
  1-based cast a ponteiro".
- **Field readers** (`AdsGetDate`, `AdsGetMemoBlockSize`,
  `AdsGetTableOpenOptions`, `AdsGetBookmark`) — implementações
  reais.
- **Helpers de cursor** (`AdsCancelUpdate`, `AdsContinue`,
  `AdsEval*Expr`) — `AdsCancelUpdate` é accept-and-ignore; os
  demais retornam `AE_FUNCTION_NOT_AVAILABLE` e X# cai no
  fallback.
- **Toggles RI / unique / autoinc** — accept-and-ignore (a
  aplicação real ocorre via `AdsCreateIndex` / DD).
- **Helpers `AdsStmt*`** — `AE_FUNCTION_NOT_AVAILABLE`; a
  superfície SQL do X# os contorna.

## Fixes de semântica que vieram com M12.23

Pareciam "exports faltantes" sob a ótica do X# RDD, mas eram
comportamento errado em entries existentes:

- **`AdsAppendRecord` auto-bloqueia o novo registro.** Semântica
  ACE para tabelas não-exclusivas — o `GoHot` do X# recusa
  escrever um registro que vê como não-bloqueado.
- **`AdsIsRecordLocked` / `AdsLockRecord` / `AdsUnlockRecord`
  respeitam `recno == 0` = registro atual** e reportam o estado
  real do lock em vez de devolver `0`.
- **Fix de bit de opção em `AdsCreateIndex61` /
  `AdsCreateIndex90`.** O flag "descending" é `ADS_DESCENDING`
  (`0x08`), não `0x02` — `0x02` é `ADS_COMPOUND`, que o ADSRDD
  do X# sempre seta para ordens CDX; a máscara antiga construía
  toda ordem como descendente e `DbGoTop` aterrissava na última
  key.
- **`AdsCreateTable` / `AdsCreateTable90` criam um `.fpt` vazio
  junto ao `.dbf`** quando a lista de campos tem `M` (usando
  `usMemoBlockSize`, default 64). Sem ele
  `Connection::open_table` não consegue anexar memo store e
  qualquer escrita de memo falha "memo store not attached".

## X# remoto (M12.23, rc19)

Três fixes adicionais para que ADSRDD do X# dirija
`openads_serverd` pela rede (`AdsConnect60("tcp://host:porta/
<datadir>", ADS_REMOTE_SERVER) → AX_SetConnectionHandle →
DbUseArea`):

- `remote_field_index` respeita o idiom "nome do campo OU
  ordinal 1-based cast a ponteiro" — mesmo do lado local.
- O branch remoto de `AdsOpenTable` aplica `.dbf` por padrão
  quando falta extensão (X# passa o nome puro para tabelas
  remotas).
- `AdsGetTableFilename` ganhou path remoto (devolve o nome
  aberto) em vez de falhar `AE_INTERNAL_ERROR` — o `Open` do
  X# o chama logo após `_FieldSub`.

## Harness de testes

```
tests/smoke/xsharp/
├── AdsSmoke.prg          # local: ace64.dll direto
└── AdsSmoke_remote.prg   # remoto: openads_serverd por tcp://
```

Ambos passam end-to-end. Doctest:
`tests/abi_versioned_overloads_test.cpp` (local) e
`tests/abi_remote_overloads_test.cpp` (sobre rede, gated por
`-DOPENADS_TEST_REMOTE=ON`).

## Follow-ups rc20+ vindos do feedback X#

- **rc21 / M12.24** — `AdsGetLastTableUpdate` com assinatura
  real matching ACE; `AdsSetDateFormat` deixa de ser no-op;
  `AdsSetAOF` retorna sucesso + `ADS_OPTIMIZED_NONE` em
  expressões não-otimizáveis.
- **rc22 / M12.25** — `AdsCreateTable` carimba a data de hoje no
  header DBF, então um create+open fresco reporta hoje em vez
  de `1900-00-00` até o primeiro `DbAppend`.
