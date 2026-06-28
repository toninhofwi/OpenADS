/* OpenADS Harbour smoke — sqlite:// SQL URI backend (SQLRDD parity).
 * Uses rddads SQL workarea helpers (AdsCreateSQLStatement / AdsExecuteSQLDirect)
 * rather than low-level ACE navigational entry points not exported by Harbour. */
#include "ads.ch"

REQUEST ADS

PROCEDURE Main()
   LOCAL hConn := 0, n := 0, cName := Space( 200 )
   LOCAL cDb, cUri

   ErrorBlock( {|oErr| MyHandler( oErr ) } )

   ? "OpenADS SQLite URI smoke"
   ? "ACE DLL:", AdsVersion()

   cDb := hb_DirBase() + "smoke_uri.db"
   cUri := "sqlite://" + StrTran( cDb, "\", "/" )
   ? "URI:", cUri

   IF AdsConnect60( cUri, ADS_LOCAL_SERVER, NIL, NIL, 0, @hConn ) != 0
      ? "AdsConnect60 failed"
      RETURN
   ENDIF

   IF AdsCreateSQLStatement( "ddl", ADS_ADT )
      IF ! AdsExecuteSQLDirect( ;
            "CREATE TABLE items (NAME TEXT)" )
         ? "CREATE TABLE failed"
      ENDIF
      USE
   ENDIF

   IF AdsCreateSQLStatement( "dml", ADS_ADT )
      IF ! AdsExecuteSQLDirect( ;
            "INSERT INTO items (NAME) VALUES ('alpha')" )
         ? "INSERT failed"
      ENDIF
      USE
   ENDIF

   IF AdsCreateSQLStatement( "flt", ADS_ADT )
      IF AdsExecuteSQLDirect( ;
            "SELECT COUNT(*) AS CNT FROM items WHERE NAME = 'alpha'" )
         dbGoTop()
         n := FIELD->CNT
         ? "Filtered count:", n
      ELSE
         ? "Filter count query failed"
      ENDIF
      USE
   ENDIF

   IF AdsCreateSQLStatement( "all", ADS_ADT )
      IF AdsExecuteSQLDirect( "SELECT COUNT(*) AS CNT FROM items" )
         dbGoTop()
         n := FIELD->CNT
         ? "Total count:", n
      ENDIF
      USE
   ENDIF

   IF AdsCreateSQLStatement( "sys", ADS_ADT )
      IF AdsExecuteSQLDirect( "SELECT * FROM system.tables" )
         dbGoTop()
         cName := FIELD->Name
         ? "system.tables first:", AllTrim( cName )
      ELSE
         ? "system.tables query failed"
      ENDIF
      USE
   ENDIF

   IF AdsCreateSQLStatement( "drop", ADS_ADT )
      AdsExecuteSQLDirect( "DROP TABLE items" )
      USE
   ENDIF

   AdsDisconnect( hConn )
   ? "Done."
   RETURN

PROCEDURE MyHandler( oErr )
   ? "ERROR:", oErr:Description
   ErrorLevel( 1 )
   QUIT
   RETURN