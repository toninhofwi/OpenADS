---
title: AdsGetAOF
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-aof/
---

# AdsGetAOF

Retorna a cadeia de expressão do Advantage Optimized Filter (AOF) atual.

## Sintaxe

```c
UNSIGNED32 AdsGetAOF(ADSHANDLE hTable, UNSIGNED8 *pucFilter, UNSIGNED16 *pusLen);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hTable` | `ADSHANDLE` | Handle da tabela. |
| `pucFilter` | `UNSIGNED8*` | Buffer de saída para a cadeia de expressão do AOF. |
| `pusLen` | `UNSIGNED16*` | Entrada/saída — tamanho do buffer; recebe o comprimento da expressão retornada. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso.

## Descrição

`AdsGetAOF` recupera a cadeia de expressão do AOF atual instalada
na tabela através de `AdsSetAOF`. Se não houver AOF ativo, a
função retorna uma cadeia vazia.

As expressões AOF são predicados de filtro otimizados com Rushmore
que podem aproveitar as chaves de índice para filtragem rápida de
registos. Use `AdsGetAOFOptLevel` para verificar o nível de
otimização.

## Exemplo

```c
char buf[256];
unsigned short len = sizeof(buf);
AdsGetAOF(hTable, (unsigned char *)buf, &len);
if (len > 0)
    printf("AOF: %s\n", buf);
else
    printf("Nenhum AOF ativo\n");
```

## Ver Também

- [AdsSetAOF]({{ site.baseurl }}/pt/funcoes/ads-set-aof/)
- [AdsClearAOF]({{ site.baseurl }}/pt/funcoes/ads-clear-aof/)
- [AdsGetAOFOptLevel]({{ site.baseurl }}/pt/funcoes/ads-get-aofopt-level/)

---

[← AdsEvalAOF]({{ site.baseurl }}/pt/funcoes/ads-eval-aof/)
[AdsCustomizeAOF →]({{ site.baseurl }}/pt/funcoes/ads-customize-aof/)
