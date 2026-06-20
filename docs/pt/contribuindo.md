---
title: Como contribuir
layout: default
parent: Início (PT)
nav_order: 10
permalink: /pt/contribuindo/
---

# Como contribuir

Guia resumido em português. O documento canônico em inglês é
[`CONTRIBUTING.md`](https://github.com/FiveTechSoft/OpenADS/blob/main/CONTRIBUTING.md)
na raiz do repositório.

## Build rápido

```bash
cmake --preset msvc-x64          # Windows
cmake --build build/msvc-x64 --config Release
ctest --test-dir build/msvc-x64 --output-on-failure -C Release
```

Outros presets: `ninja-clang` (Linux), `default` (macOS). Ver
[`CMakePresets.json`](https://github.com/FiveTechSoft/OpenADS/blob/main/CMakePresets.json).

## Fluxo de PR

1. Fork no GitHub.
2. Branch a partir de `upstream/main`.
3. Uma fatia lógica por PR (`docs:`, `fix:`, `test:`, `feat:`).
4. PR contra `FiveTechSoft/OpenADS:main`.

## Política de protocolo (obrigatória)

OpenADS oferece:

1. **ABI local compatível com ACE** — `ace64.dll` drop-in para
   Harbour `contrib/rddads`.
2. **Protocolo wire OpenADS** — formato **paralelo e independente**,
   documentado em [wire-protocol (EN)](/OpenADS/en/wire-protocol/).
   **Não** é compatível byte-a-byte com nenhum protocolo remoto
   proprietário legado.

### Contribuições devem

- Ampliar o wire OpenADS, `openads_serverd` ou o motor local via
  implementação clean-room.
- Usar termos neutros para formatos legados (`.add`, `.adt`) em
  commits, issues e docs **novos**.

### Contribuições não devem

- Buscar compatibilidade byte-a-byte com protocolo remoto legado.
- Incluir disassembly, decompilação ou quebra de ciphers offline.
- Versionar no Git ou em PRs scripts, notas ou capturas dessas
  abordagens.

**O proibido não entra no histórico Git nem em pull requests.**

## Terminologia neutra

| Em vez de marcas de terceiros | Use |
|-------------------------------|-----|
| engine comercial descontinuado | motor ACE legado, engine de referência |
| objetivo | emulação compatível, substituto drop-in |
| remoto | protocolo wire OpenADS (`tcp://` / `tls://`) |

## Checklist antes do push

1. Diff sem dumps, scripts de criptoanálise ou POC de wire legado.
2. `ctest` verde (ou PR marcado `wip:`).
3. Sem dados reais de cliente em fixtures.

## Links

- [Primeiros passos](primeiros-passos/)
- [Arquitetura](arquitetura/)
- [Protocolo wire (EN)](/OpenADS/en/wire-protocol/)
- [TODO.md](https://github.com/FiveTechSoft/OpenADS/blob/main/TODO.md)