---
title: Arquitectura
layout: default
parent: Inicio (ES)
nav_order: 2
permalink: /es/arquitectura/
---

# Arquitectura

OpenADS es un sistema de cinco capas. Cada capa es un punto de
intercambio que una aplicación o test puede usar
independientemente.

```
L1  ABI               exports extern "C" Ads*
                      (ace32/64.dll, libace.so/.dylib)
L2  Sesión            Connection / Statement / HandleRegistry / Tx
L3  Motor SQL         Lexer → Parser → Resolver → Planner → Ejecutor
                      AEP host, UDFs xBase
L4  Núcleo motor      Table / Index / MemoStore / Cursor / LockMgr
                      TxLog (WAL) / Catalog
L5  Plataforma        File / mmap / locks por rango / sockets / DLL
                      Implementaciones Win32 + POSIX
```

## Responsabilidades por capa

| Capa | Responsabilidad |
|------|-----------------|
| **L1** | Único módulo con ABI C. Traduce llamadas `Ads*` a la API C++ interna; convierte códigos de error ACE a/desde `util::Error`; conversiones OEM / ANSI / UTF-8 / UTF-16. |
| **L2** | Estado por conexión — tablas abiertas, sentencias SQL preparadas, pila de transacciones, registro de procedimientos AEP, clave de cifrado. |
| **L3** | Dialecto SQL completo de Advantage — árboles WHERE booleanos, joins (INNER / LEFT / RIGHT / FULL OUTER), subqueries (correlacionadas y no), GROUP BY + HAVING, UNION, funciones ventana, CTEs, CASE, proyección escalar / agregada / aritmética. |
| **L4** | Motor agnóstico al formato — `Table`, `Index`, `MemoStore`, `Cursor`, `LockMgr`, `TxLog`, `Catalog`. El trait `Driver` es el punto de extensión para nuevos formatos. |
| **L5** | Abstracción multiplataforma del SO (Win32 + POSIX). |

## Drivers (extensión L4)

```
AdtDriver    .adt + .adm + .adi    (ADS propietario — fuera de alcance)
CdxDriver    .dbf + .cdx + .fpt    (FoxPro)
NtxDriver    .dbf + .ntx + .dbt    (Clipper)
VfpDriver    .dbf + .cdx + .fpt    (Visual FoxPro)
```

## Servidor daemon

`openads_serverd` corre L2–L5 in-process y los expone vía el
protocolo de cable nativo OpenADS sobre TCP (`tcp://` en claro
o `tls://` con TLS, desde v0.4.0). El mismo DLL que habla con
un directorio local también habla con un servidor remoto vía
URI `tcp://host:puerto/<dir>`.

## Studio (consola web)

`OPENADS_WITH_HTTP=ON` es el default de build desde v1.0.0-rc20.
La misma SPA Studio la sirven dos hosts:

- **Modo Remote Server** — embebida en `openads_serverd.exe`,
  sirviendo el wire y el HTTP en paralelo.
- **Modo LocalServer** (desde v1.0.0-rc9) — embebida en
  `ace64.dll` / `ace32.dll`. Una app Harbour / X# / Clipper que
  cargue la DLL obtiene la consola Studio en su propio proceso.
  Tres exports propios la controlan: `AdsStudioStart` /
  `Stop` / `Port`, más un auto-start por
  `OPENADS_STUDIO_PORT` desde `DllMain`. El header de Studio
  trae un badge de modo (desde rc10) que distingue ambos modos
  vía el campo `mode` de `/api/health`.

Cada request REST abre una conexión ABI corta — la consola es
**otro consumidor del ABI público**, igual que una app Harbour.
