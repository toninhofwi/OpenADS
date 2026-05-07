---
title: Studio (web console)
layout: default
parent: Home (EN)
nav_order: 3
permalink: /en/studio-guide/
---

# Studio — web console

OpenADS Studio is a phpMyAdmin-style web console embedded in the
`openads_serverd` binary. It runs anywhere the daemon runs
(Windows, Linux, macOS) and is reachable from any browser on
the network — no native client to install.

## Enable + launch

```sh
# Configure with HTTP support
cmake -S . -B build/http -DOPENADS_WITH_HTTP=ON
cmake --build build/http --target openads_serverd --config Release

# Run
./build/http/tools/serverd/openads_serverd \
    --port 6262 \
    --http-port 6263 \
    --data /path/to/your/data \
    --http-user admin:secret      # optional — register a login
```

Then open `http://<server-host>:6263/` in any browser.

![Studio home pane — table list + welcome](/OpenADS/assets/img/studio/01-home.png)

## Header

The top bar carries:

- **Language selector** (`EN` / `ES` / `PT`) — Studio's UI strings
  switch live; choice is persisted in `localStorage`.
- **🌙 / ☀ theme toggle** — flips the SPA between dark and light
  palettes (CSS-variable driven; persisted in `localStorage`).
- **📖 Docs** — link to this site.
- **Status** — current dataset summary or last error.

## Sidebar

The left sidebar lists every `*.dbf` in the data dir. Three small
buttons next to the **Tables** heading:

| Button | Action |
|--------|--------|
| `↻` | Refresh table list. |
| `⇪` | Open native file picker; multi-file upload via `POST /api/upload`. |
| `+` | New-Table modal (column-by-column schema → CREATE TABLE DDL). |

A second section labels **Server / Info** and links to the
Server tab.

## Tabs

| Pane | What it does |
|------|--------------|
| **Browse**    | Paginated grid of records. Click a column header to sort; type in the filter box to narrow rows on the current page. Edit / delete / recall buttons per row. Click any cell to expand it in a modal (memo / long text). |
| **Structure** | Column metadata + record count + file size. Reindex / Pack / Zap / Download / Encrypt / Drop buttons. Inline 'Create index' form (tag + expression + DESC + UNIQUE). Companion-files list (`.cdx`, `.ntx`, `.fpt`, `.dbt`, `.dbv`). |
| **Insert**    | Form auto-generated from the table schema; appends a new record. |
| **SQL**       | Free-form SQL editor. Ctrl+Enter runs. Ctrl+Up / Ctrl+Down recalls history. CSV export. Errors surface the engine's parser message + a 'did you mean…?' hint when the query mixes single + double quotes. |
| **Server**    | Engine version + data dir + table list + on-disk byte breakdown (DBF / sidecar / total) + dictionary count. |
| **Sessions**  | Live registry of every active wire session: peer IP / port, user, data dir, time connected, idle time, frames in / out, open tables. Auto-refreshes every 3 s. |
| **Dict**      | Browse / edit Data Dictionary `.add` files: pick from dropdown, list TABLE aliases / USERs / INDEXes / LINKs / RI rules / DBPROP keys; add / remove forms; New-dict + Drop-dict buttons. |

### Browse

![Browse pane — paginated rows of employees.dbf](/OpenADS/assets/img/studio/02-browse.png)

### Structure

![Structure pane — columns, record count, drop / encrypt / Reindex / Pack / Zap buttons](/OpenADS/assets/img/studio/03-structure.png)

### Insert

![Insert pane — schema-driven form](/OpenADS/assets/img/studio/04-insert.png)

### SQL

![SQL pane — query + result grid](/OpenADS/assets/img/studio/05-sql.png)

### Server

![Server pane — engine info + on-disk breakdown](/OpenADS/assets/img/studio/06-server.png)

### Sessions

![Sessions pane — live wire connections](/OpenADS/assets/img/studio/07-sessions.png)

### Dict

![Dict pane — Data Dictionary CRUD](/OpenADS/assets/img/studio/08-dd.png)

## URL deep links

The SPA reads the following query parameters on load so external
links can land directly on a specific view:

