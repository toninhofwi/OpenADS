---
title: Primeros pasos
layout: default
parent: Inicio (ES)
nav_order: 1
permalink: /es/primeros-pasos/
---

# Primeros pasos

OpenADS es un proyecto CMake en C++17. Compila en Windows
(MSVC), Linux (clang o gcc) y macOS (AppleClang).

## Compilar

```sh
git clone https://github.com/FiveTechSoft/OpenADS
cd OpenADS
cmake --preset default
cmake --build build/default --config Release
ctest --test-dir build/default --output-on-failure -C Release
```

Binarios generados:

- `ace64.dll` (Windows) / `libace.so` (Linux) / `libace.dylib`
  (macOS) bajo `build/default/src/Release/` — el reemplazo
  directo del ACE.
- `tools/serverd/openads_serverd` — CLI servidor TCP.
- `tools/bench/openads_bench` — temporizador de cargas SQL
  multi-plataforma.

## Opciones del build

- `OPENADS_WITH_HTTP=ON` (**por defecto desde v1.0.0-rc20**) —
  compila la consola web **Studio** dentro de `openads_serverd`
  *y* dentro de `ace64.dll` / `ace32.dll` (modo LocalServer).
  Pasa `-DOPENADS_WITH_HTTP=OFF` para excluirla.
- `cmake -DOPENADS_WITH_TLS=ON …` — habilita URIs `tls://` en
  `AdsConnect60`. Empaqueta `mbedtls 3.6 LTS` (Apache-2.0) y la
  **enlaza estáticamente** desde v1.0.0-rc8 — cero dependencias
  runtime de `libssl` / `libcrypto` / `mbedtls`.

El ZIP de release Windows incluye **ambos** `ace64.dll` (x64) y
`ace32.dll` (x86) más `openads_serverd_{x64,x86}.exe` desde
v1.0.0-rc8, así que apps X#, Harbour-x86 y Clipper legacy eligen
la bitness correcta desde una sola descarga.

## Smoke test (drop-in)

Coloca `ace64.dll` (o `libace.so`) en el `PATH` de una
aplicación Harbour antes de cualquier copia de SAP. Las
llamadas existentes de `contrib/rddads` ahora caen en OpenADS.

## Smoke test (servidor TCP + Studio)

```sh
cmake --preset default
cmake --build build/default --target openads_serverd --config Release

./build/default/tools/serverd/openads_serverd \
    --port 6262 \
    --http-port 6263 \
    --data /ruta/a/tus/datos
```

Después abre `http://localhost:6263/` en cualquier navegador.

## Studio LocalServer (in-process)

Desde v1.0.0-rc9 la misma consola Studio queda embebida en
`ace64.dll` / `ace32.dll`. Una app Harbour / X# / Clipper que
cargue la DLL OpenADS obtiene la SPA en su propio proceso — sin
daemon. Define `OPENADS_STUDIO_PORT=8080` antes de lanzar la app
para auto-arranque, o llama a `AdsStudioStart(port, data_dir)`
desde el código host. Detalles en [Studio](guia-studio/).

## Ejecutar `openads_serverd` como servicio

Desde v1.0.0-rc14:

- **Windows**: `openads_serverd --install-service` (auto-start
  vía SCM); `--uninstall-service` lo retira.
- **Linux**: `scripts/openads-serverd.service` es una unit
  systemd hardened (`User=openads`, `ProtectSystem=strict`,
  `NoNewPrivileges`).
- **macOS**: `scripts/com.openads.serverd.plist` es un launchd
  plist con KeepAlive on crash.

Detalle en [Despliegue como servicio](servicio-despliegue/).
