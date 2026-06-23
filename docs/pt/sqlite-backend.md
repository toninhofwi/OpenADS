---
title: Backend SQLite
layout: default
parent: Início (PT)
nav_order: 8
permalink: /pt/sqlite-backend/
---

# Tabelas com backend SQLite

OpenADS pode abrir e operar um banco de dados **SQLite** através
da mesma superfície ACE / rddads usada para tabelas DBF / ADT. Do
ponto de vista de uma aplicação Harbour / Clipper / X#, a tabela
SQLite se comporta como uma área de trabalho normal — navegação
(`Skip`, `GoTop`, `GoBottom`), leitura/escrita de campos e as
chamadas `Ads*` padrão funcionam.

## Requisitos

- OpenADS compilado com `OPENADS_WITH_SQLITE` — **ativado por
  padrão** em `CMakeLists.txt` (a amálgama SQLite é incluída via
  FetchContent).
- A URI de conexão deve começar com `sqlite://` seguida do
  caminho ao arquivo `.db`.

## Como funciona

O caminho SQLite é escolhido inteiramente pela **URI de conexão**.
`AdsConnect60` chama `parse_sqlite_uri()`; quando a URI casa com
`sqlite://…` abre uma `SqliteConnection` em vez do motor DBF/CDX
nativo. Cada chamada ACE posterior (`AdsOpenTable90`,
`AdsGetField`, `AdsSetField`, `AdsSkip`, `AdsSeek`,
`AdsCreateIndex61`, …) é redirecionada ao backend SQLite.

### 1. Conectar com uma URI `sqlite://`

```clipper
LOCAL hConn
AdsConnect60( "sqlite:///path/to/database.db", ;
              ADS_LOCAL_SERVER, NIL, NIL, 0, @hConn )
```

A terceira barra é o `/` inicial do caminho absoluto, então
`sqlite:///tmp/app.db` abre `/tmp/app.db`.

### 2. Abrir uma tabela existente

Através do rddads a tabela abre como qualquer área de trabalho:

```clipper
USE customers VIA "ADSCDX" NEW SHARED
```

(Aplicações X# usam o RDD `AXDBFCDX`; o roteamento no nível ACE é
idêntico.)

## Mapeamento de tipos de campo

O tipo de campo é inferido do tipo declarado da coluna SQLite
(correspondência de substring, sem distinguir maiúsculas):

| O tipo declarado SQLite contém | Tipo de campo OpenADS | Comprimento |
|--------------------------------|-----------------------|-------------|
| `INT`                          | Integer               | 4           |
| `REAL` / `FLOA` / `DOUB`       | Double                | 8 (6 dec)   |
| `BLOB`                         | Binary                | 10          |
| qualquer outro (ex. `TEXT`)    | Character             | 64          |

## Criptografia

Uma chave de cifra pode ser passada como parâmetro de query; ela é
decodificada de URL antes do uso:

```clipper
AdsConnect60( "sqlite:///path/db.sqlite?key=minhasenha", ;
              ADS_LOCAL_SERVER, NIL, NIL, 0, @hConn )
```

## Limitações atuais

- **Apenas abertura** — `AdsCreateTable` não cria tabelas SQLite.
  Passar um handle de conexão SQLite cai no caminho DBF nativo;
  crie o esquema diretamente no SQLite.
- **Índices** são expostos como `SqliteIndex` com seek / next /
  prev básicos que mapeiam para consultas `ORDER BY`.
- **Transações** mapeiam para transações SQLite normais.

## Outros backends SQL (OpenADS Plus)

SQLite é um de quatro backends SQL escolhidos da mesma forma —
pela URI de conexão. **PostgreSQL**, **MariaDB / MySQL** e
qualquer motor acessível por **ODBC** também são suportados atrás
da ABI ACE:

| Backend | URI de conexão |
|---------|----------------|
| SQLite | `sqlite:///caminho/db.sqlite[?key=…]` |
| PostgreSQL | `postgresql://user:pass@host:5432/dbname` |
| MariaDB / MySQL | `mariadb://user:pass@host:3306/dbname` |
| ODBC (qualquer) | `odbc://Driver={…};Server=…;Database=…;UID=…;PWD=…` |

Os quatro ficam atrás de um único **registro plugável de
backend-ops** (uma struct `BackendTableOps` + uma linha de
registro por backend), então as funções ABI de navegação / campos
ficam agnósticas ao backend. Leitura + navegação + SEEK por coluna
funcionam hoje; a escrita é por backend. Os identificadores são
restritos a ASCII seguro e os valores de SEEK usam parâmetros
preparados. Veja
[`docs/OPENADS_PLUS.md`](https://github.com/FiveTechSoft/OpenADS/blob/main/docs/OPENADS_PLUS.md).
