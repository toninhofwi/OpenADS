/*
 * oa_stress_cdx.prg - stress CDX: reutiliza o MESMO clientes.cdx
 *
 * Nao apaga o banco a cada loop. Append + update no mesmo arquivo e
 * imprime tamanho de clientes.cdx a cada passo (detecta inchamento/loop).
 *
 * Uso:
 *   oa_stress_cdx.exe [pasta] [loops] [batch] [modo]
 *
 * Modos:
 *   fresh    - apaga data, cria tabela+indices, depois stress
 *   reuse    - abre existente (cria se ausente) e continua append
 *   reindex  - como reuse, mas recria os 3 tags a CADA loop (teste bug)
 *
 * Exemplos:
 *   oa_stress_cdx.exe data_stress 100 50 reuse
 *   oa_stress_cdx.exe data_stress 20 100 reindex
 */

#define OA_ADS_CDX   2
#define OA_DIR_DEF   "data_stress"
#define OA_LOOP_DEF  100
#define OA_BATCH_DEF 50

REQUEST HB_GT_STD

//---------------------------------------------------------------------------//

STATIC s_nCdx0 := 0

//---------------------------------------------------------------------------//

FUNCTION OstFileSize( cFile )
   LOCAL nH, nSz := 0
   IF ! Empty( cFile ) .AND. File( cFile )
      nH := FOpen( cFile, 0 )
      IF nH > 0
         nSz := FSeek( nH, 0, 2 )
         FClose( nH )
      ENDIF
   ENDIF
RETURN nSz

//---------------------------------------------------------------------------//

FUNCTION OstFmtNum( nVal )
RETURN PadL( LTrim( Str( nVal, 12, 0 ) ), 12 )

//---------------------------------------------------------------------------//

FUNCTION OstFmtDelta( nDelta )
   LOCAL cSign := IIf( nDelta >= 0, "+", "" )
RETURN cSign + LTrim( Str( nDelta, 12, 0 ) )

//---------------------------------------------------------------------------//

FUNCTION OstReport( nLoop, nLoops, cDbf, cCdx, nRecs, nCdxPrev )

   LOCAL nDbf := OstFileSize( cDbf )
   LOCAL nCdx := OstFileSize( cCdx )
   LOCAL nDelta := nCdx - nCdxPrev

   ? "loop " + PadL( LTrim( Str( nLoop ) ), 4 ) + "/" + ;
     LTrim( Str( nLoops ) ) + ;
     "  recs=" + OstFmtNum( nRecs ) + ;
     "  dbf=" + OstFmtNum( nDbf ) + ;
     "  cdx=" + OstFmtNum( nCdx ) + ;
     "  d_cdx=" + OstFmtDelta( nDelta )

RETURN nCdx

//---------------------------------------------------------------------------//

FUNCTION OstParseArgs( cDir, nLoops, nBatch, cMode, ;
                       cA1, cA2, cA3, cA4 )

   hb_Default( @cDir,   OA_DIR_DEF )
   hb_Default( @nLoops, OA_LOOP_DEF )
   hb_Default( @nBatch, OA_BATCH_DEF )
   hb_Default( @cMode,  "reuse" )

   IF ! Empty( cA1 )
      cDir := AllTrim( cA1 )
   ENDIF
   IF ! Empty( cA2 )
      nLoops := Val( cA2 )
   ENDIF
   IF ! Empty( cA3 )
      nBatch := Val( cA3 )
   ENDIF
   IF ! Empty( cA4 )
      cMode := Lower( AllTrim( cA4 ) )
   ENDIF

   IF nLoops < 1
      nLoops := OA_LOOP_DEF
   ENDIF
   IF nBatch < 1
      nBatch := OA_BATCH_DEF
   ENDIF

   IF ! ( cMode $ "fresh,reuse,reindex" )
      cMode := "reuse"
   ENDIF

RETURN NIL

//---------------------------------------------------------------------------//

