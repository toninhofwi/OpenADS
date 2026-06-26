---
title: AdsSetServerType
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-set-server-type/
---

# AdsSetServerType

Regista o(s) tipo(s) de servidor preferido(s) para as conexões seguintes.

## Sintaxe

```c
UNSIGNED32 AdsSetServerType(UNSIGNED16 usServerOptions);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `usServerOptions` | `UNSIGNED16` | Máscara de tipos de servidor: `ADS_LOCAL_SERVER` (1), `ADS_REMOTE_SERVER` (2), `ADS_AIS_SERVER`. Combine com OR bit a bit. |

## Valor de Retorno

`AE_SUCCESS` (0).

## Descrição

`AdsSetServerType` regista que tipo(s) de servidor a aplicação prefere ao estabelecer uma conexão. No ADS isto restringe que back-ends de servidor o `AdsConnect` tentará.

O OpenADS atende conexões locais e remotas independentemente desta definição, pelo que o valor é armazenado por paridade com a API ACE e não bloqueia uma conexão de outra forma válida. Chame-o antes de `AdsConnect` se o seu código espera a semântica do ADS; passar uma máscara combinada (local + remoto) é sempre seguro.

## Exemplo

```c
// Preferir um servidor remoto, com recurso a local.
AdsSetServerType(ADS_REMOTE_SERVER | ADS_LOCAL_SERVER);

ADSHANDLE hConn;
AdsConnect((UNSIGNED8 *)"\\\\servidor\\partilha\\dados", &hConn);
```

## Ver Também

- [AdsConnect]({{ site.baseurl }}/pt/funcoes/ads-connect/)
- [AdsGetConnectionType]({{ site.baseurl }}/pt/funcoes/ads-get-connection-type/)

---

[AdsConnect →]({{ site.baseurl }}/pt/funcoes/ads-connect/)
