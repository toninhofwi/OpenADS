---
title: AdsGetRecord
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-record/
---

# AdsGetRecord

Copia a imagem física em bruto do registo atual para um buffer do chamador.

## Sintaxe

```c
UNSIGNED32 AdsGetRecord(ADSHANDLE hTable, UNSIGNED8 *pucRecord, UNSIGNED32 *pulLen);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hTable` | `ADSHANDLE` | Handle da tabela. |
| `pucRecord` | `UNSIGNED8*` | Buffer do chamador que recebe a imagem em bruto do registo. Pode ser `NULL` para consultar o tamanho necessário. |
| `pulLen` | `UNSIGNED32*` | Entrada/saída — capacidade do buffer à entrada; bytes copiados à saída. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso. `AE_INSUFFICIENT_BUFFER` (5051) se o buffer for menor que o registo; o comprimento necessário é devolvido em `pulLen`. `AE_NO_CURRENT_RECORD` (5068) quando o cursor está em BOF/EOF.

## Descrição

`AdsGetRecord` devolve a imagem física completa do registo tal como é armazenada na tabela: o byte inicial de marca de eliminação (um espaço se o registo estiver ativo, `*` se estiver eliminado) seguido dos bytes em bruto dos campos. Os dados são copiados literalmente e **não** terminam em NUL, podendo conter bytes nulos ou altos.

Se `pucRecord` for `NULL` ou `*pulLen` for 0, a chamada é tratada como consulta de tamanho: o comprimento do registo é escrito em `*pulLen` sem copiar bytes, para que o chamador possa alocar o buffer exato e voltar a chamar.

É a função complementar de `AdsSetRecord`, que escreve uma imagem em bruto sobre o registo atual. Não está disponível para tabelas remotas.

## Exemplo

```c
UNSIGNED32 ulLen = 0;

// Consulta de tamanho e leitura.
AdsGetRecord(hTable, NULL, &ulLen);
UNSIGNED8 *pucRec = malloc(ulLen);
AdsGetRecord(hTable, pucRec, &ulLen);

printf("eliminado=%d, %u bytes\n", pucRec[0] == '*', ulLen);
free(pucRec);
```

## Ver Também

- [AdsSetRecord]({{ site.baseurl }}/pt/funcoes/ads-set-record/)
- [AdsGetField]({{ site.baseurl }}/pt/funcoes/ads-get-field/)
- [AdsGetRecordLength]({{ site.baseurl }}/pt/funcoes/ads-get-record-length/)
- [AdsIsRecordDeleted]({{ site.baseurl }}/pt/funcoes/ads-is-record-deleted/)

---

[AdsSetRecord →]({{ site.baseurl }}/pt/funcoes/ads-set-record/)
