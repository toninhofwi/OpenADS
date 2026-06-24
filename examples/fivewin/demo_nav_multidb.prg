/*
 * demo_nav_multidb.prg -- console NAV smoke via TOpenAdsConnection + AdsOpenTable.
 *
 * Same pseudo-ADO connection class as the SqlQuery demo, but reads rows with
 * USE/SKIP/GETFIELD (navigational backends: postgresql:// mariadb:// odbc://).
 *
 * Fixture: table clientes (id PK, nome, saldo) with 3 rows Ana/Bob/Cid.
 * Seed first (see tools/scripts/seed_nav_clientes.*).
 *
 * Env (optional -- omit to skip a backend):
 *   OPENADS_TEST_SQLITE_URI
 *   OPENADS_TEST_PG_URI
 *   OPENADS_TEST_MARIADB_URI
 *   OPENADS_TEST_ODBC_CONNSTR   (Driver={...};...  -- odbc:// added here)
 *
 * Build/run: demo_nav_run.bat [sqlite|odbc|pg|maria|all]
 */

#define ADS_LOCAL_SERVER 1

PROCEDURE Main()

   LOCAL aCfg := {}
   LOCAL cfg, oCn, lOk, cLine
   LOCAL nPass := 0, nSkip := 0, nFail := 0
   LOCAL i
   LOCAL cMode := Upper( AllTrim( GetEnv( "OPENADS_NAV_MODE" ) ) )
   LOCAL cOdbc := AllTrim( GetEnv( "OPENADS_TEST_ODBC_CONNSTR" ) )

   IF ! Empty( cOdbc ) .AND. Upper( Left( cOdbc, 6 ) ) != "ODBC:/"
      cOdbc := "odbc://" + cOdbc
   ENDIF

   Nav_AddIf( @aCfg, "sqlite", GetEnv( "OPENADS_TEST_SQLITE_URI" ), ;
      "clientes", cMode, { "SQLITE", "ALL", "" } )
   Nav_AddIf( @aCfg, "postgresql", GetEnv( "OPENADS_TEST_PG_URI" ), ;
      "clientes", cMode, { "PG", "POSTGRESQL", "ALL", "" } )
   Nav_AddIf( @aCfg, "mariadb", GetEnv( "OPENADS_TEST_MARIADB_URI" ), ;
      "clientes", cMode, { "MARIA", "MARIADB", "MYSQL", "ALL", "" } )
   Nav_AddIf( @aCfg, "odbc", cOdbc, "CLIENTES", cMode, ;
      { "ODBC", "FIREBIRD", "FB", "ALL", "" } )

   ? "=== OpenADS NAV smoke (TOpenAdsConnection + AdsOpenTable) ==="
   ? "ACE:", OADS_AdsVersion()
   ? "mode:", iif( Empty( cMode ), "ALL", cMode )
   ?

   IF Len( aCfg ) == 0
      ? "No backends configured. Set OPENADS_TEST_SQLITE_URI /"
      ? "OPENADS_TEST_PG_URI / OPENADS_TEST_MARIADB_URI / OPENADS_TEST_ODBC_CONNSTR"
      ? "and OPENADS_NAV_MODE if narrowing (sqlite|pg|maria|odbc|all)."
      ErrorLevel( 2 )
      RETURN
   ENDIF

   IF Val( GetEnv( "OPENADS_NAV_BENCH_ITERS" ) ) > 0
      Nav_BenchRun( aCfg, Val( GetEnv( "OPENADS_NAV_BENCH_ITERS" ) ), ;
         Val( GetEnv( "OPENADS_NAV_BENCH_WARMUP" ) ) )
      RETURN
   ENDIF

   FOR i := 1 TO Len( aCfg )
      cfg := aCfg[ i ]
      ? "--- [" + cfg[ 1 ] + "]"
      ? "    " + cfg[ 2 ]

      oCn := OpenAds_AdoTryBridge( "OPENADS," + cfg[ 2 ], .T. )
      IF oCn == NIL .OR. ! HB_ISOBJECT( oCn ) .OR. oCn:State() != 1
         ? "    SKIP: connect failed (seed DB / start server / copy DLL?)"
         nSkip++
         ?
         LOOP
      ENDIF

      lOk := Nav_ReadClientes( oCn:hConn, cfg[ 3 ], @cLine )
      oCn:Close()

      IF lOk
         ? "    PASS"
         nPass++
      ELSE
         ? "    FAIL: " + iif( Empty( cLine ), "navigation mismatch (run seed?)", cLine )
         nFail++
      ENDIF
      ?
   NEXT

   ? "Summary: pass=" + LTrim( Str( nPass ) ) + " skip=" + ;
     LTrim( Str( nSkip ) ) + " fail=" + LTrim( Str( nFail ) )

   IF nFail > 0
      ErrorLevel( 1 )
      RETURN
   ENDIF
   IF nPass == 0
      ErrorLevel( 2 )
      RETURN
   ENDIF

