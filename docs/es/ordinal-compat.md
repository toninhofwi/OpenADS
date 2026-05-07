---
title: Error "no se encontró ordinal NNN"
layout: default
parent: Inicio (ES)
nav_order: 8
permalink: /es/ordinal-compat/
---

# Solucionar "no se encontró el ordinal NNN en ace64.dll"

## Síntoma

Después de poner `ace64.dll` (o `ace32.dll`) de OpenADS en el
`PATH` de una aplicación existente, Windows muestra:

> **No se encontró el punto de entrada del procedimiento al
> ordinal 328 en la biblioteca de vínculos dinámicos
> `ace64.dll`.**

El número cambia (`328`, `415`, …). La aplicación aborta.

## Causa

La aplicación o su `rddads.lib` se enlazó contra una import
library `ace32.lib` / `ace64.lib` que **registra imports por
ordinal**, no por nombre. Cada `Ads*` vive en un ordinal
específico (1, 2, 3, …, 328, …) dentro del DLL SAP. El loader
de Windows busca ese ordinal en el nuevo DLL — y falla, porque
el `.def` upstream de OpenADS solo declara exports por nombre
y los ordinales se asignan automáticamente en orden de archivo
fuente. Los números no coinciden con los de SAP.

OpenADS implementa cada función `Ads*` clean-room — los
**nombres** son públicos (`contrib/rddads` de Harbour los llama
por nombre). Lo que falta es solo el **mapeo numérico**
ordinal → nombre. Ese mapeo vive en el DLL SAP que el usuario
ya posee legalmente; nunca leemos código fuente SAP. Un script
helper extrae el mapeo localmente en la máquina del usuario.

## Solución — una sola vez en el Win con DLL SAP

### 1. Volcar tabla de exports SAP

En *Developer Command Prompt*:

```bat
dumpbin /exports ace64.dll > ace64-exports.txt
```

### 2. Convertirlo a `.def` para OpenADS

```bat
python tools\scripts\sap_ordinals_to_def.py ace64-exports.txt > openads_ace_ordinals.def
```

### 3. Recompilar OpenADS con el `.def` custom

```bat
cmake -S . -B build\ord -DOPENADS_ACE_DEF=%CD%\openads_ace_ordinals.def
cmake --build build\ord --target openads_ace --config Release
```

El `build\ord\src\Release\ace64.dll` resultante mantiene cada
nombre de función + asigna el ordinal que SAP eligió. Drop-in
sobre el `PATH` de la app — los lookups por ordinal del loader
ya hitean el símbolo correcto.

## Camino alternativo recomendado

Si la aplicación se puede re-linkar, **no hace falta el truco
de ordinales**. Genera una import library desde el DLL de
OpenADS (que exporta por nombre):

```bat
lib /def:openads_ace.def /machine:x64 /out:ace64.lib
```

…y re-linka `rddads` y la app contra ese `ace64.lib`. Vincula
por nombre, sin acoplamiento a ordinales. Es la opción
future-proof.

## Por qué no enviamos `ace64-exports.txt` upstream

La tabla de exports es metadata sobre un binario que OpenADS
no envía y no posee. Re-publicarla dentro de este repo
significaría redistribuir datos extraídos de un binario SAP,
lo que choca con la política clean-room. Cada usuario genera
su copia local desde un binario que ya tiene derecho legal a
inspeccionar.
