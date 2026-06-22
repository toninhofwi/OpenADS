---
title: Stored procedures
layout: default
parent: Inicio (ES)
nav_order: 9
permalink: /es/stored-procedures/
---

# Stored procedures (procedimientos almacenados)

OpenADS soporta dos tipos de stored procedures, ambos invocados a
través de la superficie SQL (`AdsExecuteSQLDirect`):

1. **Procedimientos AEP personalizados** — tu propio código en una
   biblioteca externa (`.dll` / `.so` / `.dylib`).
2. **Procedimientos `sp_*` integrados** — procedimientos de sistema
   que operan sobre el Data Dictionary.

> **Nota sobre la API.** El primer argumento de
> `AdsExecuteSQLDirect` es un **handle de statement** creado con
> `AdsCreateSQLStatement`, no un handle de conexión.

## 1. Procedimientos AEP personalizados

### Registrar

```clipper
LOCAL hStmt, hCur
AdsCreateSQLStatement( hConn, @hStmt )
AdsExecuteSQLDirect( hStmt, "CREATE PROCEDURE my_sum AS 'mylib.dll::sum_proc'", @hCur )
```

`CREATE PROCEDURE <nombre> AS '<biblioteca>::<función>'` registra
el procedimiento en el registro AEP por conexión. La biblioteca se
carga dinámicamente y el símbolo se resuelve al ejecutar.

### Implementar (C ABI)

La función exportada debe tener exactamente esta firma:

```c
extern "C" int sum_proc(const char* args, char* out_buf, size_t out_cap);
```

- `args` — los argumentos unidos por el byte `\x1F` (Unit
  Separator). `EXECUTE PROCEDURE p('a', 'b')` llega como
  `"a\x1Fb"`.
- `out_buf` / `out_cap` — escribe aquí el resultado (terminado en
  NUL, limitado a `out_cap`).
- **Valor de retorno** — `0` = éxito; cualquier valor distinto de
  cero hace que `AdsExecuteSQLDirect` falle.

### Ejecutar

```clipper
AdsExecuteSQLDirect( hStmt, "EXECUTE PROCEDURE my_sum(5, 7)", @hCur )
```

El resultado se devuelve como un cursor de una fila con un campo
`RESULT`:

```clipper
AdsGotoTop( hCur )
AdsGetField( hCur, "RESULT", @cBuf, @nCap, 0 )   // -> "12"
```

## 2. Procedimientos `sp_*` integrados

Operan sobre el Data Dictionary y requieren una conexión con DD
abierto (si no, devuelven `AE_FUNCTION_NOT_AVAILABLE`).

| Procedimiento | Acción |
|---------------|--------|
| `sp_CreateUser` | Crear usuario en el DD (password y comentario opcionales) |
| `sp_DropUser` | Eliminar usuario |
| `sp_CreateGroup` | Crear grupo |
| `sp_DropGroup` | Eliminar grupo |
| `sp_AddUserToGroup` | Asignar usuario a grupo |
| `sp_RemoveUserFromGroup` | Quitar usuario de grupo |
| `sp_ModifyUserProperty` | Cambiar password / comentario / propiedades del usuario |
| `sp_ModifyGroupProperty` | Cambiar propiedades del grupo |
| `sp_AddTableToDatabase` | Registrar tabla (y sus índices) en el DD |
| `sp_AddIndexFileToDatabase` | Registrar archivo de índice en el DD |
| `sp_ModifyTableProperty` | Cambiar propiedades de tabla |
| `sp_ModifyFieldProperty` | Cambiar propiedades de campo (required, default, validación…) |
| `sp_CreateReferentialIntegrity` | Crear regla de RI |
| `sp_DropReferentialIntegrity` | Eliminar regla de RI |
| `sp_CreateLink` | Crear link a otro DD |
| `sp_DropLink` | Eliminar link |
| `sp_EnableTriggers` / `sp_DisableTriggers` | Habilitar / deshabilitar triggers (scope conexión, tabla, trigger único, o `ALL`) |
| `sp_ModifyDatabase` | Modificar propiedades del DD (admin password, comentario, default table path…) |

### Ejemplo

```clipper
AdsCreateSQLStatement( hConn, @hStmt )
AdsExecuteSQLDirect( hStmt, "EXECUTE PROCEDURE sp_CreateUser('admin','secret')", @hCur )
```
