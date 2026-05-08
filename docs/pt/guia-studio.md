---
title: Studio (console web)
layout: default
parent: Início (PT)
nav_order: 3
permalink: /pt/guia-studio/
---

# Studio — console web

OpenADS Studio é um console web estilo phpMyAdmin que lista as
tabelas da conexão, mostra o esquema, executa SQL ad-hoc e
inspeciona registros (incluindo campos memo / binários). Vem em
dois modos:

- **Modo Remote-Server** — embutido em `openads_serverd.exe`.
  O daemon expõe simultaneamente o protocolo wire OpenADS (TCP)
  e o listener HTTP do Studio. Recomendado para implantações
  compartilhadas / multi-usuário.
- **Modo LocalServer** — embutido em `ace64.dll` / `ace32.dll`.
  Uma aplicação Harbour / X# / Clipper que carrega a DLL OpenADS
  diretamente passa a ter o mesmo console web Studio dentro do
  próprio processo, sem precisar de daemon separado. Recomendado
  para apps desktop monousuário, sessões de depuração ou para
  inspecionar um processo Clipper em execução pelo navegador.

## Habilitar + iniciar — Remote Server (`openads_serverd`)

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

## Habilitar + iniciar — LocalServer (in-process)

O Studio é compilado dentro de `openads_ace` (i.e. `ace64.dll` /
`ace32.dll`) quando o build é configurado com
`-DOPENADS_WITH_HTTP=ON`. Três entry points exclusivos do OpenADS
controlam o console in-process:

```c
UNSIGNED32 AdsStudioStart(UNSIGNED16 usPort, UNSIGNED8* pucDataDir);
UNSIGNED32 AdsStudioStop (void);
UNSIGNED32 AdsStudioPort (UNSIGNED16* pusPort);
```

Duas formas de habilitar:

**1) Programaticamente.** A partir da aplicação host (qualquer
linguagem capaz de chamar a ABI C — Harbour, X#, Clipper, C++,
Python via `ctypes`, …):

```c
AdsStudioStart(8080, (UNSIGNED8*)"C:\\app\\dados");
/* ... ShellExecute("http://localhost:8080") ... */
AdsStudioStop();
```

`AdsStudioStart` retorna `AE_SUCCESS` (0) em caso de sucesso,
`AE_INTERNAL_ERROR` quando bind / listen falha (porta ocupada ou
`pucDataDir == NULL`), ou `AE_FUNCTION_NOT_AVAILABLE` quando a
DLL foi compilada sem `-DOPENADS_WITH_HTTP=ON`.

**2) Auto-start via variável de ambiente.** Defina
`OPENADS_STUDIO_PORT=<porta>` antes de iniciar a app host e a
DLL inicia o Studio automaticamente ao carregar:

```bat
set OPENADS_STUDIO_PORT=8080
set OPENADS_STUDIO_DATA=C:\app\dados      :: padrão = "."
set OPENADS_STUDIO_HOST=127.0.0.1         :: padrão = 127.0.0.1
start MeuApp.exe
```

O hook de auto-start roda em `DllMain DLL_PROCESS_ATTACH` no
Windows e em um constructor attribute no POSIX. Sem
`OPENADS_STUDIO_PORT` o hook é no-op — a DLL não vincula porta
alguma a menos que o host peça explicitamente. Falhas de bind
durante o auto-start são silenciosas para que o processo host
nunca falhe ao carregar por colisão de porta do Studio; o
`AdsStudioStart()` explícito retorna `AE_INTERNAL_ERROR` nesse
caso.

### Locking + acesso compartilhado

Studio abre tabelas somente leitura via conexões ABI de curta
duração. Se sua aplicação mantém uma tabela em modo EXCLUSIVE, o
navegador verá erro "table busy" para essa tabela até que a app
libere o lock exclusivo. Aberturas compartilhadas convivem sem
problema, então o padrão Harbour `USE … SHARED` funciona direto.

### Host de bind padrão

O host de bind padrão é `127.0.0.1`, **não** `0.0.0.0` — Studio
fica local-only por padrão, então uma app desktop que carregue
a DLL não expõe silenciosamente o diretório de dados na LAN.
Defina `OPENADS_STUDIO_HOST=0.0.0.0` (ou passe um host
explícito via wrapper) quando precisar de visibilidade LAN, e
combine com HTTP Basic auth (Remote Server registra usuários
via `--http-user`; LocalServer mantém o console aberto por
design — coloque atrás de um reverse proxy se tiver de servir
algo além de `localhost`).

![Aba inicial do Studio](/OpenADS/assets/img/studio/01-home.png)

## Header

A barra superior tem:

- **Seletor de idioma** (`EN` / `ES` / `PT`) — UI muda em tempo
  real; persistido em `localStorage`.
- **🌙 / ☀ tema** — alterna paleta dark / light (CSS variables;
  persistido em `localStorage`).
- **📖 Docs** — link para este site.
- **Badge de modo** — 🏠 `LocalServer` (verde) quando o console
  roda in-process dentro de `ace64.dll` / `ace32.dll`, ou
  🌐 `Remote Server` (azul) quando hospedado por `openads_serverd`.
  Hover sobre o badge mostra o diretório de dados ativo. O sinal
  vem do campo `mode` de `/api/health`.
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

### Browse — filtro AOF (Rushmore)

Uma segunda toolbar acima do grid traz um input **AOF (Rushmore)
filter**, botão **Apply**, botão **Clear** e um badge com o
OptLevel. Digite uma condição estilo Clipper:

```
AGE >= 25
NAME = 'SMITH' .AND. ACTIVE = .T.
TAG BETWEEN 'AAAA' AND 'CCCC'
CITY IN ('NYC', 'LON', 'TOK')
```

pressione Apply, e o grid pagina apenas os registros que passam
(`Skip` / `GoTop` respeitam o bitmap AOF, então Next / Prev
percorrem o mesmo conjunto filtrado). O badge reflete o que
`AdsGetAOFOptLevel` reporta:

| Badge | OptLevel | Significado |
|-------|----------|-------------|
| 🟢 `OptLevel: FULL` | `ADS_OPTIMIZED_FULL` | Cada folha servida por range-scan de índice — janela Rushmore completa. |
| 🟡 `OptLevel: PART` | `ADS_OPTIMIZED_PART` | Algumas folhas por índice, outras por avaliação AST por registro. |
| ⚪ `OptLevel: NONE` | `ADS_OPTIMIZED_NONE` | Nenhuma folha sobre índice — bitmap construído por full-scan. |
| ❌ `<error>`        | parse / não suportado | `AdsSetAOF` rejeitou a condição. |

Clear restaura o walk completo. A condição é reencaminhada em
cada page fetch como `?aof=<cond>`.

Gramática V1 aceita por `AdsSetAOF`:

```
<campo> OP <literal>      OP em { = == != <> # < <= > >= }
<campo> BETWEEN a AND b
<campo> IN ( v1, v2, ... )
expr AND expr             também `.AND.` (Clipper)
expr OR  expr             também `.OR.`
NOT expr                  também `.NOT.` e `!`
( expr )
```

Folhas aceleradas por índice em V1: campos character / memo com
índice cuja expressão seja bare-field-name. Numeric / date /
logical, e índices com `UPPER(field)` / compound, produzem
bitmap correto via fallback per-record — mas não contam como
"served by index" no OptLevel.

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
