---
title: Compat rddads / X# RDD
layout: default
parent: Inicio (ES)
nav_order: 7
permalink: /es/rddads-compat/
---

# Compatibilidad rddads / X# RDD

`ace64.dll` / `ace32.dll` de OpenADS es **reemplazo directo** del
Advantage Client Engine. Dos RDDs de terceros enlazan contra ella
por nombre:

- **Harbour `contrib/rddads`** — cubierto end-to-end desde
  v0.1.0-rc1.
- **`AXDBFCDX` de X#** (`ADSRDD.prg`) — cubierto local + sobre la
  red desde **v1.0.0-rc19** (M12.22 / M12.23).

## Cómo X# vincula OpenADS

`ADSRDD.prg` hace `LoadLibrary("ace32.dll")` y resuelve cada entry
point **por nombre** vía `GetProcAddress`. El RDD Advantage de X#
llama a una superficie mucho más amplia que `contrib/rddads`,
incluyendo overloads versionadas (`AdsCreateTable90`,
`AdsCreateIndex90`, etc.) que absorben los parámetros charset /
collation / page-size añadidos en builds recientes de ACE.

OpenADS exporta todos los nombres `Ads*NN` que X# busca; la
mayoría reenvían a la signatura base, unos cuantos son
accept-and-ignore para toggles que no aplican a una
implementación clean-room, y los genuinamente no-implementados
devuelven `AE_FUNCTION_NOT_AVAILABLE` para que el runtime de X#
caiga a su propio path cliente.

## M12.22 — overloads versionadas de ACE

| Export                          | Comportamiento |
|---------------------------------|----------------|
| `AdsConnect26`                  | Reenvía a `AdsConnect60`. |
| `AdsCreateTable71` / `AdsCreateTable90` | Reenvían a `AdsCreateTable` (descartan charset / collation / page-size). |
| `AdsOpenTable90`                | Reenvía a `AdsOpenTable80`. |
| `AdsCreateIndex90`              | Reenvía a `AdsCreateIndex61` tras re-mapear flags. |
| `AdsDDAddTable90`               | Reenvía a `AdsDDAddTable`. |
| `AdsDDCreateRefIntegrity62`     | Reenvía a `AdsDDCreateRefIntegrity`. |
| `AdsFindFirstTable62` / `AdsFindNextTable62` | Reenvían a base. |
| `AdsGetDateFormat60`            | Reenvía a `AdsGetDateFormat`. |
| `AdsGetExact22`                 | Reenvía a `AdsGetExact`. |
| `AdsReindex61`                  | Reenvía a `AdsReindex`. |
| `AdsRestructureTable90`         | Reenvía a `AdsRestructureTable`. |
| `AdsGetBookmark60` / `AdsGotoBookmark60` | Round-trip del recno como blob de 4 bytes. |
| `AdsCancelUpdate90` / `AdsSetProperty90` | No-ops aceptados. |
| `AdsFindConnection25` / `AdsGetTableHandle25` | Reportan not-found — OpenADS indexa por handle, no por path / nombre. |

## M12.23 — cierre del gap de exports X#

Una ejecución viva de `AXDBFCDX` contra la DLL OpenADS surfaceó
~45 entry points más que `ADSRDD.prg` busca. Comportamiento:

- **Field setters** (`AdsSetField`, `AdsSetEmpty`, `AdsSetNull`,
  `AdsSetShort`, `AdsSetMoney`, `AdsSetTime`, `AdsSetTimeStamp`)
  — todos manejan el idiom ACE "nombre de campo *o* ordinal
  1-based cast a puntero" (el `_FieldSub` de X# llama a
  `AdsGetFieldType` / `Length` / `Decimals` por ordinal, pasando
  un valor de puntero pequeño que el código viejo
  desreferenciaba como string).
- **Field readers** (`AdsGetDate`, `AdsGetMemoBlockSize`,
  `AdsGetTableOpenOptions`, `AdsGetBookmark`) —
  implementaciones reales.
- **Helpers de cursor** (`AdsCancelUpdate`, `AdsContinue`,
  `AdsEval*Expr`) — `AdsCancelUpdate` es accept-and-ignore;
  los demás devuelven `AE_FUNCTION_NOT_AVAILABLE` y X# cae al
  fallback.
- **Toggles RI / unique / autoinc** — accept-and-ignore (la
  aplicación real ocurre vía `AdsCreateIndex` / DD).
- **Helpers `AdsStmt*`** — `AE_FUNCTION_NOT_AVAILABLE`; la
  superficie SQL de X# los rodea.

## Fixes de semántica que llegaron con M12.23

Parecían "exports faltantes" desde X#, pero eran comportamiento
incorrecto en entries existentes:

- **`AdsAppendRecord` auto-bloquea el nuevo registro.** Semántica
  ACE para tablas no exclusivas — el `GoHot` de X# rehúsa
  escribir un registro que ve como no bloqueado.
- **`AdsIsRecordLocked` / `AdsLockRecord` / `AdsUnlockRecord`
  honran `recno == 0` = registro actual** y reportan el estado
  real del lock en vez de devolver `0`.
- **Fix bit de opción `AdsCreateIndex61` / `AdsCreateIndex90`.**
  El flag "descending" es `ADS_DESCENDING` (`0x08`), no `0x02` —
  `0x02` es `ADS_COMPOUND`, que el ADSRDD de X# siempre setea
  para órdenes CDX, así que la máscara vieja construía cada
  orden descendente y `DbGoTop` aterrizaba en la última key.
- **`AdsCreateTable` / `AdsCreateTable90` crean un `.fpt` vacío
  junto al `.dbf`** cuando la lista de campos tiene `M` (usando
  `usMemoBlockSize`, default 64). Sin él
  `Connection::open_table` no puede adjuntar memo store y todo
  write de memo falla "memo store not attached".

## X# remoto (M12.23, rc19)

Tres fixes más para que ADSRDD de X# pilote `openads_serverd` por
la red (`AdsConnect60("tcp://host:puerto/<datadir>",
ADS_REMOTE_SERVER) → AX_SetConnectionHandle → DbUseArea`):

- `remote_field_index` honra el idiom "nombre de campo O ordinal
  1-based cast a puntero" — igual que el lado local.
- El branch remoto de `AdsOpenTable` añade `.dbf` por defecto si
  falta la extensión (X# pasa el nombre pelado para tablas
  remotas).
- `AdsGetTableFilename` adquirió ruta remota (devuelve el nombre
  abierto) en lugar de fallar `AE_INTERNAL_ERROR` — el `Open`
  de X# lo llama justo tras `_FieldSub`.

## Harness de tests

```
tests/smoke/xsharp/
├── AdsSmoke.prg          # local: ace64.dll directo
└── AdsSmoke_remote.prg   # remoto: openads_serverd por tcp://
```

Ambos pasan end-to-end. Doctest:
`tests/abi_versioned_overloads_test.cpp` (local) y
`tests/abi_remote_overloads_test.cpp` (sobre red, gated en
`-DOPENADS_TEST_REMOTE=ON`).

## Follow-ups rc20+ desde feedback X#

- **rc21 / M12.24** — `AdsGetLastTableUpdate` con signatura real
  matching ACE; `AdsSetDateFormat` ya no es no-op; `AdsSetAOF`
  retorna éxito + `ADS_OPTIMIZED_NONE` en expresiones no
  optimizables.
- **rc22 / M12.25** — `AdsCreateTable` estampa la fecha de hoy
  en el header DBF, así que un create+open fresco reporta hoy
  en vez de `1900-00-00` hasta el primer `DbAppend`.
