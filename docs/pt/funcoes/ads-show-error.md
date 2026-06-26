---
title: AdsShowError
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-show-error/
---

# AdsShowError

Reporta uma mensagem de erro ao host.

## Sintaxe

```c
UNSIGNED32 AdsShowError(UNSIGNED8 *pucErrText);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `pucErrText` | `UNSIGNED8*` | Mensagem terminada em nulo a reportar. Uma cadeia nula ou vazia é ignorada. |

## Valor de Retorno

`AE_SUCCESS` (0).

## Descrição

`AdsShowError` reporta uma mensagem de erro ao host. Num host com interface gráfica o ADS mostra uma caixa de mensagem; o OpenADS é headless, pelo que o comportamento fiel mais próximo é escrever a mensagem na saída de erro padrão (`stderr`), seguida de uma nova linha. Uma mensagem nula ou vazia não produz saída. A chamada tem sempre sucesso.

## Exemplo

```c
UNSIGNED8 buf[256];
UNSIGNED16 len = sizeof(buf);
AdsGetLastError(&ulCode, buf, &len);
AdsShowError(buf);
```

## Ver Também

- [AdsGetLastError]({{ site.baseurl }}/pt/funcoes/ads-get-last-error/)
- [AdsGetErrorString]({{ site.baseurl }}/pt/funcoes/ads-get-error-string/)

---

[AdsGetLastError →]({{ site.baseurl }}/pt/funcoes/ads-get-last-error/)
