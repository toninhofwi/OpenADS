/*
 * ads_engine_bench.prg — SAP ADS vs OpenADS engine A/B (local DBF/CDX).
 * Usage: ads_engine_bench_64.exe <data_dir> [rows] [repeats] [rdd]
 * Env: ADS_ENGINE_NAME=sap|openads  ADS_ENGINE_LOG=path (optional)
 */

#include "ads.ch"
#include "dbstruct.ch"
#include "fileio.ch"

REQUEST ADS

#define SEEK_OPS 200

STATIC s_cLog := ""
STATIC s_cEngine := "openads"

PROCEDURE Main( cDir, cRows, cRepeats, cRdd )

   LOCAL nRows := Val( IIf( Empty( cRows ), "10000", cRows ) )
   LOCAL nRepeats := Val( IIf( Empty( cRepeats ), "3", cRepeats ) )
   LOCAL lRdd := .F.
   LOCAL cDbf := ""
   LOCAL cTag := "AAAA"
   LOCAL lSql := .F.
   LOCAL aSeek := {}, aAof := {}, aScan := {}, aSqlCnt := {}, aSqlFetch := {}
   LOCAL nMed, n
   LOCAL cLog := AllTrim( GetEnv( "ADS_ENGINE_LOG" ) )

   s_cEngine := AllTrim( GetEnv( "ADS_ENGINE_NAME" ) )
   IF Empty( s_cEngine )
      s_cEngine := "openads"
   ENDIF

   IF !Empty( cRdd ) .AND. Upper( AllTrim( cRdd ) ) == "RDD"
      lRdd := .T.
   ENDIF
   IF nRows < 100
      nRows := 100
   ENDIF
   IF nRepeats < 1
      nRepeats := 1
   ENDIF
   IF Empty( cDir )
      cDir := "data"
   ENDIF
   IF ! ( ":" $ cDir .OR. Left( cDir, 2 ) == "\\" )
      cDir := hb_FNameMerge( hb_cwd(), cDir )
   ENDIF
   IF Right( cDir, 1 ) $ "\/"
      cDir := Left( cDir, Len( cDir ) - 1 )
   ENDIF
   cDbf := hb_FNameMerge( cDir, "bench.dbf" )

   s_cLog := ""
   LogLine( "=== ads_engine_bench engine=" + s_cEngine + " ===" )
   LogLine( "cwd=" + hb_cwd() )
   LogLine( "data=" + cDir + " rows=" + LTrim( Str( nRows ) ) + ;
            " repeats=" + LTrim( Str( nRepeats ) ) )

   IF !File( cDbf )
      LogLine( "FAIL bench.dbf missing: " + cDbf )
      FlushLog( cLog )
      ErrorLevel( 1 )
      RETURN
   ENDIF

   IF !InitAds()
      FlushLog( cLog )
      ErrorLevel( 1 )
      RETURN
   ENDIF

   AdsDisconnect()
   IF !AdsConnect( cDir )
      LogLine( "FAIL AdsConnect " + cDir )
      FlushLog( cLog )
      ErrorLevel( 1 )
      RETURN
   ENDIF
   LogLine( "connect OK" )
   CleanOpenAdsSidecars( cDir )
   IF !EnsureIndex( cDir )
      LogLine( "FAIL EnsureIndex" )
      FlushLog( cLog )
      ErrorLevel( 1 )
      RETURN
   ENDIF

   lSql := File( hb_cwd() + hb_ps() + "openace64.dll" )

   LogLine( "" )
   LogLine( "--- workloads (median ms) ---" )

   FOR n := 1 TO nRepeats
      IF lSql
         AAdd( aSqlCnt, Bench_SqlCount( cDir, cTag ) )
         AAdd( aSqlFetch, Bench_SqlFetch( cDir, cTag ) )
      ENDIF
      IF lRdd
         AAdd( aSeek, Bench_SeekEq( cDir, cTag ) )
         AAdd( aAof, Bench_AofEq( cDir, cTag ) )
         AAdd( aScan, Bench_RddScanEq( cDir, cTag ) )
      ENDIF
   NEXT

   IF lRdd
      nMed := Median( aSeek )
      LogLine( PadR( "seek_eq", 14 ) + LTrim( Str( nMed, 10, 1 ) ) + " ms" )
      EmitRow( "seek_eq", nMed )
      nMed := Median( aAof )
      LogLine( PadR( "aof_eq", 14 ) + LTrim( Str( nMed, 10, 1 ) ) + " ms" )
      EmitRow( "aof_eq", nMed )
      nMed := Median( aScan )
      LogLine( PadR( "rdd_scan_eq", 14 ) + LTrim( Str( nMed, 10, 1 ) ) + " ms" )
      EmitRow( "rdd_scan_eq", nMed )
   ENDIF

   IF lSql
      nMed := Median( aSqlCnt )
      LogLine( PadR( "sql_count", 14 ) + LTrim( Str( nMed, 10, 1 ) ) + " ms" )
      EmitRow( "sql_count", nMed )
      nMed := Median( aSqlFetch )
      LogLine( PadR( "sql_fetch", 14 ) + LTrim( Str( nMed, 10, 1 ) ) + " ms" )
      EmitRow( "sql_fetch", nMed )
   ENDIF

   LogLine( "" )
   LogLine( "ENGINE_SUMMARY,engine=" + s_cEngine + ",rows=" + ;
            LTrim( Str( nRows ) ) + ",repeats=" + LTrim( Str( nRepeats ) ) + ",pass=1" )
   LogLine( "PASS ads_engine_bench" )

   AdsDisconnect()
   FlushLog( cLog )

