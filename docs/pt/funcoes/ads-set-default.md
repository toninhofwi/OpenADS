---
title: AdsSetDefault
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-set-default/
---

# AdsSetDefault

Define o diretório padrão registado para a sessão.

## Sintaxe

```c
UNSIGNED32 AdsSetDefault(UNSIGNED8 *pucPath);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `pucPath` | `UNSIGNED8*` | Caminho do diretório padrão. Uma cadeia nula ou vazia limpa-o. |

## Valor de Retorno

`AE_SUCCESS` (0).

## Descrição

`AdsSetDefault` regista uma cadeia de diretório padrão que `AdsGetDefault` devolve. No ADS é o diretório usado para resolver nomes de tabela relativos. O OpenADS resolve os caminhos contra o caminho de dados da conexão, pelo que o valor é armazenado por paridade com a API ACE e faz round-trip com `AdsGetDefault`.

## Exemplo

```c
AdsSetDefault((UNSIGNED8 *)"C:\\DATA\\APP");

UNSIGNED8 buf[260];
UNSIGNED16 len = sizeof(buf);
AdsGetDefault(buf, &len);   // "C:\DATA\APP"
```

## Ver Também

- [AdsGetDefault]({{ site.baseurl }}/pt/funcoes/ads-get-default/)
- [AdsSetSearchPath]({{ site.baseurl }}/pt/funcoes/ads-set-search-path/)

---

[AdsGetDefault →]({{ site.baseurl }}/pt/funcoes/ads-get-default/)
