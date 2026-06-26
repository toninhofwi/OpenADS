---
title: AdsGetDeleted
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-deleted/
---

# AdsGetDeleted

Retorna se os registos eliminados estão ocultos (estado `SET DELETED`).

## Sintaxe

```c
UNSIGNED32 AdsGetDeleted(UNSIGNED16 *pbShow);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `pbShow` | `UNSIGNED16*` | Saída — `ADS_TRUE` se os registos eliminados estiverem ocultos, `ADS_FALSE` caso contrário. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso.

## Descrição

`AdsGetDeleted` consulta o estado atual de `SET DELETED` para o
processo. Quando ativado (`ADS_TRUE`), os registos marcados como
eliminados são excluídos dos comandos de navegação como `Skip`,
`GoTop` e `Seek`. Quando desativado (`ADS_FALSE`), os registos
eliminados permanecem visíveis.

Isto espelha o comportamento de `SET DELETED` do Clipper e é
independente de qualquer expressão AOF ou filtro.

## Exemplo

```c
UNSIGNED16 bShow = 0;
AdsGetDeleted(&bShow);
if (bShow == ADS_TRUE)
    printf("Os registos eliminados estão ocultos\n");
else
    printf("Os registos eliminados são visíveis\n");
```

## Ver Também

- [AdsShowDeleted]({{ site.baseurl }}/pt/funcoes/ads-show-deleted/)
- [AdsGetFilter]({{ site.baseurl }}/pt/funcoes/ads-get-filter/)
- [AdsGetAOF]({{ site.baseurl }}/pt/funcoes/ads-get-aof/)

---

[← AdsGetDateFormat]({{ site.baseurl }}/pt/funcoes/ads-get-date-format/)
[AdsGetEpoch →]({{ site.baseurl }}/pt/funcoes/ads-get-epoch/)
