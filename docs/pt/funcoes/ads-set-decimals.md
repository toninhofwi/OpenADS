---
title: AdsSetDecimals
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-set-decimals/
---

# AdsSetDecimals

Define o número de casas decimais padrão registado para a sessão.

## Sintaxe

```c
UNSIGNED32 AdsSetDecimals(UNSIGNED16 usDecimals);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `usDecimals` | `UNSIGNED16` | Número padrão de casas decimais. |

## Valor de Retorno

`AE_SUCCESS` (0).

## Descrição

`AdsSetDecimals` regista o número padrão de casas decimais (a definição SET DECIMALS). É armazenado por paridade com a API ACE; o OpenADS lê e escreve os campos numéricos com a precisão definida no esquema, independentemente deste valor.

## Exemplo

```c
AdsSetDecimals(4);
```

## Ver Também

- [AdsSetExact]({{ site.baseurl }}/pt/funcoes/ads-set-exact/)
- [AdsSetEpoch]({{ site.baseurl }}/pt/funcoes/ads-set-epoch/)

---

[AdsSetExact →]({{ site.baseurl }}/pt/funcoes/ads-set-exact/)
