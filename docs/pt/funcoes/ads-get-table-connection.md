---
title: AdsGetTableConnection
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-table-connection/
---

# AdsGetTableConnection

Retorna o handle de conexão associado a uma tabela.

## Sintaxe

```c
UNSIGNED32 AdsGetTableConnection(ADSHANDLE hTable, ADSHANDLE *phConnect);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hTable` | `ADSHANDLE` | Handle da tabela. |
| `phConnect` | `ADSHANDLE*` | Saída — handle de conexão da tabela. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso.

## Descrição

`AdsGetTableConnection` recupera o handle de conexão que foi
utilizado para abrir a tabela dada. Para tabelas locais, esta é
a conexão do motor local integrada. Para tabelas remotas, é a
conexão TCP/TLS com o servidor.

## Exemplo

```c
ADSHANDLE hConn = 0;
AdsGetTableConnection(hTable, &hConn);
printf("A tabela está na conexão: %p\n", (void *)hConn);
```

## Ver Também

- [AdsConnect60]({{ site.baseurl }}/pt/funcoes/ads-connect60/)
- [AdsGetConnectionType]({{ site.baseurl }}/pt/funcoes/ads-get-connection-type/)
- [AdsIsConnectionAlive]({{ site.baseurl }}/pt/funcoes/ads-is-connection-alive/)

---

[← AdsGetTableAlias]({{ site.baseurl }}/pt/funcoes/ads-get-table-alias/)
[AdsGetTableOpenOptions →]({{ site.baseurl }}/pt/funcoes/ads-get-table-open-options/)
