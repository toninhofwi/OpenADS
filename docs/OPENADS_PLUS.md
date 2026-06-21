# OpenADS Plus — PostgreSQL

Extensão aditiva do [OpenADS](https://github.com/FiveTechSoft/OpenADS): tabelas PostgreSQL atrás da ABI ACE. DBF e wire inalterados.

## Deploy rápido

```bat
set OPENADS_TOOLCHAIN_ROOT=<dir com MSVC e pgsql\include/lib>
tools\scripts\build_nmake_postgres.bat
```

Saída: `build\pg\src\openace32.dll` — copiar para a pasta do `.exe` Harbour antes de outra `ace*.dll`.

## Conexão

`AdsConnect60("postgresql://user:pass@host:5432/dbname")`

## Teste ponta a ponta

```bat
set OPENADS_TEST_PG_URI=postgresql://user:pass@127.0.0.1:5432/testdb
build\pg\tests\openads_unit_tests.exe --test-case="*postgresql*"
```

O teste cria/derruba a tabela `clientes`, insere 3 linhas e valida navegação + SEEK pela ABI. Sem URI definida, os casos E2E fazem SKIP (CI não quebra).

## Segurança

- Nomes de tabela/coluna: só identificadores ASCII seguros (`[A-Za-z0-9_]`).
- Valores de SEEK e chaves: parâmetros preparados (`$1`), nunca concatenados.
- URI montada em runtime no app — sem paths hardcoded no código.

## Capacidades

| Recurso | Status |
|---------|--------|
| Read + navegação | Sim |
| SEEK por coluna | Sim |
| Write | Planejado |