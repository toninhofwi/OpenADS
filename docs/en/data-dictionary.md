---
title: Data Dictionary
layout: default
parent: Home (EN)
nav_order: 6
permalink: /en/data-dictionary/
---

# Data Dictionary

OpenADS supports a Data Dictionary (`.add` file) that lets a
single connection see a logical group of tables under stable
aliases, plus the surrounding metadata an application normally
expects from the legacy ADS dictionary: users / groups, link
references, referential-integrity rules, DB / user properties,
and bound index lists.

## Clean-room note

OpenADS uses an **OpenADS-native text-format Data Dictionary**
(`# OpenADS Data Dictionary v1`). It is **not byte-compatible**
with the proprietary binary `.add` shipped by the legacy ADS —
that format is undocumented and the project's clean-room policy
forbids reverse-engineering SAP-owned material. Round-trip is
guaranteed only between OpenADS instances.

## File layout (v1)

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
DBPROP    locale=es-ES
USERPROP  reporting;default_role=read_only
```

Unknown rows are preserved on load so a v1 dictionary written
back by a v2-aware OpenADS still round-trips through a v1
client without losing data. Comment lines (`# …`) round-trip too.

## Engine API

`engine::DataDict` (`src/engine/data_dict.{h,cpp}`) is the
single place that owns load + save semantics:

```cpp
using openads::engine::DataDict;

auto dd_r = DataDict::open("data/orders.add");
if (!dd_r) return dd_r.error();
DataDict& dd = dd_r.value();

dd.add_table   ("clientes", "clientes.dbf");
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

dd.save();   // atomic write-then-rename
```

## Through the public ABI

`AdsConnect60(<add_path>, ADS_LOCAL_SERVER, …)` opens a
connection backed by a `DataDict` instead of a raw filesystem
directory. Subsequent `AdsOpenTable("clientes", …)` resolves the
alias through the dictionary's `TABLE` map.

CRUD-style ABI calls land on the engine API:

| Ads* call                        | DataDict method |
|----------------------------------|-----------------|
| `AdsDDCreateTable`               | `add_table`     |
| `AdsDDRemoveTable`               | `remove_table`  |
| `AdsDDAddIndexFile`              | `add_index_file`|
| `AdsDDRemoveIndexFile`           | `remove_index_file` |
| `AdsDDCreateUser`                | `create_user`   |
| `AdsDDDeleteUser`                | `delete_user`   |
| `AdsDDAddUserToGroup`            | `add_user_to_group` |
| `AdsDDRemoveUserFromGroup`       | `remove_user_from_group` |
| `AdsDDCreateLink` / `AdsDDDropLink` / `AdsDDModifyLink` | `create_link` / `drop_link` / `modify_link` |
| `AdsDDCreateReferentialIntegrity` | `create_ri`     |
| `AdsDDRemoveReferentialIntegrity` | `remove_ri`     |
| `AdsDDSetDatabaseProperty` / `AdsDDGetDatabaseProperty` | `set_db_property` / `get_db_property` |
| `AdsDDSetUserProperty`     / `AdsDDGetUserProperty`     | `set_user_property` / `get_user_property` |

Every mutation is buffered in memory; `save()` (or the engine's
own implicit save on `AdsDisconnect`) writes atomically with
write-then-rename so a crashed mid-save leaves the previous
contents intact.

## Milestones

| Tag         | Scope |
|-------------|-------|
| `m6-partial`| Initial alias resolution (`TABLE` rows). |
| `m9.25`     | DD ABI calls returning silent-success (back-compat). |
| `m10.1`     | Real OpenADS-native persistence: every row type round-trips, atomic save, ABI calls become real. |

## Studio integration

The Studio web console exposes Data Dictionary CRUD through a
dedicated **Dict** tab (`studio.web.0.5`):

- Picks any `*.add` in the data dir from a dropdown.
- Lists every TABLE alias, USER, INDEX entry, LINK, RI rule, and
  DBPROP key in tabular form.
- Add / remove TABLE alias from a tiny inline form.
- Add / remove USER.
- Set DBPROP (key + value).
- Create new dictionary file.
- Drop dictionary file.

REST surface (used by Studio, also scriptable from curl / Python):

| Method + path | Purpose |
|---------------|---------|
| `GET /api/dd`                                | list `*.add` files |
| `POST /api/dd`                               | create new dictionary `{name}` |
| `GET /api/dd/<n>`                            | full content as JSON |
| `DELETE /api/dd/<n>`                         | drop the .add on disk |
| `POST /api/dd/<n>/tables` `{alias, path}`    | add TABLE row |
| `DELETE /api/dd/<n>/tables/<alias>`          | remove TABLE row |
| `POST /api/dd/<n>/users` `{user}`            | add USER row |
| `DELETE /api/dd/<n>/users/<u>`               | remove USER row |
| `POST /api/dd/<n>/dbprop` `{key, value}`     | set DBPROP |
