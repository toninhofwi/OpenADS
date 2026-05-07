---
title: Studio (consola web)
layout: default
parent: Inicio (ES)
nav_order: 3
permalink: /es/guia-studio/
---

# Studio — consola web

OpenADS Studio es una consola web estilo phpMyAdmin embebida en
el binario `openads_serverd`. Corre donde corre el daemon
(Windows, Linux, macOS) y se accede desde cualquier navegador
de la red — sin cliente nativo que instalar.

## Habilitar + arrancar

```sh
cmake -S . -B build/http -DOPENADS_WITH_HTTP=ON
cmake --build build/http --target openads_serverd --config Release

./build/http/tools/serverd/openads_serverd \
    --port 6262 \
    --http-port 6263 \
    --data /ruta/a/tus/datos \
    --http-user admin:secret      # opcional — registra un login
```

Después abre `http://<host-servidor>:6263/`.

![Pestaña inicio de Studio](/OpenADS/assets/img/studio/01-home.png)

## Header

La barra superior tiene:

- **Selector de idioma** (`EN` / `ES` / `PT`) — UI cambia en vivo;
  persistido en `localStorage`.
- **🌙 / ☀ tema** — alterna paleta dark / light (CSS variables;
  persistido en `localStorage`).
- **📖 Docs** — link a este sitio.
- **Status** — resumen del dataset actual o último error.

## Sidebar

El sidebar izquierdo lista cada `*.dbf` del directorio. Tres
botones junto al título **Tables**:

| Botón | Acción |
|-------|--------|
| `↻` | Refrescar lista. |
| `⇪` | File picker nativo; subida multi-fichero vía `POST /api/upload`. |
| `+` | Modal Nueva tabla (columna por columna → CREATE TABLE DDL). |

Una segunda sección **Server / Info** enlaza a la pestaña Server.

## Pestañas

| Pestaña | Función |
|---------|---------|
| **Browse**    | Grid paginado de registros. Click en cabecera ordena; filtro encima del grid acota filas en la página actual. Botones Editar / Borrar / Recall por fila. Click en celda abre modal con valor completo (memo / texto largo). |
| **Structure** | Metadatos columnas + recuento + tamaño en disco. Botones Reindex / Pack / Zap / Download / Encrypt / Drop. Form 'Create index' inline (tag + expresión + DESC + UNIQUE). Lista de archivos compañeros (`.cdx`, `.ntx`, `.fpt`, `.dbt`, `.dbv`). |
| **Insert**    | Formulario auto-generado por schema; añade un registro. |
| **SQL**       | Editor SQL libre. Ctrl+Enter ejecuta. Ctrl+Up / Ctrl+Down recupera historial. Export CSV. Errores muestran mensaje del parser + hint 'did you mean…?' si la query mezcla comillas. |
| **Server**    | Versión motor + dir datos + lista tablas + breakdown bytes en disco (DBF / sidecar / total) + count diccionarios. |
| **Sessions**  | Registro vivo de cada sesión wire activa: peer IP / port, user, dir, tiempo conectado, idle, frames in/out, tablas abiertas. Auto-refresh 3 s. |
| **Dict**      | Browse / edit Data Dictionary `.add`: dropdown selector, lista TABLE / USER / INDEX / LINK / RI / DBPROP; forms add/remove; New-dict + Drop-dict. |

### Browse

![Pestaña Browse — filas paginadas de employees.dbf](/OpenADS/assets/img/studio/02-browse.png)

### Structure

![Pestaña Structure — columnas + botones Reindex / Pack / Zap](/OpenADS/assets/img/studio/03-structure.png)

### Insert

![Pestaña Insert — formulario por schema](/OpenADS/assets/img/studio/04-insert.png)

### SQL

![Pestaña SQL — query + grid resultado](/OpenADS/assets/img/studio/05-sql.png)

### Server

![Pestaña Server — info motor + breakdown disco](/OpenADS/assets/img/studio/06-server.png)

### Sessions

![Pestaña Sessions — conexiones wire vivas](/OpenADS/assets/img/studio/07-sessions.png)

### Dict

![Pestaña Dict — CRUD Data Dictionary](/OpenADS/assets/img/studio/08-dd.png)

## Enlaces directos URL

| Param        | Efecto |
|--------------|--------|
| `?table=<n>`                      | Pre-selecciona tabla en sidebar. |
| `?tab=<browse\|structure\|insert\|sql\|server\|sessions\|dd>` | Pre-abre pestaña. |
| `?q=<sql-urlencoded>`             | Pre-rellena editor (con `tab=sql`). |
| `&autorun=1`                      | Ejecuta query al cargar. |

## API REST

Mismo subset documentado en EN — cada panel se apoya en
endpoints REST scriptables desde Python / curl.

## Autenticación

Cuando se pasa `--http-user user:password` (repetible), cada
request requiere `Authorization: Basic …`. El navegador muestra
prompt nativo. Sin `--http-user` la consola es abierta.

## Despliegues típicos

- **Admin local**: `--http-port 6263`, abre `localhost:6263`.
- **Admin LAN**: misma flag, abre `http://servidor.lan:6263`.
- **Admin remoto vía SSH**: `ssh -L 6263:localhost:6263 servidor`,
  abre `localhost:6263`. SSH cifra y autentica el túnel.
- **Móvil**: cualquier navegador responsive accede al mismo
  endpoint — el CSS escala a viewports de teléfono.

## Hitos Studio

| Tag                | Scope |
|--------------------|-------|
| `studio.web.0.1`   | Skeleton: connect, lista tablas, editor SQL, grid resultado. |
| `studio.web.0.2`   | CRUD + browse paginado + pestaña Server. |
| `studio.web.0.3`   | CREATE / DROP table + Encrypt + historial SQL persistente. |
| `studio.web.0.4`   | Sessions tab. |
| `studio.web.0.5`   | Data Dictionary tab + REST. |
| `studio.web.0.6`   | Reindex / Pack / Zap + CREATE INDEX wizard + memo viewer. |
| `studio.web.0.7`   | Sidecar list + server-stats + DBF upload + refresh. |
| `studio.web.0.8`   | HTTP Basic auth + table download + theme toggle. |
| `studio.web.0.9`   | Browse sort + filter + i18n (EN / ES / PT). |
