/*
 * oa_proto_common.prg - rotinas compartilhadas (OpenADS ACE direto, sem RDD)
 */

#define SEED_ROWS    20
#define OA_ADS_CDX   2
#define OA_ADS_ADT   3
#define OA_REMOTE_URI_DEF  "tcp://127.0.0.1:6262/"

STATIC s_hConn  := 0
STATIC s_hTable := 0

//---------------------------------------------------------------------------//

FUNCTION OaConn()
RETURN s_hConn

//---------------------------------------------------------------------------//

FUNCTION OaTable()
RETURN s_hTable

//---------------------------------------------------------------------------//

FUNCTION OaBanner( cTitulo, cEngine )
   ? Replicate( "=", 60 )
   ? "  " + cTitulo
   ? "  OpenADS ACE API - prototipo standalone (sem RDD Harbour)"
   ? "  Engine: " + cEngine
   ? "  ACE:    " + OAA_VERSION()
   ? Replicate( "=", 60 )
RETURN NIL

//---------------------------------------------------------------------------//

FUNCTION OaParseArgs( cDataDir, cMode, cUri, cArg1, cArg2, cArg3 )

   LOCAL cEnv := AllTrim( GetEnv( "OPENADS_CONNECT_URI" ) )

   hb_Default( @cDataDir, "data" )
   hb_Default( @cMode,    "local" )
   hb_Default( @cUri,     "" )

   IF ! Empty( cArg1 )
      cDataDir := AllTrim( cArg1 )
   ENDIF
   IF ! Empty( cArg2 )
      cMode := Lower( AllTrim( cArg2 ) )
   ENDIF
   IF ! Empty( cArg3 )
      cUri := AllTrim( cArg3 )
   ENDIF

   IF Empty( cUri ) .AND. ! Empty( cEnv )
      cUri := cEnv
   ENDIF

   IF cMode == "remote" .AND. Empty( cUri )
      cUri := OA_REMOTE_URI_DEF
   ENDIF

RETURN NIL

//---------------------------------------------------------------------------//

FUNCTION OaConnectUri( cDataDir, cMode, cUri )
   LOCAL lRemote := ( Lower( AllTrim( cMode ) ) == "remote" )

   IF lRemote
      RETURN cUri
   ENDIF

RETURN hb_FNameMerge( hb_cwd(), cDataDir )

//---------------------------------------------------------------------------//

FUNCTION OaConnect( cDataDir, cMode, cUri )

   LOCAL cTarget := OaConnectUri( cDataDir, cMode, cUri )
   LOCAL lRemote := ( Lower( AllTrim( cMode ) ) == "remote" )

   hb_DirCreate( cDataDir )

   s_hConn := OAA_CONNECT( cTarget )
   IF s_hConn == 0
      ? "ERRO: OAA_CONNECT falhou em " + cTarget
      ? "       " + OAA_LASTERR()
      RETURN .F.
   ENDIF

   IF lRemote
      ? "Conexao: REMOTA  uri=" + cTarget
   ELSE
      ? "Conexao: LOCAL   dir=" + cTarget
   ENDIF

RETURN .T.

//---------------------------------------------------------------------------//

FUNCTION OaDisconnect()
   IF s_hTable > 0
      OAA_TABLECLOSE( s_hTable )
      s_hTable := 0
   ENDIF
   IF s_hConn > 0
      OAA_DISCONNECT( s_hConn )
      s_hConn := 0
   ENDIF
RETURN NIL

//---------------------------------------------------------------------------//

FUNCTION OaTablePath( cDir, cBaseFile, lRemote )
   IF lRemote
      RETURN hb_FNameMerge( cDir, cBaseFile )
   ENDIF
RETURN hb_FNameMerge( hb_FNameMerge( hb_cwd(), cDir ), cBaseFile )

//---------------------------------------------------------------------------//

FUNCTION OaFieldDef()
RETURN "ID,Numeric,8,0;NOME,Character,30;CIDADE,Character,20;" + ;
       "VALOR,Numeric,12,2;OBS,Memo,10"

