---
title: AdsSetRecord
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-set-record/
---

# AdsSetRecord

Sobrescreve o registo atual com uma imagem física em bruto.

## Sintaxe

```c
UNSIGNED32 AdsSetRecord(ADSHANDLE hTable, UNSIGNED8 *pucRecord, UNSIGNED32 ulLen);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hTable` | `ADSHANDLE` | Handle da tabela. |
| `pucRecord` | `UNSIGNED8*` | Buffer com a imagem em bruto do registo (byte de eliminação + bytes dos campos). |
| `ulLen` | `UNSIGNED32` | Comprimento da imagem fornecida, em bytes. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso. `AE_NO_CURRENT_RECORD` (5068) quando o cursor está em BOF/EOF. `AE_INTERNAL_ERROR` (5000) se o buffer for nulo, a tabela for só de leitura ou o handle for desconhecido.

## Descrição

`AdsSetRecord` escreve uma imagem física completa do registo — tal como produzida por `AdsGetRecord` — sobre o registo atual, descarrega-a para disco e ressincroniza todos os índices vinculados, de modo que qualquer alteração num campo de chave se reflete na ordem do índice.

São copiados no máximo *comprimento do registo* bytes; se `ulLen` for menor, apenas esses bytes são escritos e o resto do registo permanece intacto. O primeiro byte da imagem é a marca de eliminação (um espaço se ativo, `*` se eliminado).

É a contraparte de escrita de `AdsGetRecord`. Não está disponível para tabelas remotas.

## Exemplo

```c
UNSIGNED32 ulLen = 0;

AdsGetRecord(hTable, NULL, &ulLen);
UNSIGNED8 *pucRec = malloc(ulLen);
AdsGetRecord(hTable, pucRec, &ulLen);

// Corrigir um campo de largura fixa e reescrever.
memcpy(pucRec + 5, "BBBBBBBB", 8);
AdsSetRecord(hTable, pucRec, ulLen);

free(pucRec);
```

## Ver Também

- [AdsGetRecord]({{ site.baseurl }}/pt/funcoes/ads-get-record/)
- [AdsWriteRecord]({{ site.baseurl }}/pt/funcoes/ads-write-record/)
- [AdsAppendRecord]({{ site.baseurl }}/pt/funcoes/ads-append-record/)
- [AdsGetRecordLength]({{ site.baseurl }}/pt/funcoes/ads-get-record-length/)

---

[← AdsGetRecord]({{ site.baseurl }}/pt/funcoes/ads-get-record/)