FUNCTION OstCreateBase( cTable, lRemote, cDataDir )

   LOCAL hConn := OaConn()
   LOCAL hTable := 0

   OaLimparBanco( cTable, { ".dbf", ".cdx", ".fpt" }, lRemote, cDataDir )

   hTable := OAA_CREATETABLE( hConn, cTable, OaFieldDef(), OA_ADS_CDX )
   IF hTable == 0
      ? "ERRO OstCreateBase: " + OAA_LASTERR()
      RETURN .F.
   ENDIF

   IF ! OaCreateIndexes( hTable, .F., cTable )
      OAA_TABLECLOSE( hTable )
      RETURN .F.
   ENDIF

   OAA_TABLECLOSE( hTable )
RETURN .T.

//---------------------------------------------------------------------------//

FUNCTION OstOpenTable( cTable )
   LOCAL hConn := OaConn()
   LOCAL hTable := OAA_OPENTABLE( hConn, cTable, OA_ADS_CDX )
   IF hTable == 0
      ? "ERRO OstOpenTable: " + OAA_LASTERR()
   ENDIF
RETURN hTable

//---------------------------------------------------------------------------//

FUNCTION OstAppendBatch( hTable, nBatch, nIdStart )
   LOCAL n, nId, nVal, cNome, lOk := .T.

   FOR n := 1 TO nBatch
      nId := nIdStart + n - 1
      cNome := "Stress " + LTrim( Str( nId ) )
      nVal  := Round( nId * 3.17, 2 )
      IF ! OAA_APPEND( hTable )
         ? "ERRO OAA_APPEND id=" + LTrim( Str( nId ) ) + ": " + OAA_LASTERR()
         lOk := .F.
         EXIT
      ENDIF
      OAA_SETNUM( hTable, "ID", nId )
      OAA_SETSTR( hTable, "NOME", cNome )
      OAA_SETSTR( hTable, "CIDADE", "StressCity" )
      OAA_SETNUM( hTable, "VALOR", nVal )
      OAA_SETSTR( hTable, "OBS", "stress loop id " + LTrim( Str( nId ) ) )
      IF ! OAA_WRITEREC( hTable )
         ? "ERRO OAA_WRITEREC id=" + LTrim( Str( nId ) ) + ": " + OAA_LASTERR()
         lOk := .F.
         EXIT
      ENDIF
   NEXT

RETURN lOk

//---------------------------------------------------------------------------//

FUNCTION OstUpdateSample( hTable, nMaxRec )
   LOCAL nRec, nPick, nLast := Min( nMaxRec, OAA_RECORDCOUNT( hTable ) )

   IF nLast < 1
      RETURN .T.
   ENDIF

   nPick := ( nMaxRec % nLast ) + 1
   IF ! OAA_GOTO( hTable, nPick )
      RETURN .F.
   ENDIF

   OAA_SETNUM( hTable, "VALOR", OAA_GETNUM( hTable, "VALOR" ) + 1 )
   OAA_WRITEREC( hTable )

RETURN .T.

//---------------------------------------------------------------------------//

FUNCTION OstRecreateIndexes( hTable, cTable )
   LOCAL cBag := OaIndexBag( cTable, .F. )
   LOCAL lOk := .T.

   IF ! OAA_CREATEINDEX( hTable, "ID_IDX", "ID", cBag )
      lOk := .F.
   ENDIF
   IF lOk .AND. ! OAA_CREATEINDEX( hTable, "NOME_IDX", "UPPER(NOME)", cBag )
      lOk := .F.
   ENDIF
   IF lOk .AND. ! OAA_CREATEINDEX( hTable, "ID_NOME", ;
         "STR(ID,8,0)+UPPER(NOME)", cBag )
      lOk := .F.
   ENDIF
   IF lOk
      OAA_OPENINDEX( hTable, cBag )
   ENDIF

RETURN lOk

//---------------------------------------------------------------------------//