//---------------------------------------------------------------------------//

FUNCTION OaIndexBag( cTable, lAdt )
   IF lAdt
      RETURN hb_FNameExtSet( hb_FNameName( cTable ), ".adi" )
   ENDIF
RETURN hb_FNameExtSet( hb_FNameName( cTable ), ".cdx" )

//---------------------------------------------------------------------------//

FUNCTION OaLimparBanco( cTable, aExts, lRemote, cDataDir )
   LOCAL cExt, i, cFile, cOrfao

   FOR i := 1 TO Len( aExts )
      cExt := aExts[ i ]
      IF ! Empty( cExt )
         cFile := hb_FNameExtSet( cTable, cExt )
         IF File( cFile )
            FErase( cFile )
         ENDIF
      ENDIF
   NEXT

   IF ! lRemote .AND. ! Empty( cDataDir )
      cOrfao := OaTablePath( cDataDir, ".cdx", .F. )
      IF File( cOrfao )
         FErase( cOrfao )
      ENDIF
   ENDIF

RETURN NIL

//---------------------------------------------------------------------------//

FUNCTION OaCreateIndexes( hTable, lAdt, cTable )
   LOCAL cBag := OaIndexBag( cTable, lAdt )
   LOCAL lOk  := .T.

   IF ! OAA_CREATEINDEX( hTable, "ID_IDX", "ID", cBag )
      lOk := .F.
      ? "ERRO indice ID_IDX: " + OAA_LASTERR()
   ENDIF

   IF lOk .AND. ! OAA_CREATEINDEX( hTable, "NOME_IDX", "UPPER(NOME)", cBag )
      IF lAdt
         ? "AVISO: indice NOME_IDX falhou (ADT) - busca por scan"
      ELSE
         lOk := .F.
         ? "ERRO indice NOME_IDX: " + OAA_LASTERR()
      ENDIF
   ENDIF

   IF lOk .AND. ! lAdt
      IF ! OAA_CREATEINDEX( hTable, "ID_NOME", "STR(ID,8,0)+UPPER(NOME)", cBag )
         lOk := .F.
         ? "ERRO indice ID_NOME: " + OAA_LASTERR()
      ENDIF
   ENDIF

   IF lOk .AND. ! lAdt
      IF OAA_OPENINDEX( hTable, cBag ) == 0
         ? "AVISO: OAA_OPENINDEX " + cBag + " falhou"
      ENDIF
   ENDIF

RETURN lOk

//---------------------------------------------------------------------------//

/* Gera banco do ZERO via AdsCreateTable + indices + 20 registros. */
FUNCTION OaGerarBanco( cTable, nTableType, cMemoTag, lAdt, lRemote, cDataDir )

   LOCAL aExts := IIf( lAdt, { ".adt", ".adi", ".adm", ".cdx" }, ;
                              { ".dbf", ".cdx", ".fpt" } )
   LOCAL nRows := 0

   IF s_hTable > 0
      OAA_TABLECLOSE( s_hTable )
      s_hTable := 0
   ENDIF

   ? ""
   ? ">>> GERAR BANCO DO ZERO (AdsCreateTable + indices + seed)"
   ? "    Arquivo: " + cTable
   OaLimparBanco( cTable, aExts, lRemote, cDataDir )

   s_hTable := OAA_CREATETABLE( s_hConn, cTable, OaFieldDef(), nTableType )
   IF s_hTable == 0
      OaFail( "OAA_CREATETABLE falhou: " + cTable + " - " + OAA_LASTERR() )
      RETURN .F.
   ENDIF

   IF ! OaCreateIndexes( s_hTable, lAdt, cTable )
      OAA_TABLECLOSE( s_hTable )
      s_hTable := 0
      RETURN .F.
   ENDIF

   nRows := OaPopulate( SEED_ROWS, cMemoTag )
   OAA_FLUSH( s_hTable )
   ? "    Registros gravados: " + LTrim( Str( nRows ) )
   ? "    Banco pronto."

