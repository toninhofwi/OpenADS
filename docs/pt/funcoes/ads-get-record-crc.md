---
title: AdsGetRecordCRC
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-record-crc/
---

# AdsGetRecordCRC

Calcula uma soma de verificação CRC da imagem do registo atual.

## Sintaxe

```c
UNSIGNED32 AdsGetRecordCRC(ADSHANDLE hTable, UNSIGNED32 *pulCRC, UNSIGNED32 ulOptions);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hTable` | `ADSHANDLE` | Handle da tabela. |
| `pulCRC` | `UNSIGNED32*` | Recebe o CRC de 32 bits do registo atual. |
| `ulOptions` | `UNSIGNED32` | Reservado; passe 0. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso. `AE_NO_CURRENT_RECORD` (5068) quando o cursor está em BOF/EOF. `AE_FUNCTION_NOT_AVAILABLE` (5004) para tabelas remotas.

## Descrição

`AdsGetRecordCRC` devolve uma soma de verificação de 32 bits calculada sobre a imagem física em bruto do registo — os mesmos bytes que `AdsGetRecord` devolve, incluindo o byte inicial de marca de eliminação. Usa o CRC-32 IEEE padrão (refletido, polinómio `0xEDB88320`).

A soma é uma forma rápida de detetar se um registo mudou: ler o CRC, fazer outro trabalho, ler de novo e comparar. Dois registos com bytes de campo idênticos produzem o mesmo CRC; qualquer diferença na imagem dá um valor distinto. O valor é estável para uma dada imagem de registo, mas **não** se garante que coincida com o de outra implementação de ADS.

Não está disponível para tabelas remotas.

## Exemplo

```c
UNSIGNED32 ulAntes = 0, ulDepois = 0;

AdsGetRecordCRC(hTable, &ulAntes, 0);
// ... outro processo pode atualizar a linha ...
AdsRefreshRecord(hTable);
AdsGetRecordCRC(hTable, &ulDepois, 0);

if (ulAntes != ulDepois)
    printf("registo alterado\n");
```

## Ver Também

- [AdsGetRecord]({{ site.baseurl }}/pt/funcoes/ads-get-record/)
- [AdsRefreshRecord]({{ site.baseurl }}/pt/funcoes/ads-refresh-record/)
- [AdsGetRecordLength]({{ site.baseurl }}/pt/funcoes/ads-get-record-length/)

---

[AdsGetRecord →]({{ site.baseurl }}/pt/funcoes/ads-get-record/)
