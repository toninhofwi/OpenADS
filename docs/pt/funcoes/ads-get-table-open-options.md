---
title: AdsGetTableOpenOptions
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-table-open-options/
---

# AdsGetTableOpenOptions

Retorna as flags de modo de abertura de uma tabela.

## Sintaxe

```c
UNSIGNED32 AdsGetTableOpenOptions(ADSHANDLE hTable, UNSIGNED32 *pulOptions);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hTable` | `ADSHANDLE` | Handle da tabela. |
| `pulOptions` | `UNSIGNED32*` | Saída — máscara de bits das flags de modo de abertura. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso.

## Descrição

`AdsGetTableOpenOptions` retorna a máscara de bits de flags
utilizadas quando a tabela foi aberta. Estas flags incluem acesso
de leitura/escrita, modo de partilha e outras opções a nível de
tabela.

## Exemplo

```c
unsigned long options = 0;
AdsGetTableOpenOptions(hTable, &options);
printf("Opções de abertura: 0x%08lX\n", options);
```

## Ver Também

- [AdsOpenTable]({{ site.baseurl }}/pt/funcoes/ads-open-table/)
- [AdsGetTableType]({{ site.baseurl }}/pt/funcoes/ads-get-table-type/)
- [AdsGetTableCharType]({{ site.baseurl }}/pt/funcoes/ads-get-table-char-type/)

---

[← AdsGetTableFilename]({{ site.baseurl }}/pt/funcoes/ads-get-table-filename/)
[AdsGetTableConnection →]({{ site.baseurl }}/pt/funcoes/ads-get-table-connection/)
