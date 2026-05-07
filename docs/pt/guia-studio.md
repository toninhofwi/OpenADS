---
title: Studio (console web)
layout: default
parent: Início (PT)
nav_order: 3
permalink: /pt/guia-studio/
---

# Studio — console web

OpenADS Studio é um console web no estilo phpMyAdmin embutido
no binário `openads_serverd`. Roda onde o daemon roda (Windows,
Linux, macOS) e é acessível de qualquer navegador na rede —
sem cliente nativo para instalar.

## Habilitar + iniciar

```sh
cmake -S . -B build/http -DOPENADS_WITH_HTTP=ON
cmake --build build/http --target openads_serverd --config Release

./build/http/tools/serverd/openads_serverd \
    --port 6262 \
    --http-port 6263 \
    --data /caminho/dos/seus/dados \
    --http-user admin:secret      # opcional — registra um login
```

Depois abra `http://<host-servidor>:6263/`.

![Aba inicial do Studio](/OpenADS/assets/img/studio/01-home.png)

## Header

A barra superior tem:

- **Seletor de idioma** (`EN` / `ES` / `PT`) — UI muda em tempo
  real; persistido em `localStorage`.
- **🌙 / ☀ tema** — alterna paleta dark / light (CSS variables;
  persistido em `localStorage`).
- **📖 Docs** — link para este site.
- **Status** — resumo do dataset atual ou último erro.

## Sidebar

A barra lateral lista cada `*.dbf` do diretório. Três botões
junto ao título **Tables**:

| Botão | Ação |
|-------|------|
| `↻` | Atualizar lista. |
| `⇪` | File picker nativo; upload multi-arquivo via `POST /api/upload`. |
| `+` | Modal Nova tabela (coluna por coluna → CREATE TABLE DDL). |

Uma segunda seção **Server / Info** linka à aba Server.

## Abas

| Aba | Função |
|-----|--------|
| **Browse**    | Grid paginado de registros. Click no cabeçalho ordena; filtro acima do grid restringe linhas da página atual. Botões Editar / Apagar / Recall por linha. Click numa célula abre modal com valor completo (memo / texto longo). |
| **Structure** | Metadados de colunas + contagem + tamanho. Botões Reindex / Pack / Zap / Download / Encrypt / Drop. Form 'Create index' inline (tag + expressão + DESC + UNIQUE). Lista arquivos companheiros (`.cdx`, `.ntx`, `.fpt`, `.dbt`, `.dbv`). |
| **Insert**    | Formulário auto-gerado pelo schema; anexa um registro. |
| **SQL**       | Editor SQL livre. Ctrl+Enter executa. Ctrl+Up / Ctrl+Down recupera histórico. Export CSV. Erros mostram mensagem do parser + hint 'did you mean…?' se a query mistura aspas. |
| **Server**    | Versão motor + dir + lista tabelas + breakdown bytes em disco (DBF / sidecar / total) + count dicionários. |
| **Sessions**  | Registro vivo de cada sessão wire ativa: peer IP / port, user, dir, tempo conectado, idle, frames in/out, tabelas abertas. Auto-refresh 3 s. |
| **Dict**      | Browse / edit Data Dictionary `.add`: dropdown, lista TABLE / USER / INDEX / LINK / RI / DBPROP; forms add/remove; New-dict + Drop-dict. |

### Browse

![Aba Browse — linhas paginadas de employees.dbf](/OpenADS/assets/img/studio/02-browse.png)

### Structure

![Aba Structure — colunas + botões Reindex / Pack / Zap](/OpenADS/assets/img/studio/03-structure.png)

### Insert

![Aba Insert — formulário por schema](/OpenADS/assets/img/studio/04-insert.png)

### SQL

![Aba SQL — query + grid resultado](/OpenADS/assets/img/studio/05-sql.png)

### Server

![Aba Server — info motor + breakdown disco](/OpenADS/assets/img/studio/06-server.png)

### Sessions

![Aba Sessions — conexões wire vivas](/OpenADS/assets/img/studio/07-sessions.png)

### Dict

![Aba Dict — CRUD Data Dictionary](/OpenADS/assets/img/studio/08-dd.png)

## Links diretos por URL

| Param        | Efeito |
|--------------|--------|
| `?table=<n>`                      | Pre-seleciona tabela no sidebar. |
| `?tab=<browse\|structure\|insert\|sql\|server\|sessions\|dd>` | Pre-abre aba. |
| `?q=<sql-urlencoded>`             | Pre-preenche editor (com `tab=sql`). |
| `&autorun=1`                      | Executa query ao carregar. |

## API REST

Mesmo subset documentado em EN — cada painel apoia-se em
endpoints REST scriptáveis de Python / curl.

## Autenticação

Quando se passa `--http-user user:password` (repetível), cada
request requer `Authorization: Basic …`. O navegador mostra
prompt nativo. Sem `--http-user` o console é aberto.

## Cenários de implantação

- **Admin local**: `--http-port 6263`, abra `localhost:6263`.
- **Admin LAN**: mesma flag, abra `http://servidor.lan:6263`.
- **Admin remoto via SSH**: `ssh -L 6263:localhost:6263 servidor`,
  abra `localhost:6263`. SSH cifra e autentica o túnel.
- **Mobile**: qualquer navegador responsivo acessa o mesmo
  endpoint — o CSS escala para viewports de celular.

## Marcos do Studio

| Tag                | Escopo |
|--------------------|--------|
| `studio.web.0.1`   | Skeleton: connect, lista tabelas, editor SQL, grid resultado. |
| `studio.web.0.2`   | CRUD + browse paginado + aba Server. |
| `studio.web.0.3`   | CREATE / DROP table + Encrypt + histórico SQL persistente. |
| `studio.web.0.4`   | Sessions tab. |
| `studio.web.0.5`   | Data Dictionary tab + REST. |
| `studio.web.0.6`   | Reindex / Pack / Zap + CREATE INDEX wizard + memo viewer. |
| `studio.web.0.7`   | Sidecar list + server-stats + DBF upload + refresh. |
| `studio.web.0.8`   | HTTP Basic auth + table download + theme toggle. |
| `studio.web.0.9`   | Browse sort + filter + i18n (EN / ES / PT). |