RETURN

STATIC FUNCTION InitAds()
   rddSetDefault( "ADS" )
   AdsSetServerType( ADS_LOCAL_SERVER )
   AdsSetFileType( ADS_CDX )
   AdsSetDateFormat( "yyyy/mm/dd" )
   AdsRightsCheck( .F. )
   IF Type( "AdsCacheRecords" ) <> "U"
      AdsCacheRecords( 64 )
   ENDIF
RETURN .T.

STATIC FUNCTION Bench_SeekEq( cDir, cTag )
   LOCAL nT0 := BenchNow(), n, cCdx := hb_FNameMerge( cDir, "bench.cdx" )
   CloseAlias( "WBCH" )
   USE ( hb_FNameMerge( cDir, "bench.dbf" ) ) VIA "ADS" ALIAS WBCH SHARED
   IF NetErr() .OR. Select( "WBCH" ) == 0
      RETURN -1
   ENDIF
   IF File( cCdx )
      WBCH->( OrdListAdd( cCdx ) )
   ENDIF
   IF WBCH->( OrdCount() ) > 0
      WBCH->( OrdSetFocus( 1 ) )
   ENDIF
   FOR n := 1 TO SEEK_OPS
      WBCH->( dbSeek( cTag, .T. ) )
   NEXT
   WBCH->( dbCloseArea() )
RETURN BenchNow() - nT0

STATIC FUNCTION Bench_AofEq( cDir, cTag )
   LOCAL nT0 := BenchNow(), nCnt := 0, nLvl := 0
   LOCAL cCond := "TAG = '" + cTag + "'"
   CloseAlias( "WBCH" )
   USE ( hb_FNameMerge( cDir, "bench.dbf" ) ) VIA "ADS" ALIAS WBCH SHARED
   IF WBCH->( OrdCount() ) > 0
      WBCH->( OrdSetFocus( 1 ) )
   ENDIF
   IF Type( "AdsSetAOF" ) <> "U"
      IF AdsSetAOF( cCond )
         IF Type( "AdsGetAOFOptLevel" ) <> "U"
            nLvl := AdsGetAOFOptLevel()
         ENDIF
         WBCH->( dbGoTop() )
         DO WHILE !WBCH->( Eof() )
            nCnt++
            WBCH->( dbSkip() )
         ENDDO
         AdsClearAOF()
      ENDIF
   ENDIF
   WBCH->( dbCloseArea() )
   LogLine( "  aof OptLevel=" + LTrim( Str( nLvl ) ) + " hits=" + LTrim( Str( nCnt ) ) )
RETURN BenchNow() - nT0

STATIC FUNCTION Bench_RddScanEq( cDir, cTag )
   LOCAL nT0 := BenchNow(), nCnt := 0
   CloseAlias( "WBCH" )
   USE ( hb_FNameMerge( cDir, "bench.dbf" ) ) VIA "ADS" ALIAS WBCH SHARED
   WBCH->( dbGoTop() )
   DO WHILE !WBCH->( Eof() )
      IF WBCH->TAG == cTag
         nCnt++
      ENDIF
      WBCH->( dbSkip() )
   ENDDO
   WBCH->( dbCloseArea() )
RETURN BenchNow() - nT0

STATIC FUNCTION Bench_SqlCount( cDir, cTag )
   LOCAL nT0 := BenchNow(), cSql, nVal := 0
   cSql := "SELECT COUNT(*) AS N FROM bench.dbf WHERE TAG = '" + cTag + "'"
   IF SqlScalar( cSql, @nVal )
      LogLine( "  sql count=" + LTrim( Str( nVal ) ) )
   ENDIF
RETURN BenchNow() - nT0

STATIC FUNCTION Bench_SqlFetch( cDir, cTag )
   LOCAL nT0 := BenchNow(), cSql, nCnt := 0
   cSql := "SELECT ID, TAG, AMT FROM bench.dbf WHERE TAG = '" + cTag + "'"
   IF SqlCursorOpen( cSql, "Q" )
      DO WHILE SqlCursorFetch( "Q" )
         nCnt++
      ENDDO
      SqlCursorClose( "Q" )
   ENDIF
   LogLine( "  sql fetch rows=" + LTrim( Str( nCnt ) ) )
