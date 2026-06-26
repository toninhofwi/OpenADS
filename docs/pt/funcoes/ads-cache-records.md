---
title: AdsCacheRecords
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-cache-records/
---

# AdsCacheRecords

Sugere quantos registos ler antecipadamente para uma tabela.

## Sintaxe

```c
UNSIGNED32 AdsCacheRecords(ADSHANDLE hTable, UNSIGNED16 usNumRecords);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hTable` | `ADSHANDLE` | Handle da tabela. |
| `usNumRecords` | `UNSIGNED16` | Número sugerido de registos a ler antecipadamente. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso. `AE_INTERNAL_ERROR` (5000) se o handle for desconhecido.

## Descrição

`AdsCacheRecords` é uma sugestão de leitura antecipada. O OpenADS não faz pré-cache de linhas, pelo que a chamada valida o handle da tabela e tem sucesso sem alterar o comportamento. É fornecida por compatibilidade com a API ACE para código que ajusta a cache do cliente.

## Exemplo

```c
AdsCacheRecords(hTable, 50);
```

## Ver Também

- [AdsCacheOpenTables]({{ site.baseurl }}/pt/funcoes/ads-cache-open-tables/)
- [AdsCacheOpenCursors]({{ site.baseurl }}/pt/funcoes/ads-cache-open-cursors/)

---

[AdsCacheOpenTables →]({{ site.baseurl }}/pt/funcoes/ads-cache-open-tables/)
