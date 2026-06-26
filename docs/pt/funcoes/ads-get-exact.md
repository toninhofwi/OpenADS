---
title: AdsGetExact
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-exact/
---

# AdsGetExact

Retorna se `SET EXACT` está ativado.

## Sintaxe

```c
UNSIGNED32 AdsGetExact(UNSIGNED16 *pbExact);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `pbExact` | `UNSIGNED16*` | Saída — `ADS_TRUE` se a comparação exata estiver ativada, `ADS_FALSE` caso contrário. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso.

## Descrição

`AdsGetExact` consulta o estado atual de `SET EXACT` para o
processo. Quando ativado, as comparações de cadeias requerem que
ambas as cadeias coincidam exatamente em comprimento e conteúdo.
Quando desativado, uma comparação de `"ABC" = "AB"` retorna true
(espaços/caracteres finais são ignorados).

Isto espelha o comportamento de `SET EXACT` do Clipper.

## Exemplo

```c
UNSIGNED16 bExact = 0;
AdsGetExact(&bExact);
if (bExact == ADS_TRUE)
    printf("SET EXACT está ligado\n");
else
    printf("SET EXACT está desligado\n");
```

## Ver Também

- [AdsSetExact]({{ site.baseurl }}/pt/funcoes/ads-set-exact/)
- [AdsGetDeleted]({{ site.baseurl }}/pt/funcoes/ads-get-deleted/)
- [AdsGetEpoch]({{ site.baseurl }}/pt/funcoes/ads-get-epoch/)

---

[← AdsGetEpoch]({{ site.baseurl }}/pt/funcoes/ads-get-epoch/)
[AdsGetFilter →]({{ site.baseurl }}/pt/funcoes/ads-get-filter/)
