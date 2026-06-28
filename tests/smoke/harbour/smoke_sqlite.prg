/* OpenADS Harbour smoke — sqlite:// SQL URI backend (SQLRDD parity). */
#include "ads.ch"

REQUEST ADS

PROCEDURE Main()
   LOCAL hConn := 0, hT := 0, hStmt := 0, hCur := 0
   LOCAL cDb, cUri, n := 0, cName := Space( 200 ), nLen := 200

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

   IF AdsCreateTable( hConn, "items", NIL, ADS_CDX, 0, 0, 0, 0, ;
         "ID,AutoIncrement;NAME,Character,20", @hT ) != 0
      ? "AdsCreateTable failed"
      RETURN
   ENDIF

   IF AdsAppendRecord( hT ) != 0
      ? "AdsAppendRecord failed"
      RETURN
   ENDIF
   AdsSetString( hT, "NAME", "alpha", 5 )
   AdsWriteRecord( hT )

   AdsSetFilter( hT, "NAME = 'alpha'" )
   AdsGetRecordCount( hT, 0, @n )
   ? "Filtered count:", n
   AdsClearFilter( hT )
   AdsGetRecordCount( hT, 0, @n )
   ? "After AdsClearFilter:", n

   AdsCloseTable( hT )

   IF AdsCreateSQLStatement( hConn, @hStmt ) != 0
      ? "AdsCreateSQLStatement failed"
      RETURN
   ENDIF
   IF AdsExecuteSQLDirect( hStmt, "SELECT * FROM system.tables", @hCur ) == 0 .AND. hCur > 0
      AdsGotoTop( hCur )
      AdsGetField( hCur, "Name", @cName, @nLen, 0 )
      ? "system.tables first:", Left( cName, nLen )
      AdsCloseTable( hCur )
   ELSE
      ? "system.tables query failed"
   ENDIF
   IF AdsCreateSQLStatement( hConn, @hStmt ) == 0
      IF AdsExecuteSQLDirect( hStmt, "DROP TABLE items", @hCur ) != 0
         ? "DROP TABLE failed"
      ENDIF
      AdsCloseSQLStatement( hStmt )
   ENDIF

   AdsDisconnect( hConn )
   ? "Done."
   RETURN

PROCEDURE MyHandler( oErr )
   ? "ERROR:", oErr:Description
   ErrorLevel( 1 )
   QUIT
   RETURN