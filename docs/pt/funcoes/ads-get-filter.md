---
title: AdsGetFilter
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-filter/
---

# AdsGetFilter

Retorna a expressão de filtro atual de uma tabela.

## Sintaxe

```c
UNSIGNED32 AdsGetFilter(ADSHANDLE hTable, UNSIGNED8 *pucBuf, UNSIGNED16 *pusLen);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hTable` | `ADSHANDLE` | Handle da tabela. |
| `pucBuf` | `UNSIGNED8*` | Buffer de saída para a cadeia da expressão de filtro. |
| `pusLen` | `UNSIGNED16*` | Entrada/saída — tamanho do buffer; recebe o comprimento da expressão retornada. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso.

## Descrição

`AdsGetFilter` recupera a cadeia de expressão de filtro atual
instalada na tabela através de `AdsSetFilter`. Se não houver filtro
ativo, a função retorna uma cadeia vazia.

Nota: Isto retorna a expressão de filtro não indexada. Para filtros
otimizados com Rushmore, use `AdsGetAOF` em vez disso.

## Exemplo

```c
char buf[256];
unsigned short len = sizeof(buf);
AdsGetFilter(hTable, (unsigned char *)buf, &len);
if (len > 0)
    printf("Filtro: %s\n", buf);
else
    printf("Nenhum filtro ativo\n");
```

## Ver Também

- [AdsSetFilter]({{ site.baseurl }}/pt/funcoes/ads-set-filter/)
- [AdsClearFilter]({{ site.baseurl }}/pt/funcoes/ads-clear-filter/)
- [AdsGetAOF]({{ site.baseurl }}/pt/funcoes/ads-get-aof/)

---

[← AdsGetExact]({{ site.baseurl }}/pt/funcoes/ads-get-exact/)
[AdsGetHandleType →]({{ site.baseurl }}/pt/funcoes/ads-get-handle-type/)
