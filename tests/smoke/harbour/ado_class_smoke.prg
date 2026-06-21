/*
 * ado_class_smoke.prg — exercises the OpenADS ADO bridge *through the class*
 * (TOpenAdsConnection / TOpenAdsRecordSet / GetRows), not by calling the ACE
 * ABI directly. This is the regression net that must stay green before and
 * after the GetRows C hot-path swap: same asserts, identical results.
 *
 * Build:  hbmk2 ado_class_smoke.hbp
 * Run:    ado_class_smoke.exe          (openace64.dll with the backend +
 *                                       passthrough must be first on PATH)
 *
 * URI: defaults to the portable MariaDB used in the e2e; override with
 *      set ADO_SMOKE_URI=mariadb://root@127.0.0.1:3306/test
 *      (or sqlite://..., postgresql://..., a local DBF dir, etc.)
 */

#include "ads.ch"

REQUEST ADS, ADSCDX, ADSNTX

PROCEDURE Main()

   LOCAL cUri := hb_GetEnv( "ADO_SMOKE_URI" )
   LOCAL oCn, oRs, aCols, aRows
   LOCAL nPass := 0, nFail := 0

   IF Empty( cUri )
      cUri := "sqlite://ado_smoke.db"
   ENDIF

   Trace( "1 start" )
   ? "OpenADS ADO bridge - class smoke (uses the class, not the raw ABI)"
   Trace( "2 before AdsVersion" )
   ? "ACE DLL :", AdsVersion()
   Trace( "3 AdsVersion ok" )
   ? "URI     :", cUri
   ?

   /* 1) connect THROUGH the bridge entry point -> TOpenAdsConnection */
   Trace( "4 connecting: " + cUri )
   oCn := OpenAds_AdoTryBridge( "OPENADS," + cUri, .F. )
   Trace( "5 connect returned" )
   IF oCn == NIL .OR. ! HB_ISOBJECT( oCn ) .OR. oCn:State() != 1
      ? "FATAL: TOpenAdsConnection did not connect (URI/server/DLL?)."
      ErrorLevel( 2 )
      QUIT
   ENDIF
   ? "connected: hConn =", oCn:hConn

   /* 2) setup via Connection:Execute (idempotent, errors are harmless NILs) */
   oCn:Execute( "DROP TABLE ado_smoke" )
   oCn:Execute( "CREATE TABLE ado_smoke ( id INT, name VARCHAR(30), age INT )" )
   oCn:Execute( "INSERT INTO ado_smoke ( id, name, age ) VALUES ( 1, 'alice', 30 )" )
   oCn:Execute( "INSERT INTO ado_smoke ( id, name, age ) VALUES ( 2, 'BOB',   25 )" )
   oCn:Execute( "INSERT INTO ado_smoke ( id, name, age ) VALUES ( 3, 'delta', 99 )" )

   Trace( "6 setup (DDL/DML) done; opening recordset" )
   /* 3) open a recordset THROUGH the class */
   oRs := OpenAds_OpenRecordSet( oCn, ;
      "SELECT id, name, age FROM ado_smoke ORDER BY id" )
   Trace( "7 recordset open returned" )
   IF oRs == NIL .OR. ! HB_ISOBJECT( oRs )
      ? "FATAL: OpenAds_OpenRecordSet returned NIL."
      oCn:Close()
      ErrorLevel( 2 )
      QUIT
   ENDIF

   AssertEq( oRs:oFields:Count(), 3, "field count",       @nPass, @nFail )
   AssertEq( oRs:RecordCount(),   3, "record count",      @nPass, @nFail )

   Trace( "8 calling GetRows" )
   /* 4) GetRows() - column-major; this is the hot path moving to C */
   aCols := oRs:GetRows()
   Trace( "9 GetRows returned rows=" + hb_CStr( Len( aCols ) ) )
   AssertEq( Len( aCols ),       3, "GetRows column count", @nPass, @nFail )
   IF Len( aCols ) == 3
      AssertEq( Len( aCols[ 1 ] ), 3, "GetRows row count",  @nPass, @nFail )
      AssertEq( AllTrim( aCols[ 2 ][ 1 ] ), "alice", "row1 name", @nPass, @nFail )
      AssertEq( AllTrim( aCols[ 2 ][ 2 ] ), "BOB",   "row2 name", @nPass, @nFail )
      AssertEq( AllTrim( aCols[ 2 ][ 3 ] ), "delta", "row3 name", @nPass, @nFail )
      AssertEq( Val( aCols[ 3 ][ 3 ] ),     99,      "row3 age",  @nPass, @nFail )
   ENDIF

   /* 5) RsGetRows() — row-major transpose used by TDataBase:SqlQuery */
   aRows := OpenAds_RsGetRows( oRs )
   AssertEq( Len( aRows ), 3, "RsGetRows row count", @nPass, @nFail )
   IF Len( aRows ) == 3
      AssertEq( AllTrim( aRows[ 2 ][ 2 ] ), "BOB", "rowmajor [2][2]", @nPass, @nFail )
   ENDIF

   oRs:Close()
   oCn:Close()

   ?
   ? "RESULT:", LTrim( Str( nPass ) ), "pass /", LTrim( Str( nFail ) ), "fail"
   ErrorLevel( iif( nFail == 0, 0, 1 ) )
   RETURN

STATIC PROCEDURE AssertEq( xGot, xExp, cMsg, nPass, nFail )
   LOCAL lOk := ( ValType( xGot ) == ValType( xExp ) .AND. xGot == xExp )
   IF lOk
      nPass++
      ? "  ok  ", cMsg
   ELSE
      nFail++
      ? "  FAIL", cMsg, "(got=[" + cValToChar( xGot ) + "] exp=[" + ;
        cValToChar( xExp ) + "])"
   ENDIF
   RETURN

STATIC FUNCTION cValToChar( x )
   RETURN iif( ValType( x ) == "C", x, hb_CStr( x ) )

/* flushed progress tracer: hb_MemoWrit closes the file each call, so trace.log
   on disk always reflects the last checkpoint reached even if the app hangs. */
STATIC PROCEDURE Trace( cMsg )
   STATIC s_cAll := ""
   s_cAll += cMsg + hb_eol()
   hb_MemoWrit( "trace.log", s_cAll )
   RETURN
