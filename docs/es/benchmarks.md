---
title: Benchmarks
layout: default
parent: Inicio (ES)
nav_order: 5
permalink: /es/benchmarks/
---

# Benchmarks

`tools/bench/openads_bench` genera un DBF sintético de 100 000
filas (`ID N(8,0)`, `TAG C(4)`, `AMT N(8,2)`) y mide un conjunto
fijo de cargas SQL a través de la ABI pública
(`AdsExecuteSQLDirect`). Mediana de 5 repeticiones por carga,
builds `Release`.

## v0.4.x — SQL local in-process (2026-05-06)

| Carga (mediana ms)     | Windows MSVC | Linux clang -O3 | macOS AppleClang |
|------------------------|-------------:|----------------:|-----------------:|
| crear DBF 100 k filas  |        63.5  |          57.9   |           34.0   |
| `SELECT COUNT(*)`      |       297.7  |          42.0   |          103.9   |
| `WHERE TAG = 'AAAA'`   |       303.7  |          48.3   |          108.4   |
| `SUM/AVG/MIN/MAX(AMT)` |       374.3  |         120.5   |          136.1   |
| `GROUP BY TAG`         |       321.9  |          58.6   |          120.9   |
| `ORDER BY AMT LIMIT 10`|       668.0  |         165.4   |          260.5   |
| `DISTINCT TAG`         |       598.4  |          95.2   |          213.4   |
| `BETWEEN 100 AND 500`  |       314.1  |          63.7   |          114.4   |

Linux clang -O3 gana en todas las cargas SQL — aproximadamente
7× más rápido que MSVC Release en el COUNT de tabla completa,
4× en el `ORDER BY` más pesado. macOS Intel queda en medio.

## Bench v2 — cargas con índices (Windows MSVC, 100 k filas)

| Carga (mediana ms)      | ms |
|-------------------------|---:|
| `CREATE INDEX ID_IDX`   | 38.0 |
| `WHERE ID = 50000` (post-índice)        | 308.0 |
| `WHERE ID BETWEEN 10000 AND 20000`      | 308.2 |
| `UNION ALL` de dos selects filtradas    | 608.2 |
| `GROUP BY TAG HAVING COUNT(*) > 100`    | 0.2 |

Que `indexed_eq` ~308 ms ≈ `seq_walk_where` ~315 ms expone una
oportunidad conocida: el planner SQL actualmente NO empuja los
predicados WHERE a un índice CDX/NTX coincidente. Cerrar esa
brecha es un milestone futuro.

## Bench v3 — AOF (Rushmore-style) (rc12, 100 k filas)

`AdsSetAOF` parsea + evalúa la condición, instala un bitmap por
registro como predicado de filtro que `Skip` / `GoTop` honran, y
enruta cada hoja por range-scan de CDX / NTX cuando un índice
abierto tiene ese campo como key expr. `AdsGetAOFOptLevel` reporta
`ADS_OPTIMIZED_FULL` / `PART` / `NONE` según cobertura.
La navegación con bitmap sparse (M-AOF.5) lleva el walk del
visible-set de O(N) a O(M).

Mismo DBF sintético de 100 000 filas, todas medianas de 5
repeticiones, builds `Release`:

| Carga AOF                                          | Win MSVC x64 | Linux clang -O3 | macOS AppleClang | OptLevel |
|----------------------------------------------------|-------------:|----------------:|-----------------:|----------|
| `AdsSetAOF("TAG='AAAA'")`, sin índice TAG          |   593 ms     |     93 ms       |    210 ms        | NONE     |
| `AdsSetAOF("TAG='AAAA'")`, con índice TAG          |   323 ms     |     58 ms       |    119 ms        | FULL     |
| `AdsSetAOF("TAG BETWEEN 'AAAA' AND 'CCCC'")`, idx  |    24 ms     |    4.5 ms       |      9 ms        | FULL     |

Speedup vs baseline full-scan no-indexada (mismo host):

| Carga AOF                                          | Win MSVC | Linux clang | macOS |
|----------------------------------------------------|---------:|------------:|------:|
| `AdsSetAOF("TAG='AAAA'")`, con índice TAG          |   1.83×  |    1.61×    | 1.77× |
| `AdsSetAOF("TAG BETWEEN 'AAAA' AND 'CCCC'")`       |  24.4×   |   20.7×     | 23.4× |

Qué provoca la aceleración:

1. `AdsSetAOF` se convierte en range-scan sobre el índice en
   lugar de decode + AST eval por fila. Ganancia rc11 (M-AOF.4).
2. `Skip` / `GoTop` sólo recorren los registros que pasan
   (navegación sparse, M-AOF.5) en vez de iterar cada recno
   consultando el predicado. Ganancia rc12 — la ventana
   "10-100×" tipo Rushmore para filtros selectivos.

El speedup de ~1.83× del eq-walk está limitado por el coste de
`load_record_` por registro visible (~80 µs × 3848 matches ≈
310 ms suelo en Windows). Aplicaciones que no tocan el dato
matched — `COUNT(*)` sobre el AOF, o `dbSeek` puntual — entran
en la ventana Rushmore completa encima del range-scan gain.

## Bench v4 — repaint xbrowse sobre el wire (rc18)

El stack wire (rc18, M12.17 .. M12.20) trae cuatro optimizaciones
solapadas que llevan el coste de un PgDn estilo xbrowse —
W columnas × H filas × metadata por fila — de ~300 round-trips a
~20 RTT × ~5 ms con Nagle off ≈ ~100 ms total.

Medición end-to-end LAN, `openads_serverd` en la misma subred,
100 columnas × 25 filas visibles sobre `tcp://`:

| Etapa                                              | RTT  | Nota |
|----------------------------------------------------|-----:|------|
| baseline pre-M12.17 (`FieldGet` por celda)         | ~300 | W celdas por fila + metadata por fila, cada una con su round-trip. |
| **M12.17** — cache de fila `FetchCurrentRow`        | ~80  | W celdas por fila → 1 RTT. |
| **M12.18** — nav-ack lleva el trailer de fila       | ~20  | Acks `GotoTop` / `Skip` / `GotoRecord` empaquetan row buffer + recno + flag deleted, así `AdsGetField` / `AdsGetRecordNum` / `AdsIsRecordDeleted` pegan en la cache del nav previo. |
| **M12.19** — cache de record-count                  | ~20  | `AdsGetRecordCount` / `AdsGetRelKeyPos` (la scrollbar) sirven de cache; invalidados solo por writes que cambien el count. |
| **M12.20** — `TCP_NODELAY`                          | ~100 ms | Nagle off — ping-pong estricto quita el tax de 200 ms de acumulación. |

**Neto**: ~30× speedup end-to-end vs pre-M12.17 en LAN típica.

## Ejecutar en tu hardware

```sh
cmake --build build/default --target openads_bench --config Release
./build/default/tools/bench/openads_bench --rows 100000 --repeats 5 --csv
```

El flag `--csv` emite una fila CSV por carga.
