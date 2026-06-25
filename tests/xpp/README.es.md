# Prueba de humo Xbase++ (Alaska) — ACE directo

`test_ads.prg` es una app Xbase++ headless que ejercita la DLL ACE de
OpenADS mediante la **API C ACE directa**, llamada dinámicamente con
`DllPrepareCall` / `DllExecuteCall`. **No** pasa por el motor `ADSDBE`
de Alaska (ver *Por qué no ADSDBE* abajo), así que es una verificación
directa de que la ABI cdecl exportada por OpenADS resuelve y funciona
al llamarse desde Xbase++.

Cada paso se escribe en `test_result.log`; la última línea es
`EXIT 0` si pasa o `EXIT 1` en la primera aserción fallida.
(La salida de consola `?` de Xbase++ va a una ventana PM, no a stdout,
por eso la prueba registra en archivo.)

## Requisitos

- **Alaska Xbase++ 2.0** — compilador `xpp.exe` + enlazador `alink.exe`
  (32-bit), por defecto en
  `C:\Program Files (x86)\Alaska Software\xpp20`. No se incluye aquí.
  Las herramientas de build necesitan una clave de producto válida; una
  licencia trial se activa con `workbench20\asac.exe /p <ProductKey>` y
  luego `asac.exe /a`. Sin ella el compilador falla `XBLM707` y el
  enlazador falla `ALK5001`.
- Una DLL **32-bit** de OpenADS junto a la prueba como `openace32.dll`
  (`build/release-x86/src/Release/openace32.dll`). **Debe ser la DLL de
  OpenADS, no la `ace32.dll` de SAP/Alaska.** Compilala con:

  ```sh
  cmake --build build/release-x86 --config Release --target openads_ace
  ```

## Compilar y ejecutar

```sh
# desde este directorio; run.sh pone el toolchain de Alaska en PATH,
# compila, enlaza, ejecuta headless con timeout y vuelca el log.
bash run.sh
```

`run.sh` acepta un argumento opcional para intercambiar la DLL en
corridas de control:

| comando            | efecto                                              |
|--------------------|-----------------------------------------------------|
| `bash run.sh`      | usa la `openace32.dll` presente                     |
| `bash run.sh openads` | copia `ace32_openads.dll` como `ace32.dll`       |
| `bash run.sh real` | copia la `ace32.dll` real de Alaska (control)       |

(El intercambio apunta a `ace32.dll`; la prueba ACE directa carga
`openace32.dll` por nombre, así que el swap sólo importa para
experimentos estilo ADSDBE.)

## Qué hace

1. `DllPrepareCall` un template tipado por export (cdecl, retorno
   UINT32, `DLL_CALLMODE_COPY` para salidas `@`byref, `RESTOREFPU`).
2. `AdsConnect60` al directorio de datos local (sin servidor).
3. `AdsCreateTable` una tabla CDX `ID,N,8,0;NAME,C,20,0;`.
4. `AdsAppendRecord` ×5; setea campos con `AdsSetString` (el `ID`
   numérico se setea como texto — `set_field` lo convierte, evitando
   pasar un `double` C por `AdsSetDouble`). Verifica
   `AdsGetRecordCount` == 5.
5. `AdsCreateIndex61` un tag CDX `NAME_TAG` sobre `NAME`.
6. `AdsSeek "Charlie"` (clave string, soft seek); lee `RecNo` con
   `AdsGetRecordNum` e `ID` con `AdsGetLong`; verifica `recno==3` e
   `ID==3`.
7. `AdsCloseTable` / `AdsDisconnect`.

## Detalles trampa (documentados como comentarios en la prueba)

- **`ADSHANDLE` es `uint64_t` — incluso en la DLL 32-bit.** Un handle
  pasado *por valor* (entradas `hConn` / `hTable` / `hIndex`, y las
  salidas `@`byref) ocupa **dos** slots de stack de 32 bits.
  Marshalearlo como `UINT32` corre cada argumento siguiente 4 bytes —
  el síntoma fue `AdsCreateTable` fallando con `"no fields"` porque su
  string de campos caía en el slot equivocado. Todos los handles usan
  `DLL_TYPE_INT64`. Las funciones que toman un handle sólo *por
  puntero* (ej. el `phConnect` de `AdsConnect60`) no se ven afectadas —
  por eso connect funcionaba y create no.
- **`DllCall()` descarta silenciosamente argumentos pasados ~8.** El
  `AdsCreateTable` de 10 args perdía los últimos, así que la prueba usa
  la ruta tipada `DllPrepareCall` / `DllExecuteCall`.
- **Las constantes ACE** (`ADS_LOCAL_SERVER`, `ADS_CDX`,
  `ADS_STRINGKEY`, …) se `#define`n localmente. El `ads.ch` de Xbase++
  trae sólo la capa de comandos `AX_*`, no las declaraciones `Ads*`
  crudas ni sus constantes.

## Por qué no ADSDBE

Xbase++ tiene un RDD ADS nativo — el motor **ADSDBE**
(`DbeLoad("ADSDBE")` + `USE` / `INDEX` / `DbSeek` estándar). Sería la
ruta de integración más idiomática, pero es inutilizable headless aquí:

- La `ace32.dll` real de Alaska cuelga en `DbCreate` (el diálogo
  interactivo / de evaluación del servidor local de Advantage).
- La `ace32.dll` de OpenADS cuelga *dentro* de `DbeLoad` — `adsdbe.dll`
  llama a un export de init durante la carga del motor que bloquea (un
  `LoadLibrary` simple de la DLL vía `dllinfo.exe` funciona, así que no
  es el `DllMain` de OpenADS).

Ambas cuelgan en un diálogo GUI sin stdout, lo que no sobrevive a una
corrida desatendida. La ruta ACE directa evita `adsdbe.dll` y el
servidor local por completo. Para una prueba de integración a nivel
RDD, ver la prueba de humo X# (`tests/smoke/xsharp/`), que maneja
OpenADS a través del RDD `AXDBFCDX` de X#.
