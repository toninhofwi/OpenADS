---
title: Dicionário de dados
layout: default
parent: Início (PT)
nav_order: 6
permalink: /pt/dicionario-dados/
---

# Dicionário de Dados

OpenADS suporta um Dicionário de Dados (arquivo `.add`) que
permite a uma conexão ver um grupo lógico de tabelas sob aliases
estáveis, além dos metadados que uma aplicação ADS tipicamente
espera: usuários / grupos, links externos, regras de integridade
referencial, propriedades DB / usuário, e índices vinculados.

## Nota clean-room

OpenADS usa um **Dicionário de Dados em formato texto nativo
OpenADS** (`# OpenADS Data Dictionary v1`). **Não é compatível
byte-a-byte** com o `.add` binário proprietário do ADS legado —
esse formato não é documentado e a política clean-room do
projeto proíbe reverse-engineering. Round-trip garantido apenas
entre instâncias OpenADS.

## Layout (v1)

```
# OpenADS Data Dictionary v1
TABLE     clientes=clientes.dbf
TABLE     pedidos=pedidos.dbf
INDEX     clientes=clientes.cdx        primary key
INDEX     pedidos=pedidos.cdx
USER      admin
USER      reporting
MEMBER    reporting=readers
LINK      remote_archive=tcp://archive.lan:6262/archive
RI        order_customer=clientes;pedidos;CUSTOMER_ID;cascade;restrict;rierrors
DBPROP    schema_version=1
DBPROP    locale=pt-BR
USERPROP  reporting;default_role=read_only
```

Linhas desconhecidas são preservadas ao carregar — um dict v1
escrito por um OpenADS v2-aware continua round-tripping em um
cliente v1 sem perder dados. Comentários (`# …`) round-trip
também.

## API do motor

`engine::DataDict` (`src/engine/data_dict.{h,cpp}`):

```cpp
using openads::engine::DataDict;

auto dd_r = DataDict::open("data/orders.add");
if (!dd_r) return dd_r.error();
DataDict& dd = dd_r.value();

dd.add_table("clientes", "clientes.dbf");
dd.add_index_file("clientes", "clientes.cdx", "primary key");
dd.create_user("admin");
dd.add_user_to_group("admin", "dba");
dd.create_ri({.name = "order_customer",
              .parent = "clientes", .child = "pedidos",
              .tag = "CUSTOMER_ID",
              .update_opt = "cascade",
              .delete_opt = "restrict",
              .fail_table = "rierrors"});
dd.set_db_property("schema_version", "1");

dd.save();   // escrita atômica
```

## Pela ABI pública

`AdsConnect60(<caminho.add>, ADS_LOCAL_SERVER, …)` abre conexão
respaldada por `DataDict`. `AdsOpenTable("clientes", …)`
resolve o alias através do mapa `TABLE`.

| Chamada Ads*                       | Método DataDict |
|------------------------------------|-----------------|
| `AdsDDCreateTable`                 | `add_table`     |
| `AdsDDAddIndexFile`                | `add_index_file`|
| `AdsDDCreateUser`                  | `create_user`   |
| `AdsDDAddUserToGroup`              | `add_user_to_group` |
| `AdsDDCreateLink`                  | `create_link`   |
| `AdsDDCreateReferentialIntegrity`  | `create_ri`     |
| `AdsDDSetDatabaseProperty`         | `set_db_property` |
| `AdsDDSetUserProperty`             | `set_user_property` |

Save atômico (write-then-rename) — um crash no meio do save
deixa o conteúdo anterior intacto.

## Marcos

| Tag         | Escopo |
|-------------|--------|
| `m6-partial`| Resolução de aliases (linhas `TABLE`). |
| `m9.25`     | Chamadas DD ABI silent-success (back-compat). |
| `m10.1`     | Persistência real: todas linhas round-trip, save atômico. |

## Integração com Studio

Studio atualmente lista `*.dbf` direto do diretório. Mostrar DD
aliases / users / RI na SPA está na fila como `studio.web.0.5`.