| Param        | Effect |
|--------------|--------|
| `?table=<n>`                      | Pre-selects a table in the sidebar. |
| `?tab=<browse\|structure\|insert\|sql\|server\|sessions\|dd>` | Pre-opens the named tab. |
| `?q=<urlencoded-sql>`             | Pre-fills the SQL editor (only if `tab=sql`). |
| `&autorun=1`                      | Runs the query on load. |
| `?lang=...`                       | (planned) Force language for one session. |

Same surface is useful for embedding **"open this query in
Studio"** links in chat / docs.

## REST API

Each panel is implemented on top of a small REST surface — useful
when scripting against the server from Python / curl / etc.

| Method + path | Purpose |
|---------------|---------|
| `GET /api/health`                          | liveness probe |
| `GET /api/server/info`                     | engine + on-disk byte breakdown |
| `GET /api/server/sessions`                 | active wire sessions |
| `GET /api/tables`                          | list `*.dbf` |
| `POST /api/tables`                         | CREATE TABLE (DDL via SQL) |
| `DELETE /api/tables/<n>`                   | drop file + sidecars |
| `GET /api/tables/<n>/schema`               | column metadata |
| `GET /api/tables/<n>/sidecars`             | companion-file list |
| `GET /api/tables/<n>/rows?offset=&limit=`  | paginated browse |
| `POST /api/tables/<n>/insert`              | append row |
| `POST /api/tables/<n>/update?recno=N`      | overwrite columns |
| `POST /api/tables/<n>/delete?recno=N`      | mark deleted (or `?recall=1` to undelete) |
| `POST /api/tables/<n>/encrypt`             | AES-256-CTR encrypt in place |
| `POST /api/tables/<n>/reindex`             | AdsReindex |
| `POST /api/tables/<n>/pack`                | AdsPackTable |
| `POST /api/tables/<n>/zap`                 | AdsZapTable |
| `POST /api/tables/<n>/index`               | CREATE INDEX wizard |
| `GET /api/tables/<n>/download`             | download DBF |
| `POST /api/upload`                         | multipart upload of files |
| `POST /api/sql`                            | run arbitrary SQL |
| `GET /api/dd`                              | list `*.add` |
| `POST /api/dd`                             | create `{ name }` |
| `GET /api/dd/<n>`                          | full dictionary content |
| `DELETE /api/dd/<n>`                       | drop `.add` |
| `POST /api/dd/<n>/tables`                  | add TABLE alias |
| `DELETE /api/dd/<n>/tables/<alias>`        | remove TABLE alias |
| `POST /api/dd/<n>/users`                   | create USER |
| `DELETE /api/dd/<n>/users/<u>`             | drop USER |
| `POST /api/dd/<n>/dbprop`                  | set `{ key, value }` DB property |

## Authentication

When `--http-user user:password` is passed (repeatable for multiple
accounts), every request must carry an `Authorization: Basic …`
header. The first hit prompts the browser for credentials via the
native login dialog. With no `--http-user` flag the console is
open (development / single-host setups).

## Deployment shapes

- **Local admin**: `--http-port 6263`, browse via `localhost:6263`.
- **LAN admin**: same flag, browse via `http://server.lan:6263`.
- **Remote admin via SSH**: `ssh -L 6263:localhost:6263 server`,
  browse via `localhost:6263`. The SSH tunnel handles encryption
  and authentication; the daemon itself listens on `127.0.0.1`
  inside the remote host.
- **Mobile**: any responsive browser hits the same endpoint —
  Studio's CSS scales to phone-sized viewports.

## Studio milestones

| Tag                | Scope |
|--------------------|-------|
| `studio.web.0.1`   | Skeleton — connect, table tree, SQL editor, basic result grid. |
| `studio.web.0.2`   | CRUD + paginated browse + Server tab. |
| `studio.web.0.3`   | CREATE / DROP table + Encrypt + persisted SQL history. |
| `studio.web.0.4`   | Sessions tab (live wire connections). |
| `studio.web.0.5`   | Data Dictionary tab + REST. |
| `studio.web.0.6`   | Reindex / Pack / Zap + CREATE INDEX wizard + memo viewer. |
| `studio.web.0.7`   | Sidecar list + server-stats breakdown + DBF upload + refresh button. |
| `studio.web.0.8`   | HTTP Basic auth + table download + theme toggle. |
| `studio.web.0.9`   | Browse client-side sort + filter + i18n (EN / ES / PT). |
