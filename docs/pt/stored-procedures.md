---
title: Stored procedures
layout: default
parent: Início (PT)
nav_order: 9
permalink: /pt/stored-procedures/
---

# Stored procedures (procedimentos armazenados)

OpenADS suporta dois tipos de stored procedures, ambos invocados
através da superfície SQL (`AdsExecuteSQLDirect`):

1. **Procedimentos AEP personalizados** — seu próprio código em
   uma biblioteca externa (`.dll` / `.so` / `.dylib`).
2. **Procedimentos `sp_*` integrados** — procedimentos de sistema
   que operam sobre o Data Dictionary.

> **Nota sobre a API.** O primeiro argumento de
> `AdsExecuteSQLDirect` é um **handle de statement** criado com
> `AdsCreateSQLStatement`, não um handle de conexão.

## 1. Procedimentos AEP personalizados

### Registrar

```clipper
LOCAL hStmt, hCur
AdsCreateSQLStatement( hConn, @hStmt )
AdsExecuteSQLDirect( hStmt, "CREATE PROCEDURE my_sum AS 'mylib.dll::sum_proc'", @hCur )
```

`CREATE PROCEDURE <nome> AS '<biblioteca>::<função>'` registra o
procedimento no registro AEP por conexão. A biblioteca é carregada
dinamicamente e o símbolo resolvido na execução.

### Implementar (C ABI)

A função exportada deve ter exatamente esta assinatura:

```c
extern "C" int sum_proc(const char* args, char* out_buf, size_t out_cap);
```

- `args` — os argumentos unidos pelo byte `\x1F` (Unit
  Separator). `EXECUTE PROCEDURE p('a', 'b')` chega como
  `"a\x1Fb"`.
- `out_buf` / `out_cap` — escreva aqui o resultado (terminado em
  NUL, limitado a `out_cap`).
- **Valor de retorno** — `0` = sucesso; qualquer valor diferente
  de zero faz `AdsExecuteSQLDirect` falhar.

### Executar

```clipper
AdsExecuteSQLDirect( hStmt, "EXECUTE PROCEDURE my_sum(5, 7)", @hCur )
```

O resultado é retornado como um cursor de uma linha com um campo
`RESULT`:

```clipper
AdsGotoTop( hCur )
AdsGetField( hCur, "RESULT", @cBuf, @nCap, 0 )   // -> "12"
```

## 2. Procedimentos `sp_*` integrados

Operam sobre o Data Dictionary e requerem uma conexão com DD
aberto (caso contrário retornam `AE_FUNCTION_NOT_AVAILABLE`).

| Procedimento | Ação |
|--------------|------|
| `sp_CreateUser` | Criar usuário no DD (senha e comentário opcionais) |
| `sp_DropUser` | Excluir usuário |
| `sp_CreateGroup` | Criar grupo |
| `sp_DropGroup` | Excluir grupo |
| `sp_AddUserToGroup` | Adicionar usuário a grupo |
| `sp_RemoveUserFromGroup` | Remover usuário de grupo |
| `sp_ModifyUserProperty` | Alterar senha / comentário / propriedades do usuário |
| `sp_ModifyGroupProperty` | Alterar propriedades do grupo |
| `sp_AddTableToDatabase` | Registrar tabela (e seus índices) no DD |
| `sp_AddIndexFileToDatabase` | Registrar arquivo de índice no DD |
| `sp_ModifyTableProperty` | Alterar propriedades de tabela |
| `sp_ModifyFieldProperty` | Alterar propriedades de campo (required, default, validação…) |
| `sp_CreateReferentialIntegrity` | Criar regra de RI |
| `sp_DropReferentialIntegrity` | Excluir regra de RI |
| `sp_CreateLink` | Criar link para outro DD |
| `sp_DropLink` | Excluir link |
| `sp_EnableTriggers` / `sp_DisableTriggers` | Habilitar / desabilitar triggers (escopo conexão, tabela, trigger único, ou `ALL`) |
| `sp_ModifyDatabase` | Modificar propriedades do DD (admin password, comentário, default table path…) |

### Exemplo

```clipper
AdsCreateSQLStatement( hConn, @hStmt )
AdsExecuteSQLDirect( hStmt, "EXECUTE PROCEDURE sp_CreateUser('admin','secret')", @hCur )
```
