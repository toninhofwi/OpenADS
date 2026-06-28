/* OpenADS Harbour smoke — sqlite:// SQL URI backend (SQLRDD parity).
 * Uses rddads SQL workarea helpers (AdsCreateSQLStatement / AdsExecuteSQLDirect). */
#include "ads.ch"

REQUEST ADS

PROCEDURE Main()
   LOCAL hConn := 0, cDb, cUri, lOk := .T.

   ErrorBlock( {|oErr| MyHandler( oErr ) } )

   ? "OpenADS SQLite URI smoke"
   ? "ACE DLL:", AdsVersion()

   cDb := hb_DirBase() + "smoke_uri.db"
   cUri := "sqlite://" + StrTran( cDb, "\", "/" )
   ? "URI:", cUri

   IF ! AdsConnect60( cUri, ADS_LOCAL_SERVER, NIL, NIL, 0, @hConn )
      ? "AdsConnect60 failed"
      ErrorLevel( 1 )
      RETURN
   ENDIF

   IF AdsCreateSQLStatement( "prep", ADS_ADT )
      AdsExecuteSQLDirect( "DROP TABLE IF EXISTS items" )
      USE
   ENDIF

   IF AdsCreateSQLStatement( "ddl", ADS_ADT )
      IF ! AdsExecuteSQLDirect( "CREATE TABLE items (NAME TEXT)" )
         ? "CREATE TABLE failed"
         lOk := .F.
      ENDIF
      USE
   ENDIF

   IF lOk .AND. AdsCreateSQLStatement( "dml", ADS_ADT )
      IF ! AdsExecuteSQLDirect( ;
            "INSERT INTO items (NAME) VALUES ('alpha')" )
         ? "INSERT failed"
         lOk := .F.
      ENDIF
      USE
   ENDIF

   IF lOk .AND. AdsCreateSQLStatement( "flt", ADS_ADT )
      IF ! AdsExecuteSQLDirect( ;
            "SELECT 1 AS CNT FROM items WHERE NAME = 'alpha'" )
         ? "Filter query failed"
         lOk := .F.
      ELSE
         ? "Filter query ok"
      ENDIF
      USE
   ENDIF

   IF lOk .AND. AdsCreateSQLStatement( "sys", ADS_ADT )
      IF AdsExecuteSQLDirect( "SELECT * FROM system.tables" )
         ? "system.tables ok, rows:", RecCount()
      ELSE
         ? "system.tables query failed"
         lOk := .F.
      ENDIF
      USE
   ENDIF

   IF AdsCreateSQLStatement( "drop", ADS_ADT )
      AdsExecuteSQLDirect( "DROP TABLE IF EXISTS items" )
      USE
   ENDIF

   AdsDisconnect( hConn )
   ? "Done."
   IF ! lOk
      ErrorLevel( 1 )
   ENDIF
   RETURN

PROCEDURE MyHandler( oErr )
   ? "ERROR:", oErr:Description
   BREAK
   RETURN