PROCEDURE Main( cA1, cA2, cA3, cA4 )

   LOCAL cDir    := OA_DIR_DEF
   LOCAL nLoops  := OA_LOOP_DEF
   LOCAL nBatch  := OA_BATCH_DEF
   LOCAL cMode   := "reuse"
   LOCAL cTable  := ""
   LOCAL cDbf    := ""
   LOCAL cCdx    := ""
   LOCAL hTable  := 0
   LOCAL nLoop, nRecs, nIdNext
   LOCAL nCdxPrev, nCdxLast, nCdxGrow
   LOCAL nWarnDelta := 32768   && reuse normal ~1-3 KB/loop; reindex bug ~40+ KB/loop
   LOCAL nAlerts := 0
   LOCAL lOk := .T.
   LOCAL lReindex := .F.

   ? Replicate( "=", 70 )
   ? "  oa_stress_cdx - stress CDX (mesmo arquivo, monitora tamanho)"
   ? "  ACE: " + OAA_VERSION()
   ? Replicate( "=", 70 )

   OstParseArgs( @cDir, @nLoops, @nBatch, @cMode, cA1, cA2, cA3, cA4 )
   lReindex := ( cMode == "reindex" )

   cTable := OaTablePath( cDir, "clientes.dbf", .F. )
   cDbf   := cTable
   cCdx   := hb_FNameExtSet( cTable, ".cdx" )

   ? "Pasta:  " + hb_FNameMerge( hb_cwd(), cDir )
   ? "Loops:  " + LTrim( Str( nLoops ) ) + "  batch=" + LTrim( Str( nBatch ) )
   ? "Modo:   " + cMode + IIf( lReindex, " (recria indices cada loop)", "" )
   ? "CDX:    " + cCdx
   ?

   IF ! OaConnect( cDir, "local", "" )
      QUIT
   ENDIF

   IF cMode == "fresh" .OR. ! File( cDbf )
      ? ">>> Criar base (tabela + 3 tags CDX, sem seed)"
      IF ! OstCreateBase( cTable, .F., cDir )
         OaDisconnect()
         QUIT
      ENDIF
   ENDIF

   hTable := OstOpenTable( cTable )
   IF hTable == 0
      OaDisconnect()
      QUIT
   ENDIF

   nRecs    := OAA_RECORDCOUNT( hTable )
   nIdNext  := nRecs + 1
   s_nCdx0  := OstFileSize( cCdx )
   nCdxPrev := s_nCdx0
   nCdxLast := s_nCdx0

   ? ">>> Inicio: recs=" + LTrim( Str( nRecs ) ) + ;
     "  cdx=" + LTrim( Str( s_nCdx0 ) ) + " bytes"
   ? Replicate( "-", 70 )

   FOR nLoop := 1 TO nLoops
      IF lReindex
         IF ! OstRecreateIndexes( hTable, cTable )
            ? "AVISO: OstRecreateIndexes loop " + LTrim( Str( nLoop ) )
         ENDIF
      ENDIF

      IF ! OstAppendBatch( hTable, nBatch, nIdNext )
         lOk := .F.
         EXIT
      ENDIF
      nIdNext += nBatch

      OstUpdateSample( hTable, nLoop * nBatch )
      OAA_FLUSH( hTable )

      nRecs := OAA_RECORDCOUNT( hTable )
      nCdxLast := OstReport( nLoop, nLoops, cDbf, cCdx, nRecs, nCdxPrev )

      IF ( nCdxLast - nCdxPrev ) > nWarnDelta
         ? "  *** ALERTA: CDX cresceu > 32KB neste loop"
         nAlerts++
      ENDIF

      nCdxPrev := nCdxLast
   NEXT

   nCdxGrow := nCdxLast - s_nCdx0

   ? Replicate( "-", 70 )
   ? ">>> Fim: recs=" + LTrim( Str( nRecs ) ) + ;
     "  cdx=" + LTrim( Str( nCdxLast ) ) + ;
     "  crescimento total=" + OstFmtDelta( nCdxGrow ) + " bytes"

   OAA_TABLECLOSE( hTable )
   OaDisconnect()

   IF lOk .AND. nAlerts == 0
      ? ""
      ? "PASS - CDX cresceu de forma moderada (sem alertas)."
      ErrorLevel( 0 )
   ELSEIF lOk .AND. nAlerts > 0
      ? ""
      ? "*** FALHA - CDX inchou: " + LTrim( Str( nAlerts ) ) + ;
        " loop(s) com crescimento > 32KB."
      ? "    Modo reindex costuma reproduzir o bug (recriar indice sem apagar bag)."
      ErrorLevel( 1 )
   ELSE
      ? ""
      ? "*** FALHA no stress CDX (erro de I/O)"
      ErrorLevel( 1 )
   ENDIF

RETURN