---
title: AdsGetTableAlias
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-table-alias/
---

# AdsGetTableAlias

Retorna o alias de uma tabela.

## Sintaxe

```c
UNSIGNED32 AdsGetTableAlias(ADSHANDLE hTable, UNSIGNED8 *pucBuf, UNSIGNED16 *pusLen);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hTable` | `ADSHANDLE` | Handle da tabela. |
| `pucBuf` | `UNSIGNED8*` | Buffer de saída para a cadeia do alias. |
| `pusLen` | `UNSIGNED16*` | Entrada/saída — tamanho do buffer; recebe o comprimento do alias retornado. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso.

## Descrição

`AdsGetTableAlias` recupera o nome do alias atribuído ao handle
da tabela dado. O alias é o nome utilizado na instrução `USE`
ou quando a tabela foi aberta.

## Exemplo

```c
char alias[32];
unsigned short len = sizeof(alias);
AdsGetTableAlias(hTable, (unsigned char *)alias, &len);
printf("Alias: %s\n", alias);
```

## Ver Também

- [AdsOpenTable]({{ site.baseurl }}/pt/funcoes/ads-open-table/)
- [AdsGetTableFilename]({{ site.baseurl }}/pt/funcoes/ads-get-table-filename/)
- [AdsGetTableType]({{ site.baseurl }}/pt/funcoes/ads-get-table-type/)

---

[← AdsGetTableCharType]({{ site.baseurl }}/pt/funcoes/ads-get-table-char-type/)
[AdsGetTableConnection →]({{ site.baseurl }}/pt/funcoes/ads-get-table-connection/)
