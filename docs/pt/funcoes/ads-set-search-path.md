---
title: AdsSetSearchPath
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-set-search-path/
---

# AdsSetSearchPath

Define o caminho de pesquisa de tabelas registado para a sessão.

## Sintaxe

```c
UNSIGNED32 AdsSetSearchPath(UNSIGNED8 *pucPath);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `pucPath` | `UNSIGNED8*` | Lista de diretórios separados por ponto e vírgula. Uma cadeia nula ou vazia limpa-a. |

## Valor de Retorno

`AE_SUCCESS` (0).

## Descrição

`AdsSetSearchPath` regista um caminho de pesquisa que `AdsGetSearchPath` devolve. No ADS é a lista de diretórios pesquisados ao abrir uma tabela por nome simples. O OpenADS resolve os caminhos contra o caminho de dados da conexão, pelo que o valor é armazenado por paridade com a API ACE e faz round-trip com `AdsGetSearchPath`.

## Exemplo

```c
AdsSetSearchPath((UNSIGNED8 *)"C:\\DATA;C:\\SHARED");
```

## Ver Também

- [AdsGetSearchPath]({{ site.baseurl }}/pt/funcoes/ads-get-search-path/)
- [AdsSetDefault]({{ site.baseurl }}/pt/funcoes/ads-set-default/)

---

[AdsGetSearchPath →]({{ site.baseurl }}/pt/funcoes/ads-get-search-path/)
