/*
 * demo_ado_multidb.prg -- one and the same pseudo-ADO class
 * (TOpenAdsConnection / TOpenAdsRecordSet) driven against several databases
 * just by changing the connection URI. Console build, no FiveWin link needed.
 *
 * Any OpenADS backend that implements the SQL passthrough (AdsExecuteSQLDirect)
 * works here -- today that is sqlite:// and the local-directory engine. The
 * navigational backends (mariadb:// / postgresql:// / odbc://) are USE/SKIP/SEEK
 * only and do not run AdsExecuteSQLDirect, so the demo skips them cleanly.
 *
 * Build/run: demo_run.bat   (loads the sqlpass openace64.dll at runtime)
 */

#include "ads.ch"

REQUEST ADS, ADSCDX, ADSNTX

PROCEDURE Main()

   LOCAL aCfg := {}
   LOCAL cfg, oCn, oRs, aRows, i, j, cLine

   /* { label, uri, table, createSql, { insertSql... }, selectSql } */
   AAdd( aCfg, { "sqlite / clientes", "sqlite://demo_clientes.db", "clientes", ;
      "CREATE TABLE clientes ( id INTEGER, nome VARCHAR(40), uf CHAR(2) )", ;
      { "INSERT INTO clientes VALUES ( 1, 'Ana Souza',   'SP' )", ;
        "INSERT INTO clientes VALUES ( 2, 'Bruno Lima',  'RJ' )", ;
        "INSERT INTO clientes VALUES ( 3, 'Carla Reis',  'MG' )" }, ;
      "SELECT id, nome, uf FROM clientes ORDER BY id" } )

   AAdd( aCfg, { "sqlite / produtos", "sqlite://demo_produtos.db", "produtos", ;
      "CREATE TABLE produtos ( cod INTEGER, descr VARCHAR(30), preco VARCHAR(12) )", ;
      { "INSERT INTO produtos VALUES ( 10, 'Cabo USB-C', '29.90'  )", ;
        "INSERT INTO produtos VALUES ( 20, 'Fonte 65W',  '149.00' )" }, ;
      "SELECT cod, descr, preco FROM produtos ORDER BY cod" } )

   AAdd( aCfg, { "mariadb (navegacional)", "mariadb://root@127.0.0.1:3306/test", ;
      "t", "CREATE TABLE t ( a INTEGER )", { "INSERT INTO t VALUES ( 1 )" }, ;
      "SELECT a FROM t" } )

   ? "=== OpenADS ADO bridge -- demo multi-DB (the SAME class, several URIs) ==="
   ? "ACE DLL:", AdsVersion()
   ?

   FOR i := 1 TO Len( aCfg )
      cfg := aCfg[ i ]
      ? "--- [" + cfg[ 1 ] + "]  " + cfg[ 2 ]

      oCn := OpenAds_AdoTryBridge( "OPENADS," + cfg[ 2 ], .F. )
      IF oCn == NIL .OR. ! HB_ISOBJECT( oCn ) .OR. oCn:State() != 1
         ? "    skip: nao conectou"
         ?
         LOOP
      ENDIF

      /* setup is idempotent: a failing DROP/CREATE just returns NIL */
      oCn:Execute( "DROP TABLE " + cfg[ 3 ] )
      oCn:Execute( cfg[ 4 ] )
      AEval( cfg[ 5 ], {| cSql | oCn:Execute( cSql ) } )

      oRs := OpenAds_OpenRecordSet( oCn, cfg[ 6 ] )
      IF oRs == NIL
         ? "    skip: sem passthrough (AdsExecuteSQLDirect nao executou)"
      ELSE
         ? "    campos : " + Names2Str( oRs:oFields:aNames )
         ? "    linhas : " + LTrim( Str( oRs:RecordCount() ) )
         aRows := OpenAds_RsGetRows( oRs )
         FOR j := 1 TO Len( aRows )
            cLine := ""
            AEval( aRows[ j ], {| v | cLine += PadR( AllTrim( v ), 16 ) } )
            ? "      " + cLine
         NEXT
         oRs:Close()
      ENDIF

      oCn:Close()
      ?
   NEXT

   ? "Done."
   RETURN

STATIC FUNCTION Names2Str( aNames )
   LOCAL c := "", cN
   FOR EACH cN IN aNames
      c += iif( Empty( c ), "", ", " ) + cN
   NEXT
   RETURN c
