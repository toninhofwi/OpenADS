/*
 * oa_proto_dbf.prg - prototipo OpenADS: DBF + FPT (memo) + CDX
 *
 * API ACE direta (AdsCreateTable, AdsSeek, ...) - sem RDD Harbour.
 * O BANCO E GERADO INTEIRO NESTE .prg (20 registros + indices CDX).
 *
 * Uso: oa_proto_dbf.exe [pasta] [local|remote] [uri]
 */

#define OA_ADS_CDX  2

REQUEST HB_GT_STD

PROCEDURE Main( cArg1, cArg2, cArg3 )

   LOCAL cDir     := "data_dbf"
   LOCAL cMode    := "local"
   LOCAL cUri     := ""
   LOCAL cTable   := ""
   LOCAL cKey     := ""
   LOCAL nNewId   := 21
   LOCAL lRemote  := .F.
   LOCAL lOk      := .T.

   OaBanner( "DBF / CDX / FPT + memo", "ADS_CDX via ACE" )

   OaParseArgs( @cDir, @cMode, @cUri, cArg1, cArg2, cArg3 )
   lRemote := ( Lower( cMode ) == "remote" )
   cTable  := OaTablePath( cDir, "clientes.dbf", lRemote )

   ? "Tabela: " + cTable
   ? "Modo:   " + cMode
   ?

   IF ! OaConnect( cDir, cMode, cUri )
      QUIT
   ENDIF

   IF ! OaGerarBanco( cTable, OA_ADS_CDX, "DBF/CDX", .F., lRemote, cDir )
      OaDisconnect()
      QUIT
   ENDIF

   ? ""
   ? ">>> Testes sobre banco gerado"
   ? ""
   ? ">>> 4) Alterar registro id=5 via seek ID_IDX"
   IF OaSeekId( 5 )
      OaUpdateFields( { "VALOR", OAA_GETNUM( OaTable(), "VALOR" ) + 50, ;
                       "OBS",   "Alterado em id=5 via seek ID." } )
      OaShowRec( "apos alter" )
   ELSE
      lOk := OaFail( "seek ID=5" )
   ENDIF

   IF lOk
      ? ""
      ? ">>> 5) Seek por NOME (case-insensitive)"
      IF OaSeekNome( "CARLA DIAS" )
         OaShowRec( "seek nome" )
      ELSE
         lOk := OaFail( "seek NOME=CARLA DIAS" )
      ENDIF
   ENDIF

   IF lOk
      ? ""
      ? ">>> 6) Seek chave composta ID+NOME"
      cKey := OaCompoundKey( 10, "Joao Alves" )
      IF OaSeekIdNome( 10, "Joao Alves" )
         OaUpdateFields( { "CIDADE", "Aveiro", ;
                          "OBS",    "Alterado via indice composto ID_NOME." } )
         OaShowRec( "composto" )
      ELSE
         lOk := OaFail( "seek composto " + cKey )
      ENDIF
   ENDIF

   IF lOk
      ? ""
      ? ">>> 7) Novo registro + seek + alteracao"
      IF OAA_APPEND( OaTable() )
         OAA_SETNUM( OaTable(), "ID", nNewId )
         OAA_SETSTR( OaTable(), "NOME", "Vera Campos" )
         OAA_SETSTR( OaTable(), "CIDADE", "Setubal" )
         OAA_SETNUM( OaTable(), "VALOR", 999.99 )
         OAA_SETSTR( OaTable(), "OBS", "Registro novo #21." )
         OAA_WRITEREC( OaTable() )
         OAA_FLUSH( OaTable() )
      ENDIF
      ? "Incluido id=" + LTrim( Str( nNewId ) )

      IF OaSeekId( nNewId )
         OaUpdateFields( { "VALOR", 1234.56, ;
                          "OBS",   "Novo registro alterado apos seek." } )
         OaShowRec( "novo" )
      ELSE
         lOk := OaFail( "seek novo id=" + LTrim( Str( nNewId ) ) )
      ENDIF
   ENDIF

   OaDisconnect()

   IF lOk
      ? ""
      ? "PASS - banco DBF gerado e testado com sucesso."
      ErrorLevel( 0 )
   ENDIF

RETURN