RETURN BenchNow() - nT0

STATIC FUNCTION SqlScalar( cSql, nOut )
   LOCAL aRow := {}
   IF !SqlCursorOpen( cSql, "S" )
      RETURN .F.
   ENDIF
   IF SqlCursorFetch( "S", @aRow ) .AND. Len( aRow ) > 0
      nOut := IIf( ValType( aRow[ 1 ] ) == "N", aRow[ 1 ], Val( aRow[ 1 ] ) )
   ENDIF
   SqlCursorClose( "S" )
RETURN .T.

STATIC FUNCTION SqlCursorOpen( cSql, cAlias )
   LOCAL cErr := ""
   IF Select( cAlias ) > 0
      SqlCursorClose( cAlias )
   ENDIF
   IF !AdsCreateSQLStatement( cAlias, ADS_LOCAL_SERVER )
      AdsGetLastError( @cErr )
      LogLine( "SqlOpen Create: " + cErr )
      RETURN .F.
   ENDIF
   IF !AdsExecuteSQLDirect( cSql )
      AdsGetLastError( @cErr )
      LogLine( "SqlOpen Exec: " + cErr )
      SqlCursorClose( cAlias )
      RETURN .F.
   ENDIF
RETURN ( Select( cAlias ) > 0 )

STATIC FUNCTION SqlCursorFetch( cAlias, aRow )
   LOCAL n, nCols
   IF Select( cAlias ) == 0 .OR. ( cAlias )->( Eof() )
      RETURN .F.
   ENDIF
   nCols := ( cAlias )->( FCount() )
   IF aRow != NIL
      aRow := Array( nCols )
      FOR n := 1 TO nCols
         aRow[ n ] := ( cAlias )->( FieldGet( n ) )
      NEXT
   ENDIF
   ( cAlias )->( dbSkip() )
RETURN .T.

STATIC FUNCTION SqlCursorClose( cAlias )
   IF Select( cAlias ) > 0
      ( cAlias )->( dbCloseArea() )
   ENDIF
RETURN .T.

STATIC PROCEDURE CleanOpenAdsSidecars( cDir )
   LOCAL aFiles := { "openads.txlog", "openads.lsnmap" }, n, cPath
   FOR n := 1 TO Len( aFiles )
      cPath := cDir + aFiles[ n ]
      IF File( cPath )
         FErase( cPath )
      ENDIF
   NEXT
RETURN

STATIC FUNCTION BenchNow()
RETURN hb_MilliSeconds()

STATIC FUNCTION Median( aVals )
   LOCAL a := AClone( aVals ), n, t, i, j, m
   IF Len( a ) == 0
      RETURN 0
   ENDIF
   FOR i := 1 TO Len( a ) - 1
      FOR j := i + 1 TO Len( a )
         IF a[ j ] < a[ i ]
            t := a[ i ]
            a[ i ] := a[ j ]
            a[ j ] := t
         ENDIF
      NEXT
   NEXT
   n := Len( a )
   m := Int( n / 2 ) + 1
   IF n % 2 == 0
      RETURN ( a[ m ] + a[ m + 1 ] ) / 2
   ENDIF
RETURN a[ m ]

STATIC PROCEDURE EmitRow( cWorkload, nMed )
   LogLine( "ENGINE_ROW,engine=" + s_cEngine + ",workload=" + cWorkload + ;
            ",median_ms=" + LTrim( Str( nMed, 12, 2 ) ) )
RETURN

STATIC PROCEDURE LogLine( cLine )
   s_cLog += cLine + Chr( 10 )
   ? cLine
RETURN

STATIC FUNCTION EnsureIndex( cDir )
   LOCAL cDbf := hb_FNameMerge( cDir, "bench.dbf" )
   LOCAL cCdx := hb_FNameMerge( cDir, "bench.cdx" )
   IF !File( cDbf )
      RETURN .F.
   ENDIF
   IF File( cCdx )
      FErase( cCdx )
   ENDIF
   CloseAlias( "WBIX" )
   USE ( cDbf ) VIA "ADS" ALIAS WBIX EXCLUSIVE
   IF NetErr() .OR. Select( "WBIX" ) == 0
      RETURN .F.
   ENDIF
   INDEX ON TAG TO ( cCdx )
   dbCloseArea()
RETURN .T.

STATIC PROCEDURE CloseAlias( cAlias )
   IF Select( cAlias ) > 0
      ( cAlias )->( dbCloseArea() )
   ENDIF
RETURN

STATIC PROCEDURE FlushLog( cLog )
   LOCAL cOut := IIf( Empty( cLog ), "ads_engine.log", cLog )
   MemoWrit( cOut, s_cLog )
RETURN