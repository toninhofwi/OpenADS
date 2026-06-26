---
title: AdsRefreshAOF
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-refresh-aof/
---

# AdsRefreshAOF

Reavalia o Advantage Optimized Filter ativo contra os dados atuais.

## Sintaxe

```c
UNSIGNED32 AdsRefreshAOF(ADSHANDLE hTable);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hTable` | `ADSHANDLE` | Handle da tabela com um AOF ativo. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso, incluindo quando não há AOF ativo (no-op). `AE_INTERNAL_ERROR` (5000) se o handle for desconhecido.

## Descrição

`AdsRefreshAOF` reavalia a expressão AOF instalada com `AdsSetAOF` contra o conteúdo atual da tabela e reinstala o bitmap resultante. Os registos adicionados ou cujos campos de chave mudaram desde que o filtro foi definido são reclassificados, de modo que o conjunto visível reflete os dados mais recentes.

Se não houver AOF ativo, ou o conjunto foi construído apenas via `AdsCustomizeAOF` (sem expressão armazenada), a chamada tem sucesso sem alterar nada. Para tabelas remotas o servidor mantém o AOF, pelo que a chamada é um no-op.

## Exemplo

```c
AdsSetAOF(hTable, "BALANCE > 0", 0);
// ... os registos mudam ou são adicionados ...
AdsRefreshAOF(hTable);   // o conjunto visível reflete os novos dados
```

## Ver Também

- [AdsSetAOF]({{ site.baseurl }}/pt/funcoes/ads-set-aof/)
- [AdsCustomizeAOF]({{ site.baseurl }}/pt/funcoes/ads-customize-aof/)
- [AdsClearAOF]({{ site.baseurl }}/pt/funcoes/ads-clear-aof/)

---

[AdsCustomizeAOF →]({{ site.baseurl }}/pt/funcoes/ads-customize-aof/)
