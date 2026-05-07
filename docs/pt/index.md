---
title: Início (PT)
layout: default
nav_order: 4
permalink: /pt/
has_children: true
---

# OpenADS — Documentação (Português)

OpenADS é uma implementação livre e *clean-room* de um motor
de banco de dados compatível com ADS. Funciona como
**substituto direto** do Advantage Client Engine (`ace32.dll` /
`ace64.dll` / `libace.so`) — aplicações Harbour / Clipper que
fazem link com `contrib/rddads` continuam funcionando sem
recompilar.

## Conteúdo

- **[Primeiros passos](primeiros-passos/)** — instalação,
  primeiro build, smoke test.
- **[Arquitetura](arquitetura/)** — arquitetura de cinco
  camadas (ABI / Sessão / SQL / Motor / Plataforma).
- **[Dicionário de dados](dicionario-dados/)** — formato `.add`
  clean-room + API `engine::DataDict` + superfície REST.
- **[Studio (console web)](guia-studio/)** — administração do
  motor a partir de qualquer navegador através do console HTTP
  embutido em `openads_serverd`.
- **[Benchmarks](benchmarks/)** — números cross-platform SQL
  (Windows MSVC / Linux clang -O3 / macOS AppleClang).

## Outros idiomas

[English](/en/) · [Español](/es/)
