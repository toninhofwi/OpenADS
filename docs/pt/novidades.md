---
title: Novidades
layout: default
parent: Início (PT)
nav_order: 0
permalink: /pt/novidades/
---

# Novidades (v1.0.0-rc29 → rc31)

Esta página resume as mudanças mais notáveis desde a versão
v1.0.0-rc29. Para o histórico completo de commits, consulte o
[CHANGELOG](https://github.com/FiveTechSoft/OpenADS/blob/main/CHANGELOG.md).

---

## Novas Funcionalidades

### Driver de Tabelas com SQLite

Um driver alternativo de tabelas suportado por SQLite está
disponível atrás da flag do CMake `OPENADS_WITH_SQLITE`. Quando
habilitado, o motor pode abrir e manipular tabelas através de um
backend SQLite, fornecendo uma camada de armazenamento
alternativa. Novos arquivos fonte:

- `src/sql_backend/sqlite_backend.cpp`
- `src/sql_backend/sqlite_connection.cpp`
- `src/sql_backend/sqlite_table.h`
- `src/sql_backend/sqlite_index.h`

### Patches de Validação ADT (F1–F7, R1–R3)

A validação de tabelas ADT agora inclui um conjunto completo de
patches estruturais (F1–F7) e verificações a nível de registro
(R1–R3), reforçando as garantias de integridade para arquivos
`.adt` produzidos pelo SAP Advantage.

### Criação, Leitura, Escrita e Busca em Índice ADT/ADI Nativo

OpenADS agora pode operar de ponta a ponta com arquivos nativos
`.adt` / `.adi` / `.adm`:

- **Criar** — `AdsCreateTable(ADS_ADT)` escreve um cabeçalho de
  tabela válido, descritores de campo e um armazenamento de memo
  `.adm` opcional.
- **Escrever** — `AdsAppendRecord` / `AdsWriteRecord` persistem
  linhas e payloads de memo.
- **Ler** — Reabrir, obter campos, contagem de registros,
  ida e volta de memo.
- **Índice** — `AdsCreateIndex61` constrói sacolas `.adi`
  (primeiro tag via `AdiIndex::create`, tags adicionais via
  `add_tag`).
- **Buscar** — `AdsSeek` em chaves ADI de caractere e numéricas.
- **AUTOINC** — contador semeado a partir de linhas existentes ao
  abrir; bytes 139–143 do descritor permanecem em zero no disco.
- **Layout de memo ADM** — blocos de 8 bytes com um prefixo de
  metadados de 1024 bytes.

### Caminho de Escrita ADI

O driver de índices ADI agora suporta operações de escrita —
`insert`, `erase` e `flush` — incluindo a decodificação de recno
de folha densa e busca de chaves de caractere. Isso completa o
ciclo de leitura-escrita para índices ADI.

### Despacho de Triggers (BEFORE / INSTEAD_OF / AFTER)

Os triggers agora são executados com o despacho de temporização
adequado:

- **BEFORE** — executa antes da instrução DML.
- **INSTEAD_OF** — substitui a DML em views.
- **AFTER** — executa após a execução bem-sucedida.

São suportados ordenação por prioridade, tabela `__error` para
falhas, procedimentos armazenados `sp_DisableTriggers` /
`sp_EnableTriggers` e chaves compostas de triggers em
`system.triggers`.

### Interface de Gestão DA-Web

O substituto do Data Architect baseado em navegador (**DA-Web**)
recebeu um trabalho extenso:

- **Edição de células em linha** com rastreamento de alterações
  e feedback visual.
- **Gestão de índices** — salvar e excluir índices via API.
- **CRUD de Triggers** — adicionar, excluir e editar triggers
  com validação em linha.
- **Barra de filtros AOF (Rushmore)** no navegador de tabelas.
- **Destaque de sintaxe SQL ADS** com cores semelhantes ao
  HeidiSQL.
- **Visualizador de código de procedimentos armazenados /
  funções** com grade de parâmetros e Save-to-DD.
- **Navegador de tags de índice**, etiquetas de tipos de campo e
  scripts SQL.
- **Menu de conexão** — Novo DD, Abrir DD, Tabelas livres.
- **Abas de Permissões Efetivas** e **Membros** nos painéis de
  usuário/grupo.
- **Dropdowns de tags RI** populados a partir de arquivos `.add`
  binários.

### openmonitor — TUI e Painel Web

Uma nova ferramenta `openmonitor` fornece tanto uma interface de
terminal (TUI) quanto um painel web para monitorar e administrar
`openads_serverd`.

### OpenADS Studio — Responsive (telefone / tablet)

O console web Studio agora se adapta a telas pequenas — pode ser
usado de um telefone ou tablet, não apenas de um navegador
desktop. Abaixo de ~768 px a lista de tabelas vira um **drawer**
deslizante (☰ no header, fundo escurecido, fechamento automático
ao escolher); a barra de abas rola horizontalmente e os
formulários / modais se reorganizam em uma coluna com alvos de
toque maiores. Também corrige um bug antigo do tema escuro — as
variáveis CSS `--panel` / `--panel-2` / `--border` eram
auto-referenciais, então painéis e bordas ficavam transparentes.

### Extensão Nativa PHP (`php_openads`)

Uma extensão nativa Zend PHP (`php_openads.dll`) está agora
disponível para PHP 8.x, fornecendo CRUD completo do DD (35
novos métodos `AdsDictionary`), decodificação de campos
date/timestamp e cache de nomes de campo por instrução.

### Melhorias na Importação de Dicionário de Dados SAP

- `import_dd` agora copia arquivos de memo `.am` e decodifica
  corpos de funções encriptados.
- Importação de membresia de grupos (DB:Admin, DB:Backup,
  DB:Debug) a partir de arquivos `.add` binários.
- Temporização de triggers capturada de `system.triggers`.
- `grant_permission` e código de erro `AE_SAP_PERMS_NEED_IMPORT`
  para migração de permissões.

### Recuperação de Falhas WAL

A recuperação de falhas WAL (Write-Ahead Log) agora lida com
registros `APPEND`, completando o modelo de recuperação
ARIES-lite.

### Expansão do SQL do DD

- `CREATE DATABASE`, `GRANT` / `REVOKE`.
- Procedimentos armazenados `sp_*`.
- Tabelas virtuais `system.*` (`system.iota`, `system.columns`).
- `AdsDDGet/SetFieldProperty`, triggers, procedimentos
  armazenados, views e propriedades de índices.
- Controle de acesso por tabela com níveis de permissão de
  usuário/grupo.

---

## Correções de Erros

### Motor

- **`dbSeek` numérico** — rddads envia tipo de chave
  `ADS_STRING` para buscas numéricas; o motor agora lida com
  isso corretamente.
- **`ALIAS->FIELD` em busca numérica** — remove o prefixo
  `ALIAS->` para que tags CDX com alias encontrem chaves.
- **Reversão de transações** — remove fisicamente os registros
  adicionados ao reverter em vez de deixar linhas fantasma.
- **Vazamento de estado LockMgr** — `held_` agora é limpo ao
  desbloquear para que o próximo bloqueio tome um bloqueio real
  do SO.
- **`AdsGetRecordCount`** — agora respeita `bFilterOption`.
- **`AdsSetRelation`** — falha honestamente quando apropriado.
- **`seek_key`** — `walk_to_last` agora honra `SET DELETED ON`.
- **Navegação de tabela vazia** — lida corretamente com tabelas
  com zero registros.
- **Navegação de registros excluídos** — estado correto após
  pular linhas excluídas.
- **Contagem de bloqueios LockMgr** — bloqueios repetidos na
  mesma chave agora são contados por referência; o bloqueio do
  SO é liberado apenas quando o último detentor desbloqueia.
- **Verificação de limites de registros WAL** —
  `TxLog::read_all` valida o comprimento de cada campo
  UPDATE/APPEND antes de ler, evitando leituras excessivas em
  arquivos WAL truncados ou corrompidos.

### ABI

- **Leitura fora dos limites** em formatação de chave numérica —
  prevenida.
- **Bits de opção trocados** — `ADS_DESCENDING` (0x02) e
  `ADS_COMPOUND` (0x08) eram decodificados incorretamente em
  `AdsCreateIndex61`.
- **Resolução de nomes de campo insensível a maiúsculas** —
  `field_index` agora é cacheado para melhor desempenho.
- **`AE_NO_CURRENT_RECORD`** — retorna 5068 em vez de 5026.
- **`OrdListAdd`** — volta ao nome base quando um caminho
  relativo prefixa duas vezes o diretório da tabela.
- **Vínculo de helpers trig** — vínculo C++ para helpers `trig_*`
  para silenciar MSVC C4190.

### Segurança DA-Web

- Sanitização de expressões de filtro AOF.
- Correção de contenção de caminho de raiz de unidade.
- Varredura de segurança de API em todos os endpoints PHP.
- Caminhos de índice RI meta contidos sob o diretório DD.
- Limites de tamanho de frame wire para prevenir abuso.

### Plataforma / Build

- **macOS** — avisos de sign-conversion e unused-function
  corrigidos.
- **GCC** — avisos `-Werror` resolvidos (shadow, implicit
  conversion, format-truncation, stringop-truncation).
- **MSVC** — decoração `__stdcall` de x86 e crash de `_wfsopen`
  em x64 corrigidos.
- **Clang** — guarda `-Wc2y-extensions` para Apple Clang mais
  antigo.
- **Colisão de fd 0 no POSIX** — os manipuladores de arquivo
  agora são armazenados como `(fd + 1)` para que um fd 0 real
  (retornado por `open()` quando stdin está fechado) não seja
  confundido com a sentinela "não aberto".
- **Repetição `EINTR` no POSIX** — `pread` / `pwrite` repetem
  ante interrupção por sinal em vez de falhar a E/S.
- **mmap de comprimento zero no POSIX** — `map_readonly` rejeita
  mapeamentos de comprimento zero em vez de chamar `mmap` com
  comprimento 0.

### CDX

- Bloqueio de cabeçalho compartilhado ao abrir para que
  inserções concorrentes não falhem.
- CDX estrutural nomeado por tabela, não por sessão de
  diretório.

### Remoto (Wire)

- Crash de use-after-free em consultas de tabelas virtuais via
  TCP eliminado.

---

## Documentação

- **CONTRIBUTING.md** — novo guia de contribuição com fluxo de
  PR, política de protocolo e regras de clean-room.
- **Wire Protocol DD API** — referência completa da API do
  Dicionário de Dados (§9) adicionada.
- **DA-Web GUIDE.md** — guia completo do usuário para a
  interface de navegador DA-Web.
- **README** — atualização completa do estado pós-rc29 cobrindo
  caminho de escrita ADI, criação ADT, modo AES e escopo do
  DA-Web.
- **Cookbook** — nova pasta `cookbook/` com exemplos Harbour
  executáveis e muito comentados (simples → avançado): trilha
  `console/` (xBase puro `ADSCDX` — criar, seek por índice,
  transações, manutenção DBF) e trilha `orm/` (CRUD via um ORM
  Harbour companheiro sobre back-ends SQLite / DBF / PostgreSQL /
  MariaDB / ODBC), além de guias de strings de conexão, tipos de
  campo e solução de problemas.

---

## Testes

- **564 testes unitários** passando em todas as plataformas
  (48 127 asserções).
- Novos arquivos de teste: `abi_adi_create_test.cpp` (criação
  ADI, multi-tag, skip/seek populado) e
  `abi_adt_scope_validation_test.cpp` (criação do zero, busca
  dual-tag, ida e volta de memo, stress append, wire remoto,
  sub processo serverd).
- Smoke test de índices NTX do Wilson adicionado.
- Demo Harbour em `examples/adt-native/` (por glokcode).