RETURN ( nRows == SEED_ROWS )

//---------------------------------------------------------------------------//

FUNCTION OaSeedNames()
   LOCAL aN := { ;
      "Ana Silva",    "Bruno Costa",   "Carla Dias",   "Diego Faria",  "Elena Moura", ;
      "Fabio Lima",   "Gisele Rocha",  "Henrique Paz", "Iris Nunes",   "Joao Alves", ;
      "Karen Souza",  "Lucas Prado",   "Marina Teix",  "Nelson Vega",  "Olivia Cruz", ;
      "Paulo Mendes", "Rita Campos",   "Sergio Neto",  "Tania Borges", "Ulisses Reis" }
RETURN aN

//---------------------------------------------------------------------------//

FUNCTION OaPopulate( nRows, cMemoTag )

   LOCAL aN := OaSeedNames()
   LOCAL n, cCid, nVal, cMemo

   FOR n := 1 TO nRows
      cCid  := { "Lisboa", "Porto", "Braga", "Coimbra", "Faro" }[ 1 + ( ( n - 1 ) % 5 ) ]
      nVal  := Round( ( n * 13.7 ) + 100, 2 )
      cMemo := "Memo #" + LTrim( Str( n ) ) + " - " + cMemoTag + ;
               " - registro sintetico para teste OpenADS."
      IF ! OAA_APPEND( s_hTable )
         OaFail( "OAA_APPEND falhou rec " + LTrim( Str( n ) ) + ": " + OAA_LASTERR() )
         RETURN n - 1
      ENDIF
      OAA_SETNUM( s_hTable, "ID", n )
      OAA_SETSTR( s_hTable, "NOME", aN[ n ] )
      OAA_SETSTR( s_hTable, "CIDADE", cCid )
      OAA_SETNUM( s_hTable, "VALOR", nVal )
      OAA_SETSTR( s_hTable, "OBS", cMemo )
      IF ! OAA_WRITEREC( s_hTable )
         OaFail( "OAA_WRITEREC falhou rec " + LTrim( Str( n ) ) + ": " + OAA_LASTERR() )
         RETURN n - 1
      ENDIF
   NEXT

RETURN nRows

//---------------------------------------------------------------------------//

FUNCTION OaShowRec( cLabel )
   LOCAL cObs := Left( AllTrim( OAA_GETSTR( s_hTable, "OBS" ) ), 40 )

   ? "  " + cLabel + ;
     " rec=" + LTrim( Str( OAA_RECNO( s_hTable ) ) ) + ;
     " id=" + LTrim( Str( OAA_GETNUM( s_hTable, "ID" ) ) ) + ;
     " nome=[" + AllTrim( OAA_GETSTR( s_hTable, "NOME" ) ) + "]" + ;
     " valor=" + LTrim( Str( OAA_GETNUM( s_hTable, "VALOR" ), 10, 2 ) ) + ;
     IIf( Empty( cObs ), "", " obs=[" + cObs + "...]" )
RETURN NIL

//---------------------------------------------------------------------------//

FUNCTION OaCompoundKey( nId, cNome )
RETURN PadL( LTrim( Str( nId, 8 ) ), 8 ) + Upper( AllTrim( cNome ) )

//---------------------------------------------------------------------------//

FUNCTION OaIndexHandle( cTag )
RETURN OAA_GETINDEX( s_hTable, cTag )

//---------------------------------------------------------------------------//

FUNCTION OaSeekId( nId )
   LOCAL hIdx := OaIndexHandle( "ID_IDX" )
   IF hIdx > 0 .AND. OAA_SEEKNUM( hIdx, nId )
      RETURN .T.
   ENDIF
RETURN OaFindById( nId )

//---------------------------------------------------------------------------//

FUNCTION OaSeekNome( cNome )
   LOCAL hIdx := OaIndexHandle( "NOME_IDX" )
   IF hIdx > 0 .AND. OAA_SEEKSTR( hIdx, Upper( AllTrim( cNome ) ) )
      RETURN .T.
   ENDIF
