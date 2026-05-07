---
title: Erro "ordinal NNN não encontrado"
layout: default
parent: Início (PT)
nav_order: 8
permalink: /pt/ordinal-compat/
---

# Resolver "o ordinal NNN não foi localizado em ace64.dll"

## Sintoma

Após colocar `ace64.dll` (ou `ace32.dll`) do OpenADS no `PATH`
de uma aplicação existente, o Windows exibe:

> **O ponto de entrada do procedimento no ordinal 328 não foi
> localizado na biblioteca de vínculos dinâmicos `ace64.dll`.**

O número varia (`328`, `415`, …). A aplicação aborta.

## Causa

A aplicação ou seu `rddads.lib` foi linkada contra uma import
library `ace32.lib` / `ace64.lib` que **registra imports por
ordinal**, não por nome. Cada `Ads*` vive num ordinal
específico (1, 2, 3, …, 328, …) dentro do DLL SAP. O loader
do Windows procura esse ordinal no novo DLL — e falha, porque
o `.def` upstream do OpenADS só declara exports por nome e
os ordinais são auto-atribuídos em ordem de arquivo fonte.
Os números não batem com os do SAP.

OpenADS implementa cada função `Ads*` clean-room — os
**nomes** são públicos (`contrib/rddads` do Harbour os chama
por nome). Falta apenas o **mapeamento numérico** ordinal →
nome. Esse mapeamento vive no DLL SAP que o usuário já
possui legalmente; nunca lemos código-fonte SAP. Um script
helper extrai o mapeamento localmente na máquina do usuário.

## Solução — uma única vez no Win com DLL SAP

### 1. Despejar tabela de exports SAP

No *Developer Command Prompt*:

```bat
dumpbin /exports ace64.dll > ace64-exports.txt
```

### 2. Converter em `.def` para OpenADS

```bat
python tools\scripts\sap_ordinals_to_def.py ace64-exports.txt > openads_ace_ordinals.def
```

### 3. Recompilar OpenADS com o `.def` custom

```bat
cmake -S . -B build\ord -DOPENADS_ACE_DEF=%CD%\openads_ace_ordinals.def
cmake --build build\ord --target openads_ace --config Release
```

O `build\ord\src\Release\ace64.dll` resultante mantém cada
nome de função + atribui o ordinal que SAP escolheu. Drop-in
sobre o `PATH` da app — os lookups por ordinal do loader
agora atingem o símbolo correto.

## Caminho alternativo recomendado

Se a aplicação puder ser re-linkada, **não precisa do truque
de ordinais**. Gere uma import library do DLL OpenADS (que
exporta por nome):

```bat
lib /def:openads_ace.def /machine:x64 /out:ace64.lib
```

…e re-linke `rddads` e a app contra esse `ace64.lib`. Vincula
por nome, sem acoplamento a ordinais. É a opção future-proof.

## Por que não distribuímos `ace64-exports.txt` upstream

A tabela de exports é metadata sobre um binário que OpenADS
não envia e não possui. Re-publicá-la dentro deste repo
significaria redistribuir dados extraídos de um binário SAP,
o que conflita com a política clean-room. Cada usuário gera
sua cópia localmente de um binário que já tem direito legal
de inspecionar.
