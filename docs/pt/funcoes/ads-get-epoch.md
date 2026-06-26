---
title: AdsGetEpoch
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-epoch/
---

# AdsGetEpoch

Retorna o valor do pivô de ano de 2 algarismos.

## Sintaxe

```c
UNSIGNED32 AdsGetEpoch(UNSIGNED16 *pusEpoch);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `pusEpoch` | `UNSIGNED16*` | Saída — o ano pivô (por exemplo, 1970 ou 2000). |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso.

## Descrição

`AdsGetEpoch` retorna o pivô de ano de 2 algarismos a nível de
processo. Datas com anos de 2 algarismos abaixo do pivô são
interpretadas como datas do século XXI; as que estão em ou acima
são interpretadas como datas do século XX.

O epoch predefinido é 1970. Esta configuração afeta como as datas
são analisadas quando armazenadas como valores de ano de 2
algarismos.

## Exemplo

```c
UNSIGNED16 usEpoch = 0;
AdsGetEpoch(&usEpoch);
printf("Pivô do epoch: %u\n", usEpoch);
```

## Ver Também

- [AdsSetEpoch]({{ site.baseurl }}/pt/funcoes/ads-set-epoch/)
- [AdsGetDateFormat]({{ site.baseurl }}/pt/funcoes/ads-get-date-format/)
- [AdsGetDeleted]({{ site.baseurl }}/pt/funcoes/ads-get-deleted/)

---

[← AdsGetDeleted]({{ site.baseurl }}/pt/funcoes/ads-get-deleted/)
[AdsGetExact →]({{ site.baseurl }}/pt/funcoes/ads-get-exact/)
