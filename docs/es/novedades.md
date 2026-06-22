---
title: Novedades
layout: default
parent: Inicio (ES)
nav_order: 0
permalink: /es/novedades/
---

# Novedades (v1.0.0-rc29 → rc31)

Esta página resume los cambios más destacados desde la versión
v1.0.0-rc29. Para el historial completo de commits, consulta el
[CHANGELOG](https://github.com/FiveTechSoft/OpenADS/blob/main/CHANGELOG.md).

---

## Nuevas Funcionalidades

### Driver de Tablas con SQLite

Un driver alternativo de tablas respaldado por SQLite está
disponible detrás del flag de CMake `OPENADS_WITH_SQLITE`. Cuando
se habilita, el motor puede abrir y manipular tablas a través de
un backend SQLite, proporcionando una capa de almacenamiento
alternativa. Nuevos archivos fuente:

- `src/sql_backend/sqlite_backend.cpp`
- `src/sql_backend/sqlite_connection.cpp`
- `src/sql_backend/sqlite_table.h`
- `src/sql_backend/sqlite_index.h`

### Parches de Validación ADT (F1–F7, R1–R3)

La validación de tablas ADT ahora incluye un conjunto completo
de parches estructurales (F1–F7) y verificaciones a nivel de
registro (R1–R3), reforzando las garantías de integridad para
archivos `.adt` producidos por SAP Advantage.

### Creación, Lectura, Escritura y Búsqueda en Índice ADT/ADI Nativo

OpenADS ahora puede operar de extremo a extremo con archivos
nativos `.adt` / `.adi` / `.adm`:

- **Crear** — `AdsCreateTable(ADS_ADT)` escribe una cabecera de
  tabla válida, descriptores de campo y un almacén de memo
  `.adm` opcional.
- **Escribir** — `AdsAppendRecord` / `AdsWriteRecord` persisten
  filas y payloads de memo.
- **Leer** — Reabrir, obtener campos, conteo de registros,
  ida y vuelta de memo.
- **Índice** — `AdsCreateIndex61` construye bolsas `.adi`
  (primer tag vía `AdiIndex::create`, tags adicionales vía
  `add_tag`).
- **Buscar** — `AdsSeek` en claves ADI de carácter y numéricas.
- **AUTOINC** — contador sembrado desde filas existentes al
  abrir; bytes 139–143 del descriptor permanecen en cero en
  disco.
- **Layout de memo ADM** — bloques de 8 bytes con un prefijo
  de metadatos de 1024 bytes.

### Ruta de Escritura ADI

El driver de índices ADI ahora soporta operaciones de escritura
— `insert`, `erase` y `flush` — incluyendo la decodificación de
recno de hoja densa y búsqueda de claves de carácter. Esto
completa el ciclo de lectura-escritura para índices ADI.

### Despacho de Triggers (BEFORE / INSTEAD_OF / AFTER)

Los triggers ahora se ejecutan con el despacho de temporización
adecuado:

- **BEFORE** — se ejecuta antes de la sentencia DML.
- **INSTEAD_OF** — reemplaza la DML en vistas.
- **AFTER** — se ejecuta tras la ejecución exitosa.

Se soportan orden por prioridad, tabla `__error` para fallos,
procedimientos almacenados `sp_DisableTriggers` /
`sp_EnableTriggers`, y claves compuestas de triggers en
`system.triggers`.

### Interfaz de Gestión DA-Web

El reemplazo del Data Architect basado en navegador (**DA-Web**)
ha recibido un trabajo extenso:

- **Edición en línea de celdas** con seguimiento de cambios
  visuales.
- **Gestión de índices** — guardar y eliminar índices vía API.
- **CRUD de Triggers** — agregar, eliminar y editar triggers con
  validación en línea.
- **Barra de filtros AOF (Rushmore)** en el explorador de tablas.
- **Resaltado de sintaxis SQL ADS** con colores similares a
  HeidiSQL.
- **Visor de código de procedimientos almacenados / funciones**
  con parámetros y Save-to-DD.
- **Explorador de tags de índice**, etiquetas de tipos de campo
  y scripts SQL.
- **Menú de conexión** — Nuevo DD, Abrir DD, Tablas libres.
- **Pestañas de Permisos Efectivos** y **Miembros** en paneles de
  usuario/grupo.
- **Desplegables de tags RI** poblados desde archivos `.add`
  binarios.

### openmonitor — TUI y Panel Web

Una nueva herramienta `openmonitor` proporciona tanto una interfaz
de terminal (TUI) como un panel web para monitorear y administrar
`openads_serverd`.

### Extensión Nativa PHP (`php_openads`)

Una extensión nativa Zend PHP (`php_openads.dll`) está ahora
disponible para PHP 8.x, proporcionando CRUD completo del DD (35
nuevos métodos `AdsDictionary`), decodificación de campos
date/timestamp y caché de nombres de campo por sentencia.

### Mejoras en la Importación de Diccionario de Datos SAP

- `import_dd` ahora copia archivos de memo `.am` y decodifica
  cuerpos de funciones encriptados.
- Importación de membresía de grupos (DB:Admin, DB:Backup,
  DB:Debug) desde archivos `.add` binarios.
- Temporización de triggers capturada desde `system.triggers`.
- `grant_permission` y código de error `AE_SAP_PERMS_NEED_IMPORT`
  para migración de permisos.

### Recuperación de Fallos WAL

La recuperación de fallos WAL (Write-Ahead Log) ahora maneja
registros `APPEND`, completando el modelo de recuperación
ARIES-lite.

### Expansión del SQL del DD

- `CREATE DATABASE`, `GRANT` / `REVOKE`.
- Procedimientos almacenados `sp_*`.
- Tablas virtuales `system.*` (`system.iota`, `system.columns`).
- `AdsDDGet/SetFieldProperty`, triggers, procedimientos
  almacenados, vistas y propiedades de índices.
- Control de acceso por tabla con niveles de permiso de
  usuario/grupo.

---

## Correcciones de Errores

### Motor

- **`dbSeek` numérico** — rddads envía tipo de clave
  `ADS_STRING` para búsquedas numéricas; el motor ahora lo
  maneja correctamente.
- **`ALIAS->FIELD` en búsqueda numérica** — elimina el prefijo
  `ALIAS->` para que los tags CDX con alias encuentren claves.
- **Reversión de transacciones** — elimina físicamente los
  registros agregados al revertir en lugar de dejar filas
  fantasma.
- **Fuga de estado LockMgr** — `held_` ahora se limpia al
  desbloquear para que el siguiente bloqueo tome un bloqueo real
  del SO.
- **`AdsGetRecordCount`** — ahora respeta `bFilterOption`.
- **`AdsSetRelation`** — falla honestamente cuando es
  apropiado.
- **`seek_key`** — `walk_to_last` ahora honra `SET DELETED ON`.
- **Navegación de tabla vacía** — maneja correctamente tablas
  con cero registros.
- **Navegación de registros eliminados** — estado correcto tras
  saltar filas eliminadas.
- **Recuento de bloqueos LockMgr** — los bloqueos repetidos sobre
  la misma clave ahora se cuentan por referencia; el bloqueo del
  SO se libera solo cuando el último poseedor desbloquea.
- **Comprobación de límites de registros WAL** —
  `TxLog::read_all` valida la longitud de cada campo
  UPDATE/APPEND antes de leer, evitando lecturas excesivas en
  archivos WAL truncados o corruptos.

### ABI

- **Lectura fuera de límites** en formato de clave numérica —
  prevenida.
- **Bits de opción intercambiados** — `ADS_DESCENDING` (0x02) y
  `ADS_COMPOUND` (0x08) se decodificaban incorrectamente en
  `AdsCreateIndex61`.
- **Resolución de nombres de campo insensible a mayúsculas** —
  `field_index` se cachea para mejor rendimiento.
- **`AE_NO_CURRENT_RECORD`** — devuelve 5068 en lugar de 5026.
- **`OrdListAdd`** — vuelve al nombre base cuando una ruta
  relativa prefija doble el directorio de la tabla.
- **Vínculo de helpers trig** — vínculo C++ para helpers `trig_*`
  para silenciar MSVC C4190.

### Seguridad DA-Web

- Sanitización de expresiones de filtro AOF.
- Corrección de contención de ruta de raíz de unidad.
- Barrido de seguridad de API en todos los endpoints PHP.
- Rutas de índice RI meta contenidas bajo el directorio DD.
- Límites de tamaño de frame wire para prevenir abuso.

### Plataforma / Build

- **macOS** — advertencias de sign-conversion y unused-function
  corregidas.
- **GCC** — advertencias `-Werror` resueltas (shadow, implicit
  conversion, format-truncation, stringop-truncation).
- **MSVC** — decoración `__stdcall` de x86 y crash de `_wfsopen`
  en x64 corregidos.
- **Clang** — guardia `-Wc2y-extensions` para Apple Clang
  anterior.
- **Colisión de fd 0 en POSIX** — los manejadores de archivo
  ahora se almacenan como `(fd + 1)` para que un fd 0 real
  (devuelto por `open()` cuando stdin está cerrado) no se
  confunda con el centinela "no abierto".
- **Reintento `EINTR` en POSIX** — `pread` / `pwrite` reintentan
  ante interrupción por señal en lugar de fallar la E/S.
- **mmap de longitud cero en POSIX** — `map_readonly` rechaza
  mapeos de longitud cero en lugar de llamar a `mmap` con
  longitud 0.

### CDX

- Bloqueo de cabecera compartido al abrir para que las
  inserciones concurrentes no fallen.
- CDX estructural nombrado por tabla, no por sesión de
  directorio.

### Remoto (Wire)

- Crash de use-after-free en consultas de tablas virtuales vía
  TCP eliminado.

---

## Documentación

- **CONTRIBUTING.md** — nueva guía de contribución con flujo de
  PR, política de protocolo y reglas de clean-room.
- **Wire Protocol DD API** — referencia completa de la API del
  Diccionario de Datos (§9) agregada.
- **DA-Web GUIDE.md** — guía completa de usuario para la
  interfaz de navegador DA-Web.
- **README** — actualización completa del estado post-rc29
  cubriendo ruta de escritura ADI, creación ADT, modo AES y
  alcance de DA-Web.

---

## Pruebas

- **564 pruebas unitarias** pasando en todas las plataformas
  (48 127 aserciones).
- Nuevos archivos de prueba: `abi_adi_create_test.cpp` (creación
  ADI, multi-tag, skip/seek poblado) y
  `abi_adt_scope_validation_test.cpp` (creación desde cero,
  búsqueda dual-tag, ida y vuelta de memo, stress append, wire
  remoto, subproceso serverd).
- Smoke test de índices NTX de Wilson agregado.
- Demo de Harbour en `examples/adt-native/` (por glokcode).