RETURN

STATIC FUNCTION Nav_BenchRun( aCfg, nIters, nWarmup )

   LOCAL cfg := aCfg[ 1 ]
   LOCAL nW := iif( nWarmup < 1, 1, nWarmup )
   LOCAL i, oCn, lOk, cLine
   LOCAL t0, t1, tC, tN, tT
   LOCAL aTotal := {}, aConn := {}, aNav := {}
   LOCAL nPass := 0, nFail := 0
   LOCAL nAvgT, nAvgC, nAvgN, nMinT, nMaxT, nP50, nP95

   IF nIters < 1
      nIters := 30
   ENDIF

   ? "=== OpenADS NAV bench (timed connect + AdsOpenTable nav) ==="
   ? "ACE:", OADS_AdsVersion()
   ? "backend:", cfg[ 1 ], " table:", cfg[ 3 ]
   ? "iters:", LTrim( Str( nIters ) ), " warmup:", LTrim( Str( nW ) )
   ? "BENCH_START,iters=" + LTrim( Str( nIters ) ) + ",warmup=" + ;
     LTrim( Str( nW ) ) + ",mode=" + cfg[ 1 ]
   ?

   FOR i := 1 TO nIters + nW
      t0 := Seconds()
      oCn := OpenAds_AdoTryBridge( "OPENADS," + cfg[ 2 ], .F. )
      t1 := Seconds()
      tC := ( t1 - t0 ) * 1000

      IF oCn == NIL .OR. ! HB_ISOBJECT( oCn ) .OR. oCn:State() != 1
         oCn := NIL
         tN := 0
         tT := tC
         lOk := .F.
         cLine := "connect failed"
      ELSE
         lOk := Nav_ReadClientes( oCn:hConn, cfg[ 3 ], @cLine )
         tN := ( Seconds() - t1 ) * 1000
         oCn:Close()
         tT := ( Seconds() - t0 ) * 1000
      ENDIF

      IF i > nW
         IF lOk
            nPass++
            AAdd( aTotal, tT )
            AAdd( aConn, tC )
            AAdd( aNav, tN )
         ELSE
            nFail++
         ENDIF
         ? "BENCH_ROW,iter=" + LTrim( Str( i - nW ) ) + ",connect_ms=" + ;
           Nav_FmtMs( tC ) + ",nav_ms=" + Nav_FmtMs( tN ) + ;
           ",total_ms=" + Nav_FmtMs( tT ) + ",ok=" + iif( lOk, "1", "0" ) + ;
           iif( lOk, "", ",err=" + cLine )
      ENDIF
   NEXT

   IF nPass == 0
      ? "BENCH_SUMMARY,iters=0,pass=0,fail=" + LTrim( Str( nFail ) )
      ErrorLevel( 1 )
      RETURN
   ENDIF

   ASort( aTotal )
   nAvgT := Nav_Avg( aTotal )
   nAvgC := Nav_Avg( aConn )
   nAvgN := Nav_Avg( aNav )
   nMinT := aTotal[ 1 ]
   nMaxT := aTotal[ Len( aTotal ) ]
   nP50  := Nav_Pctl( aTotal, 50 )
   nP95  := Nav_Pctl( aTotal, 95 )

   ? 
   ? "BENCH_SUMMARY,iters=" + LTrim( Str( nPass ) ) + ",pass=" + ;
     LTrim( Str( nPass ) ) + ",fail=" + LTrim( Str( nFail ) ) + ;
     ",avg_total_ms=" + Nav_FmtMs( nAvgT ) + ",min_total_ms=" + ;
     Nav_FmtMs( nMinT ) + ",max_total_ms=" + Nav_FmtMs( nMaxT ) + ;
     ",p50_total_ms=" + Nav_FmtMs( nP50 ) + ",p95_total_ms=" + ;
     Nav_FmtMs( nP95 ) + ",avg_connect_ms=" + Nav_FmtMs( nAvgC ) + ;
     ",avg_nav_ms=" + Nav_FmtMs( nAvgN )

   IF nFail > 0
      ErrorLevel( 1 )
   ENDIF

RETURN

STATIC FUNCTION Nav_FmtMs( nMs )

