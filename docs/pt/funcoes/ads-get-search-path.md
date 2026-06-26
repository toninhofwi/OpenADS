---
title: AdsGetSearchPath
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-search-path/
---

# AdsGetSearchPath

Devolve o caminho de pesquisa de tabelas registado para a sessão.

## Sintaxe

```c
UNSIGNED32 AdsGetSearchPath(UNSIGNED8 *pucPath, UNSIGNED16 *pusLen);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `pucPath` | `UNSIGNED8*` | Buffer que recebe a cadeia do caminho de pesquisa. |
| `pusLen` | `UNSIGNED16*` | Entrada/saída — tamanho do buffer à entrada; comprimento da cadeia à saída. |

## Valor de Retorno

`AE_SUCCESS` (0).

## Descrição

`AdsGetSearchPath` devolve o caminho de pesquisa definido previamente com `AdsSetSearchPath`, ou uma cadeia vazia se nenhum foi definido. As duas funções formam um round-trip.

## Exemplo

```c
UNSIGNED8 buf[512];
UNSIGNED16 len = sizeof(buf);
AdsGetSearchPath(buf, &len);
```

## Ver Também

- [AdsSetSearchPath]({{ site.baseurl }}/pt/funcoes/ads-set-search-path/)
- [AdsGetDefault]({{ site.baseurl }}/pt/funcoes/ads-get-default/)

---

[← AdsSetSearchPath]({{ site.baseurl }}/pt/funcoes/ads-set-search-path/)
