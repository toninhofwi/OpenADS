---
title: AdsGetTableCharType
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-table-char-type/
---

# AdsGetTableCharType

Retorna o tipo de caracteres de uma tabela (`ADS_ANSI` ou `ADS_OEM`).

## Sintaxe

```c
UNSIGNED32 AdsGetTableCharType(ADSHANDLE hTable, UNSIGNED16 *pusType);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hTable` | `ADSHANDLE` | Handle da tabela. |
| `pusType` | `UNSIGNED16*` | Saída — constante do tipo de caracteres. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso.

## Constantes de Tipo de Caracteres

| Constante | Valor | Descrição |
|-----------|-------|-----------|
| `ADS_OEM` | 0 | Conjunto de caracteres OEM (compatibilidade binária). |
| `ADS_ANSI` | 1 | Conjunto de caracteres ANSI (Windows). |

## Descrição

`AdsGetTableCharType` retorna se a tabela foi aberta com
tradução de caracteres OEM ou ANSI. Isto afeta como os campos
de caracteres são armazenados e comparados.

## Exemplo

```c
UNSIGNED16 charType = 0;
AdsGetTableCharType(hTable, &charType);
if (charType == ADS_ANSI)
    printf("A tabela usa caracteres ANSI\n");
else
    printf("A tabela usa caracteres OEM\n");
```

## Ver Também

- [AdsOpenTable]({{ site.baseurl }}/pt/funcoes/ads-open-table/)
- [AdsGetTableType]({{ site.baseurl }}/pt/funcoes/ads-get-table-type/)
- [AdsGetTableAlias]({{ site.baseurl }}/pt/funcoes/ads-get-table-alias/)

---

[← AdsGetTableConType]({{ site.baseurl }}/pt/funcoes/ads-get-table-con-type/)
[AdsGetTableAlias →]({{ site.baseurl }}/pt/funcoes/ads-get-table-alias/)
