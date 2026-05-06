---
title: Diccionario de datos
layout: default
parent: Inicio (ES)
nav_order: 6
permalink: /es/diccionario-datos/
---

# Diccionario de Datos

OpenADS soporta un Diccionario de Datos (archivo `.add`) que
permite a una conexión ver un grupo lógico de tablas bajo
alias estables, además de los metadatos que una aplicación
ADS típicamente espera: usuarios / grupos, links externos,
reglas de integridad referencial, propiedades DB / usuario,
e índices vinculados.

## Nota clean-room

OpenADS usa un **Diccionario de Datos en formato texto nativo
OpenADS** (`# OpenADS Data Dictionary v1`). **No es compatible
byte-byte** con el `.add` binario propietario del ADS legacy —
ese formato no está documentado y la política clean-room del
proyecto prohíbe reverse-engineering. Round-trip garantizado
solo entre instancias OpenADS.

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
DBPROP    locale=es-ES
USERPROP  reporting;default_role=read_only
```

Filas desconocidas se preservan al cargar — un dict v1 escrito
por un OpenADS v2-aware sigue round-tripping en un cliente v1
sin perder datos. Comentarios (`# …`) round-trip también.

## API motor

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

dd.save();   // escritura atómica
```

## Mediante ABI pública

`AdsConnect60(<ruta.add>, ADS_LOCAL_SERVER, …)` abre conexión
respaldada por `DataDict`. `AdsOpenTable("clientes", …)`
resuelve el alias a través del mapa `TABLE`.

| Llamada Ads*                       | Método DataDict |
|------------------------------------|-----------------|
| `AdsDDCreateTable`                 | `add_table`     |
| `AdsDDAddIndexFile`                | `add_index_file`|
| `AdsDDCreateUser`                  | `create_user`   |
| `AdsDDAddUserToGroup`              | `add_user_to_group` |
| `AdsDDCreateLink`                  | `create_link`   |
| `AdsDDCreateReferentialIntegrity`  | `create_ri`     |
| `AdsDDSetDatabaseProperty`         | `set_db_property` |
| `AdsDDSetUserProperty`             | `set_user_property` |

Save atómico (write-then-rename) — un crash mid-save deja los
contenidos previos intactos.

## Hitos

| Tag         | Scope |
|-------------|-------|
| `m6-partial`| Resolución de aliases (filas `TABLE`). |
| `m9.25`     | Llamadas DD ABI silent-success (back-compat). |
| `m10.1`     | Persistencia real: todas las filas round-trip, save atómico. |

## Integración con Studio

Studio actualmente lista `*.dbf` directamente del directorio.
Mostrar DD aliases / users / RI en la SPA está en cola como
`studio.web.0.5`.
