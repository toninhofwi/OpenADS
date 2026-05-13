---
title: Primeiros passos
layout: default
parent: Início (PT)
nav_order: 1
permalink: /pt/primeiros-passos/
---

# Primeiros passos

OpenADS é um projeto CMake em C++17. Compila no Windows
(MSVC), Linux (clang ou gcc) e macOS (AppleClang).

## Compilar

```sh
git clone https://github.com/FiveTechSoft/OpenADS
cd OpenADS
cmake --preset default
cmake --build build/default --config Release
ctest --test-dir build/default --output-on-failure -C Release
```

Binários gerados:

- `ace64.dll` (Windows) / `libace.so` (Linux) / `libace.dylib`
  (macOS) em `build/default/src/Release/` — o substituto direto
  do ACE.
- `tools/serverd/openads_serverd` — CLI servidor TCP.
- `tools/bench/openads_bench` — temporizador de cargas SQL
  multi-plataforma.

## Opções de build

- `OPENADS_WITH_HTTP=ON` (**padrão desde v1.0.0-rc20**) — compila o
  console web **Studio** dentro de `openads_serverd` *e* dentro de
  `ace64.dll` / `ace32.dll` (modo LocalServer). Use
  `-DOPENADS_WITH_HTTP=OFF` para excluir.
- `cmake -DOPENADS_WITH_TLS=ON …` — habilita URIs `tls://` em
  `AdsConnect60`. Empacota `mbedtls 3.6 LTS` (Apache-2.0) e o
  **linka estaticamente** desde v1.0.0-rc8 — zero dependência
  runtime de `libssl` / `libcrypto` / `mbedtls`.

O ZIP de release Windows traz **ambos** `ace64.dll` (x64) e
`ace32.dll` (x86) mais `openads_serverd_{x64,x86}.exe` desde
v1.0.0-rc8, então apps X#, Harbour-x86 e Clipper legacy escolhem
o bitness correto numa só baixada.

## Smoke test (drop-in)

Coloque `ace64.dll` (ou `libace.so`) no `PATH` da aplicação
Harbour antes de qualquer cópia da SAP. As chamadas existentes
de `contrib/rddads` agora caem no OpenADS.

## Smoke test (servidor TCP + Studio)

```sh
cmake --preset default
cmake --build build/default --target openads_serverd --config Release

./build/default/tools/serverd/openads_serverd \
    --port 6262 \
    --http-port 6263 \
    --data /caminho/dos/seus/dados
```

Depois abra `http://localhost:6263/` em qualquer navegador.

## Studio LocalServer (in-process)

Desde v1.0.0-rc9 o mesmo console Studio fica embutido em
`ace64.dll` / `ace32.dll`. Uma app Harbour / X# / Clipper que
carregue a DLL OpenADS recebe a SPA dentro do próprio processo —
sem daemon. Defina `OPENADS_STUDIO_PORT=8080` antes de iniciar a
app para auto-arrancar, ou chame `AdsStudioStart(port, data_dir)`
no código host. Detalhes em [Studio](guia-studio/).

## Rodar `openads_serverd` como serviço

Desde v1.0.0-rc14:

- **Windows**: `openads_serverd --install-service` (auto-start
  via SCM); `--uninstall-service` remove.
- **Linux**: `scripts/openads-serverd.service` — unit systemd
  hardened (`User=openads`, `ProtectSystem=strict`,
  `NoNewPrivileges`).
- **macOS**: `scripts/com.openads.serverd.plist` — launchd plist
  com KeepAlive on crash.

Detalhes em [Implantação como serviço](servico-implantacao/).