RETURN LTrim( Str( nMs, 12, 2 ) )

STATIC FUNCTION Nav_Avg( aVals )

   LOCAL s := 0, i

   FOR i := 1 TO Len( aVals )
      s += aVals[ i ]
   NEXT

RETURN iif( Len( aVals ) > 0, s / Len( aVals ), 0 )

STATIC FUNCTION Nav_Pctl( aSorted, nPct )

   LOCAL n := Len( aSorted )
   LOCAL idx

   IF n == 0
      RETURN 0
   ENDIF
   idx := Int( ( nPct / 100 ) * n + 0.999999 )
   IF idx < 1
      idx := 1
   ENDIF
   IF idx > n
      idx := n
   ENDIF

RETURN aSorted[ idx ]

STATIC FUNCTION Nav_AddIf( aCfg, cLabel, cUri, cTable, cMode, aModes )

   LOCAL cM := Upper( AllTrim( cMode ) )
   LOCAL lRun := Empty( cM )
   LOCAL m

   IF Empty( cUri )
      RETURN
   ENDIF

   IF ! lRun
      FOR EACH m IN aModes
         IF cM == Upper( AllTrim( m ) )
            lRun := .T.
            EXIT
         ENDIF
      NEXT
   ENDIF

   IF lRun
      AAdd( aCfg, { cLabel, cUri, cTable } )
   ENDIF

RETURN

STATIC FUNCTION Nav_ReadClientes( hConn, cTable, cWhy )

   LOCAL hTable := 0
   LOCAL nCount := 0
   LOCAL nFields := 0
   LOCAL cNome := ""
   LOCAL cSaldo := ""
   LOCAL nEof := 0

   cWhy := ""

   IF AdsOpenTable( hConn, cTable, cTable, 0, 0, 0, 0, 3, @hTable ) != 0
      cWhy := Nav_LastAdsErr( "AdsOpenTable" )
      RETURN .F.
   ENDIF

   IF AdsGetNumFields( hTable, @nFields ) != 0 .OR. nFields != 3
      cWhy := "expected 3 fields, got " + LTrim( Str( nFields ) )
      AdsCloseTable( hTable )
      RETURN .F.
   ENDIF

   nCount := OADS_NavRecCount( hTable )
   IF nCount != 3
      cWhy := "expected 3 rows, got " + LTrim( Str( nCount ) )
      AdsCloseTable( hTable )
      RETURN .F.
   ENDIF

   AdsGotoTop( hTable )
   cNome := Nav_Field( hTable, "nome" )
   IF Left( AllTrim( cNome ), 3 ) != "Ana"
      cWhy := "row1 nome=[" + AllTrim( cNome ) + "]"
      AdsCloseTable( hTable )
      RETURN .F.
   ENDIF

   AdsSkip( hTable, 1 )
   cSaldo := Nav_Field( hTable, "saldo" )
   IF ! Nav_IsNullField( cSaldo )
      cWhy := "row2 saldo=[" + AllTrim( cSaldo ) + "] expected NULL"
      AdsCloseTable( hTable )
      RETURN .F.
   ENDIF

   AdsSkip( hTable, 1 )
   cNome := Nav_Field( hTable, "nome" )
   IF Left( AllTrim( cNome ), 3 ) != "Cid"
      cWhy := "row3 nome=[" + AllTrim( cNome ) + "]"
      AdsCloseTable( hTable )
      RETURN .F.
   ENDIF

   AdsSkip( hTable, 1 )
   IF AdsAtEOF( hTable, @nEof ) != 0 .OR. nEof != 1
      cWhy := "EOF not set after skip past last row"
      AdsCloseTable( hTable )
      RETURN .F.
   ENDIF

   AdsCloseTable( hTable )

RETURN .T.

STATIC FUNCTION Nav_IsNullField( cVal )

   LOCAL c := AllTrim( cVal )

RETURN Empty( c ) .OR. c == "NULL"

STATIC FUNCTION Nav_LastAdsErr( cStep )

   LOCAL nErr := 0, cMsg := Space( 256 ), nLen := 256

   OADS_GetLastError( @nErr, @cMsg, @nLen )

RETURN cStep + " err=" + LTrim( Str( nErr ) ) + " " + AllTrim( Left( cMsg, nLen ) )

STATIC FUNCTION Nav_Field( hTable, cName )

   LOCAL cBuf := Space( 256 )
   LOCAL nLen := Len( cBuf )

   IF AdsGetField( hTable, cName, @cBuf, @nLen, 0 ) != 0
      RETURN ""
   ENDIF

RETURN Left( cBuf, nLen )