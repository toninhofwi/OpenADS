# Teste de fumaça Xbase++ (Alaska) — ACE direto

`test_ads.prg` é um app Xbase++ headless que exercita a DLL ACE do
OpenADS pela **API C ACE direta**, chamada dinamicamente com
`DllPrepareCall` / `DllExecuteCall`. **Não** passa pelo motor `ADSDBE`
da Alaska (ver *Por que não ADSDBE* abaixo), então é uma verificação
direta de que a ABI cdecl exportada pelo OpenADS resolve e funciona
quando chamada do Xbase++.

Cada passo é gravado em `test_result.log`; a última linha é `EXIT 0`
em caso de sucesso ou `EXIT 1` na primeira asserção que falhar.
(A saída de console `?` do Xbase++ vai para uma janela PM, não para o
stdout, por isso o teste grava em arquivo.)

## Pré-requisitos

- **Alaska Xbase++ 2.0** — compilador `xpp.exe` + linker `alink.exe`
  (32-bit), por padrão em
  `C:\Program Files (x86)\Alaska Software\xpp20`. Não incluído aqui.
  As ferramentas de build exigem uma chave de produto válida; uma
  licença trial é ativada com `workbench20\asac.exe /p <ProductKey>` e
  depois `asac.exe /a`. Sem ela o compilador falha `XBLM707` e o linker
  falha `ALK5001`.
- Uma DLL **32-bit** do OpenADS ao lado do teste como `openace32.dll`
  (`build/release-x86/src/Release/openace32.dll`). **Deve ser a DLL do
  OpenADS, não a `ace32.dll` da SAP/Alaska.** Compile-a com:

  ```sh
  cmake --build build/release-x86 --config Release --target openads_ace
  ```

## Compilar e executar

```sh
# deste diretório; run.sh coloca o toolchain da Alaska no PATH,
# compila, linka, executa headless com timeout e despeja o log.
bash run.sh
```

`run.sh` também aceita um argumento opcional para trocar a DLL em
execuções de controle:

| comando            | efeito                                              |
|--------------------|-----------------------------------------------------|
| `bash run.sh`      | usa a `openace32.dll` presente                      |
| `bash run.sh openads` | copia `ace32_openads.dll` como `ace32.dll`       |
| `bash run.sh real` | copia a `ace32.dll` real da Alaska (controle)       |

(A troca mira `ace32.dll`; o teste ACE direto carrega `openace32.dll`
por nome, então o swap só importa para experimentos estilo ADSDBE.)

## O que faz

1. `DllPrepareCall` um template tipado por export (cdecl, retorno
   UINT32, `DLL_CALLMODE_COPY` para saídas `@`byref, `RESTOREFPU`).
2. `AdsConnect60` ao diretório de dados local (sem servidor).
3. `AdsCreateTable` uma tabela CDX `ID,N,8,0;NAME,C,20,0;`.
4. `AdsAppendRecord` ×5; define campos com `AdsSetString` (o `ID`
   numérico definido como texto — `set_field` converte, evitando passar
   um `double` C por `AdsSetDouble`). Verifica
   `AdsGetRecordCount` == 5.
5. `AdsCreateIndex61` uma tag CDX `NAME_TAG` sobre `NAME`.
6. `AdsSeek "Charlie"` (chave string, soft seek); lê `RecNo` com
   `AdsGetRecordNum` e `ID` com `AdsGetLong`; verifica `recno==3` e
   `ID==3`.
7. `AdsCloseTable` / `AdsDisconnect`.

## Pegadinhas (documentadas como comentários no teste)

- **`ADSHANDLE` é `uint64_t` — mesmo na DLL 32-bit.** Um handle passado
  *por valor* (entradas `hConn` / `hTable` / `hIndex`, e as saídas
  `@`byref) ocupa **dois** slots de pilha de 32 bits. Marshalá-lo como
  `UINT32` desloca cada argumento seguinte em 4 bytes — o sintoma foi
  `AdsCreateTable` falhando com `"no fields"` porque sua string de
  campos caía no slot errado. Todos os handles usam `DLL_TYPE_INT64`.
  Funções que recebem um handle apenas *por ponteiro* (ex. o
  `phConnect` de `AdsConnect60`) não são afetadas — por isso o connect
  funcionava e o create não.
- **`DllCall()` descarta silenciosamente argumentos após ~8.** O
  `AdsCreateTable` de 10 args perdia os últimos, então o teste usa o
  caminho tipado `DllPrepareCall` / `DllExecuteCall`.
- **As constantes ACE** (`ADS_LOCAL_SERVER`, `ADS_CDX`,
  `ADS_STRINGKEY`, …) são `#define`das localmente. O `ads.ch` do
  Xbase++ traz apenas a camada de comandos `AX_*`, não as declarações
  `Ads*` cruas nem suas constantes.

## Por que não ADSDBE

O Xbase++ tem um RDD ADS nativo — o motor **ADSDBE**
(`DbeLoad("ADSDBE")` + `USE` / `INDEX` / `DbSeek` padrão). Seria o
caminho de integração mais idiomático, mas é inutilizável headless
aqui:

- A `ace32.dll` real da Alaska trava em `DbCreate` (o diálogo
  interativo / de avaliação do servidor local do Advantage).
- A `ace32.dll` do OpenADS trava *dentro* de `DbeLoad` — `adsdbe.dll`
  chama um export de init durante a carga do motor que bloqueia (um
  `LoadLibrary` simples da DLL via `dllinfo.exe` funciona, então não é
  o `DllMain` do OpenADS).

Ambas travam em um diálogo GUI sem stdout, o que não sobrevive a uma
execução não assistida. O caminho ACE direto evita `adsdbe.dll` e o
servidor local por completo. Para um teste de integração no nível RDD,
veja o teste de fumaça X# (`tests/smoke/xsharp/`), que conduz o OpenADS
através do RDD `AXDBFCDX` do X#.
