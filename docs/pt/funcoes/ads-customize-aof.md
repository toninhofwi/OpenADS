---
title: AdsCustomizeAOF
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-customize-aof/
---

# AdsCustomizeAOF

Força registos individuais para dentro ou fora do Advantage Optimized Filter ativo.

## Sintaxe

```c
UNSIGNED32 AdsCustomizeAOF(ADSHANDLE hTable, UNSIGNED32 ulNumRecords,
                           UNSIGNED32 *pulRecords, UNSIGNED16 usOption);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hTable` | `ADSHANDLE` | Handle da tabela com um AOF ativo. |
| `ulNumRecords` | `UNSIGNED32` | Número de registos em `pulRecords`. |
| `pulRecords` | `UNSIGNED32*` | Array de números de registo a adicionar ou remover. |
| `usOption` | `UNSIGNED16` | `ADS_AOF_ADD_RECORD` (1) para incluir, `ADS_AOF_REMOVE_RECORD` (2) para excluir. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso. `AE_INTERNAL_ERROR` (5000) se não houver AOF ativo, a opção for inválida ou o handle for desconhecido. `AE_FUNCTION_NOT_AVAILABLE` (5004) para tabelas remotas.

## Descrição

`AdsCustomizeAOF` substitui manualmente a pertença de registos específicos ao conjunto de resultados do AOF instalado com `AdsSetAOF`. `ADS_AOF_ADD_RECORD` torna visíveis os registos listados mesmo que não satisfaçam a expressão de filtro; `ADS_AOF_REMOVE_RECORD` oculta-os mesmo que a satisfaçam.

A alteração atualiza o bitmap AOF retido e reinstala-o, de modo que a navegação seguinte (`AdsGotoTop`, `AdsGotoBottom`, `AdsSkip`) reflete de imediato o conjunto personalizado. Números de registo fora do intervalo da tabela são ignorados. A personalização é descartada ao limpar o AOF com `AdsClearAOF` ou ao substituí-lo por um novo `AdsSetAOF`.

Requer um AOF ativo e não está disponível para tabelas remotas.

## Exemplo

```c
AdsSetAOF(hTable, "BALANCE > 0", 0);

// Incluir sempre o registo 4, mesmo que o saldo seja 0.
UNSIGNED32 add[] = { 4 };
AdsCustomizeAOF(hTable, 1, add, ADS_AOF_ADD_RECORD);

// Ocultar o registo 2 independentemente do saldo.
UNSIGNED32 rem[] = { 2 };
AdsCustomizeAOF(hTable, 1, rem, ADS_AOF_REMOVE_RECORD);
```

## Ver Também

- [AdsSetAOF]({{ site.baseurl }}/pt/funcoes/ads-set-aof/)
- [AdsGetAOF]({{ site.baseurl }}/pt/funcoes/ads-get-aof/)
- [AdsClearAOF]({{ site.baseurl }}/pt/funcoes/ads-clear-aof/)

---

[AdsGetAOF →]({{ site.baseurl }}/pt/funcoes/ads-get-aof/)
