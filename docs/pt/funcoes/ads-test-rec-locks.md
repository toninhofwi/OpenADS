---
title: AdsTestRecLocks
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-test-rec-locks/
---

# AdsTestRecLocks

Hook de diagnóstico para os bloqueios de registo de uma tabela.

## Sintaxe

```c
UNSIGNED32 AdsTestRecLocks(ADSHANDLE hTable);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hTable` | `ADSHANDLE` | Handle da tabela. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso. `AE_INTERNAL_ERROR` (5000) se o handle for desconhecido.

## Descrição

`AdsTestRecLocks` é um hook de diagnóstico. O OpenADS não tem uma verificação de consistência da tabela de bloqueios separada para executar, pelo que a chamada valida o handle da tabela e reporta sucesso. É fornecida por compatibilidade com a API ACE. Para inspecionar o estado real dos bloqueios, use `AdsGetAllLocks` ou `AdsIsRecordLocked`.

## Exemplo

```c
AdsTestRecLocks(hTable);
```

## Ver Também

- [AdsGetAllLocks]({{ site.baseurl }}/pt/funcoes/ads-get-all-locks/)
- [AdsIsRecordLocked]({{ site.baseurl }}/pt/funcoes/ads-is-record-locked/)
- [AdsGetNumLocks]({{ site.baseurl }}/pt/funcoes/ads-get-num-locks/)

---

[AdsGetNumLocks →]({{ site.baseurl }}/pt/funcoes/ads-get-num-locks/)
