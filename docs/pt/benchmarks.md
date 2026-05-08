---
title: Benchmarks
layout: default
parent: Início (PT)
nav_order: 5
permalink: /pt/benchmarks/
---

# Benchmarks

`tools/bench/openads_bench` gera um DBF sintético de 100 000
linhas (`ID N(8,0)`, `TAG C(4)`, `AMT N(8,2)`) e cronometra um
conjunto fixo de cargas SQL através da ABI pública
(`AdsExecuteSQLDirect`). Mediana de 5 repetições por carga,
builds `Release`.

## Resultados v0.4.x (2026-05-06)

| Carga (mediana ms)     | Windows MSVC | Linux clang -O3 | macOS AppleClang |
|------------------------|-------------:|----------------:|-----------------:|
| criar DBF 100 k linhas |        63.5  |          57.9   |           34.0   |
| `SELECT COUNT(*)`      |       297.7  |          42.0   |          103.9   |
| `WHERE TAG = 'AAAA'`   |       303.7  |          48.3   |          108.4   |
| `SUM/AVG/MIN/MAX(AMT)` |       374.3  |         120.5   |          136.1   |
| `GROUP BY TAG`         |       321.9  |          58.6   |          120.9   |
| `ORDER BY AMT LIMIT 10`|       668.0  |         165.4   |          260.5   |
| `DISTINCT TAG`         |       598.4  |          95.2   |          213.4   |
| `BETWEEN 100 AND 500`  |       314.1  |          63.7   |          114.4   |

Linux clang -O3 vence em todas as cargas SQL — aproximadamente
7× mais rápido que MSVC Release no COUNT de tabela completa,
4× no `ORDER BY` mais pesado. macOS Intel fica no meio.

## Bench v2 — cargas com índices (Windows MSVC, 100 k linhas)

| Carga (mediana ms)      | ms |
|-------------------------|---:|
| `CREATE INDEX ID_IDX`   | 38.0 |
| `WHERE ID = 50000` (pós-índice)         | 308.0 |
| `WHERE ID BETWEEN 10000 AND 20000`      | 308.2 |
| `UNION ALL` de dois selects filtrados   | 608.2 |
| `GROUP BY TAG HAVING COUNT(*) > 100`    | 0.2 |

Que `indexed_eq` ~308 ms ≈ `seq_walk_where` ~315 ms expõe uma
oportunidade conhecida: o planner SQL atualmente NÃO empurra os
predicados WHERE para um índice CDX/NTX correspondente. Fechar
essa lacuna é um milestone futuro.

## Bench v3 — AOF (Rushmore-style) (rc12, 100 k linhas)

`AdsSetAOF` faz parse + evaluate da condição, instala um bitmap
por registro como predicado de filtro que `Skip` / `GoTop`
respeitam, e roteia cada folha por range-scan de CDX / NTX
quando um índice aberto tem o campo como key expr.
`AdsGetAOFOptLevel` reporta `ADS_OPTIMIZED_FULL` / `PART` /
`NONE` por cobertura. A navegação com bitmap sparse (M-AOF.5)
leva o walk do visible-set de O(N) para O(M).

Mesmo DBF sintético de 100 000 linhas, medianas de 5
repetições, builds `Release`:

| Carga AOF                                          | Win MSVC x64 | Linux clang -O3 | macOS AppleClang | OptLevel |
|----------------------------------------------------|-------------:|----------------:|-----------------:|----------|
| `AdsSetAOF("TAG='AAAA'")`, sem índice TAG          |   593 ms     |     93 ms       |    210 ms        | NONE     |
| `AdsSetAOF("TAG='AAAA'")`, com índice TAG          |   323 ms     |     58 ms       |    119 ms        | FULL     |
| `AdsSetAOF("TAG BETWEEN 'AAAA' AND 'CCCC'")`, idx  |    24 ms     |    4.5 ms       |      9 ms        | FULL     |

Speedup vs baseline full-scan não-indexada (mesmo host):

| Carga AOF                                          | Win MSVC | Linux clang | macOS |
|----------------------------------------------------|---------:|------------:|------:|
| `AdsSetAOF("TAG='AAAA'")`, com índice TAG          |   1.83×  |    1.61×    | 1.77× |
| `AdsSetAOF("TAG BETWEEN 'AAAA' AND 'CCCC'")`       |  24.4×   |   20.7×     | 23.4× |

O que provoca o speedup:

1. `AdsSetAOF` torna-se range-scan sobre o índice em vez de
   decode + AST eval por linha. Ganho rc11 (M-AOF.4).
2. `Skip` / `GoTop` percorrem só os registros que passam
   (navegação sparse, M-AOF.5) em vez de iterar cada recno
   consultando o predicado. Ganho rc12 — a janela "10-100×"
   tipo Rushmore para filtros seletivos.

O speedup de ~1.83× do eq-walk é limitado pelo custo de
`load_record_` por registro visível (~80 µs × 3848 matches ≈
310 ms piso no Windows). Aplicações que não tocam o dado
matched — `COUNT(*)` sobre o AOF, ou `dbSeek` pontual — entram
na janela Rushmore completa em cima do range-scan gain.

## Executar no seu hardware

```sh
cmake --build build/default --target openads_bench --config Release
./build/default/tools/bench/openads_bench --rows 100000 --repeats 5 --csv
```

O flag `--csv` emite uma linha CSV por carga.