RETURN OaFindByNome( cNome )

//---------------------------------------------------------------------------//

FUNCTION OaSeekIdNome( nId, cNome )
   LOCAL hIdx := OaIndexHandle( "ID_NOME" )
   LOCAL cKey := OaCompoundKey( nId, cNome )
   IF hIdx > 0 .AND. OAA_SEEKSTR( hIdx, cKey )
      RETURN .T.
   ENDIF
RETURN OaFindByIdNome( nId, cNome )

//---------------------------------------------------------------------------//

FUNCTION OaWalkIndex( cTag, nMax )

   LOCAL n := 0
   LOCAL nRec, nLast := OAA_RECORDCOUNT( s_hTable )

   hb_Default( @nMax, 5 )

   ? ""
   ? "--- Walk indice " + cTag + " (primeiros " + LTrim( Str( nMax ) ) + ") ---"

   FOR nRec := 1 TO nLast
      IF n >= nMax
         EXIT
      ENDIF
      OAA_GOTO( s_hTable, nRec )
      n++
      OaShowRec( LTrim( Str( n ) ) + ")" )
   NEXT

RETURN n

//---------------------------------------------------------------------------//

FUNCTION OaGoFirst()
   IF OAA_RECORDCOUNT( s_hTable ) > 0
      OAA_GOTO( s_hTable, 1 )
   ENDIF
RETURN NIL

//---------------------------------------------------------------------------//

FUNCTION OaFindById( nId )
   LOCAL n, nLast := OAA_RECORDCOUNT( s_hTable )

   FOR n := 1 TO nLast
      OAA_GOTO( s_hTable, n )
      IF OAA_GETNUM( s_hTable, "ID" ) == nId
         RETURN .T.
      ENDIF
   NEXT

RETURN .F.

//---------------------------------------------------------------------------//

FUNCTION OaFindByNome( cNome )
   LOCAL n, nLast := OAA_RECORDCOUNT( s_hTable )

   cNome := Upper( AllTrim( cNome ) )
   FOR n := 1 TO nLast
      OAA_GOTO( s_hTable, n )
      IF Upper( AllTrim( OAA_GETSTR( s_hTable, "NOME" ) ) ) == cNome
         RETURN .T.
      ENDIF
   NEXT

RETURN .F.

//---------------------------------------------------------------------------//

FUNCTION OaFindByIdNome( nId, cNome )
   LOCAL n, nLast := OAA_RECORDCOUNT( s_hTable )

   cNome := Upper( AllTrim( cNome ) )
   FOR n := 1 TO nLast
      OAA_GOTO( s_hTable, n )
      IF OAA_GETNUM( s_hTable, "ID" ) == nId .AND. ;
         Upper( AllTrim( OAA_GETSTR( s_hTable, "NOME" ) ) ) == cNome
         RETURN .T.
      ENDIF
   NEXT

RETURN .F.

//---------------------------------------------------------------------------//

FUNCTION OaUpdateFields( aPairs )
   LOCAL i, n := Len( aPairs )

   FOR i := 1 TO n STEP 2
      IF ValType( aPairs[ i + 1 ] ) == "N"
         OAA_SETNUM( s_hTable, aPairs[ i ], aPairs[ i + 1 ] )
      ELSE
         OAA_SETSTR( s_hTable, aPairs[ i ], aPairs[ i + 1 ] )
      ENDIF
   NEXT
   OAA_WRITEREC( s_hTable )
RETURN .T.

//---------------------------------------------------------------------------//

FUNCTION OaFail( cMsg )
   LOCAL cLog := "oa_proto.log"
   ? ""
   ? "*** FALHA: " + cMsg
   MemoWrit( cLog, DToC( Date() ) + " " + Time() + " FAIL " + cMsg + hb_eol(), .T. )
   ErrorLevel( 1 )
RETURN .F.