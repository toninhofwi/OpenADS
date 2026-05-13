---
title: Arquitetura
layout: default
parent: Início (PT)
nav_order: 2
permalink: /pt/arquitetura/
---

# Arquitetura

OpenADS é um sistema de cinco camadas. Cada camada é um ponto
de troca que uma aplicação ou teste pode usar
independentemente.

```
L1  ABI               exports extern "C" Ads*
                      (ace32/64.dll, libace.so/.dylib)
L2  Sessão            Connection / Statement / HandleRegistry / Tx
L3  Motor SQL         Lexer → Parser → Resolver → Planner → Executor
                      AEP host, UDFs xBase
L4  Núcleo motor      Table / Index / MemoStore / Cursor / LockMgr
                      TxLog (WAL) / Catalog
L5  Plataforma        File / mmap / locks por intervalo / sockets
                      Implementações Win32 + POSIX
```

## Responsabilidades por camada

| Camada | Responsabilidade |
|--------|------------------|
| **L1** | Único módulo com ABI C. Traduz chamadas `Ads*` para a API C++ interna; converte códigos de erro ACE de/para `util::Error`; conversões OEM / ANSI / UTF-8 / UTF-16. |
| **L2** | Estado por conexão — tabelas abertas, declarações SQL preparadas, pilha de transações, registro de procedimentos AEP, chave de criptografia. |
| **L3** | Dialeto SQL completo do Advantage — árvores WHERE booleanas, joins (INNER / LEFT / RIGHT / FULL OUTER), subqueries (correlacionadas e não), GROUP BY + HAVING, UNION, funções janela, CTEs, CASE, projeção escalar / agregada / aritmética. |
| **L4** | Motor agnóstico ao formato — `Table`, `Index`, `MemoStore`, `Cursor`, `LockMgr`, `TxLog`, `Catalog`. O trait `Driver` é o ponto de extensão para novos formatos. |
| **L5** | Abstração multi-plataforma do SO (Win32 + POSIX). |

## Drivers (extensão L4)

```
AdtDriver    .adt + .adm + .adi    (ADS proprietário — fora de escopo)
CdxDriver    .dbf + .cdx + .fpt    (FoxPro)
NtxDriver    .dbf + .ntx + .dbt    (Clipper)
VfpDriver    .dbf + .cdx + .fpt    (Visual FoxPro)
```

## Servidor daemon

`openads_serverd` roda L2–L5 in-process e os expõe via o
protocolo de fio nativo OpenADS sobre TCP (`tcp://` em claro ou
`tls://` com TLS, desde v0.4.0). A mesma DLL que fala com um
diretório local também fala com um servidor remoto via URI
`tcp://host:porta/<dir>`.

## Studio (console web)

`OPENADS_WITH_HTTP=ON` é o padrão de build desde v1.0.0-rc20.
A mesma SPA Studio é servida por dois hosts:

- **Modo Remote Server** — embutida em `openads_serverd.exe`,
  servindo o wire e o HTTP em paralelo.
- **Modo LocalServer** (desde v1.0.0-rc9) — embutida em
  `ace64.dll` / `ace32.dll`. Uma app Harbour / X# / Clipper que
  carregue a DLL recebe o console Studio dentro do próprio
  processo. Três exports exclusivos controlam: `AdsStudioStart`
  / `Stop` / `Port`, mais auto-start por
  `OPENADS_STUDIO_PORT` desde `DllMain`. O header do Studio
  traz um badge de modo (desde rc10) que distingue os dois
  modos via campo `mode` de `/api/health`.

Cada requisição REST abre uma conexão ABI curta — o console é
**outro consumidor do ABI público**, igual a uma app Harbour.
