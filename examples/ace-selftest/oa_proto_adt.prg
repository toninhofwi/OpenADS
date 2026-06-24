/*
 * oa_proto_adt.prg - prototipo OpenADS: ADT + ADI + ADM (memo)
 *
 * API ACE direta (AdsCreateTable ADS_ADT, ...) - sem RDD Harbour.
 * O BANCO E GERADO INTEIRO NESTE .prg (20 registros + indice ADI em ID).
 *
 * Uso: oa_proto_adt.exe [pasta] [local|remote] [uri]
 */

#define OA_ADS_ADT  3

REQUEST HB_GT_STD

PROCEDURE Main( cArg1, cArg2, cArg3 )

   LOCAL cDir     := "data_adt"
   LOCAL cMode    := "local"
   LOCAL cUri     := ""
   LOCAL cTable   := ""
   LOCAL nNewId   := 21
   LOCAL lRemote  := .F.
   LOCAL lOk      := .T.

   OaBanner( "ADT / ADI / ADM + memo", "ADS_ADT via ACE" )

   OaParseArgs( @cDir, @cMode, @cUri, cArg1, cArg2, cArg3 )
   lRemote := ( Lower( cMode ) == "remote" )
   cTable  := OaTablePath( cDir, "clientes.adt", lRemote )

   ? "Tabela: " + cTable
   ? "Modo:   " + cMode
   ?

   IF ! OaConnect( cDir, cMode, cUri )
      QUIT
   ENDIF

   IF ! OaGerarBanco( cTable, OA_ADS_ADT, "ADT/ADI", .T., lRemote, cDir )
      OaDisconnect()
      QUIT
   ENDIF

   ? ""
   ? ">>> Testes sobre banco gerado"
   ? ""
   ? ">>> 3) Walk ordem natural (primeiros 3)"
   OaGoFirst()
   OaShowRec( "1)" )
   IF OAA_RECORDCOUNT( OaTable() ) >= 2
      OAA_GOTO( OaTable(), 2 )
      OaShowRec( "2)" )
   ENDIF
   IF OAA_RECORDCOUNT( OaTable() ) >= 3
      OAA_GOTO( OaTable(), 3 )
      OaShowRec( "3)" )
   ENDIF

   ? ""
   ? ">>> 4) Alterar registro id=5"
   IF ! OaSeekId( 5 ) .AND. ! OaFindById( 5 )
      lOk := OaFail( "busca ID=5" )
   ELSE
      OaUpdateFields( { "VALOR", OAA_GETNUM( OaTable(), "VALOR" ) + 50, ;
                       "OBS",   "Alterado em id=5 (ADT)." } )
      OaShowRec( "apos alter" )
   ENDIF

   IF lOk
      ? ""
      ? ">>> 5) Busca por NOME"
      IF ! OaSeekNome( "CARLA DIAS" ) .AND. ! OaFindByNome( "CARLA DIAS" )
         lOk := OaFail( "busca NOME=CARLA DIAS" )
      ELSE
         OaShowRec( "busca nome" )
      ENDIF
   ENDIF

   IF lOk
      ? ""
      ? ">>> 6) Busca composta ID+NOME"
      IF ! OaFindByIdNome( 10, "Joao Alves" )
         lOk := OaFail( "busca id=10 nome=Joao Alves" )
      ELSE
         OaUpdateFields( { "CIDADE", "Aveiro", ;
                          "OBS",    "Alterado via busca ID+NOME (ADT)." } )
         OaShowRec( "composto" )
      ENDIF
   ENDIF

   IF lOk
      ? ""
      ? ">>> 7) Novo registro + busca + alteracao"
      IF OAA_APPEND( OaTable() )
         OAA_SETNUM( OaTable(), "ID", nNewId )
         OAA_SETSTR( OaTable(), "NOME", "Vera Campos" )
         OAA_SETSTR( OaTable(), "CIDADE", "Setubal" )
         OAA_SETNUM( OaTable(), "VALOR", 999.99 )
         OAA_SETSTR( OaTable(), "OBS", "Registro novo #21 (ADT)." )
         OAA_WRITEREC( OaTable() )
         OAA_FLUSH( OaTable() )
      ENDIF
      ? "Incluido id=" + LTrim( Str( nNewId ) )

      IF OaSeekId( nNewId ) .OR. OaFindById( nNewId )
         OaUpdateFields( { "VALOR", 1234.56, ;
                          "OBS",   "Novo registro alterado (ADT)." } )
         OaShowRec( "novo" )
      ELSE
         lOk := OaFail( "busca novo id=" + LTrim( Str( nNewId ) ) )
      ENDIF
   ENDIF

   OaDisconnect()

   IF lOk
      ? ""
      ? "PASS - banco ADT gerado e testado com sucesso."
      ErrorLevel( 0 )
   ENDIF

RETURN