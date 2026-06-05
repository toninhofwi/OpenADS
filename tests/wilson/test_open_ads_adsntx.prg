/* OpenADS / Harbour rddads NTX smoke test.
 *
 * Opens a DBF table with two Clipper-style NTX indexes through
 * the ADS RDD in remote-server mode. Walks top→bottom and
 * verifies both indexes are active.
 *
 * Fixture: ejecutiv.dbf + EJEC1X.ntx + EJEC2X.NTX
 *   EJEC1X: index on DIAS_MAS (numeric, ascending)
 *   EJEC2X: index on E_NOMBRE (character, ascending)
 */
#include "ads.ch"

REQUEST ADS, ADSNTX

PROCEDURE Main()
   ErrorBlock( {|oErr| MyHandler( oErr ) } )

   ? "OpenADS NTX index test (wilson)"
   ? "ACE DLL reports:", AdsVersion()

   AdsSetFileType( ADS_NTX )
   AdsSetServerType( ADS_REMOTE_SERVER )

   IF ! AdsConnect( "." )
      ? "AdsConnect failed."
      RETURN
   ENDIF

   USE ejecutiv INDEX ejec1x, ejec2x NEW SHARED ALIAS ejecutiv
   IF NetErr()
      ? "USE failed:", FError()
      RETURN
   ENDIF

   ? "Reccount:", LTrim(Str(Reccount()))
   ? "Number of orders:", OrdCount()

   ? ""
   ? "=== Index: EJEC1X (DIAS_MAS) ==="
   OrdSetFocus( "EJEC1X" )
   ? "Active order:", OrdName(), "  key:", OrdKey()
   dbGoTop()
   ? "Top    rec", LTrim(Str(RecNo())), "DIAS_MAS=", LTrim(Str(FIELD->DIAS_MAS)), "NOMBRE=[" + FIELD->E_NOMBRE + "]"
   dbGoBottom()
   ? "Bottom rec", LTrim(Str(RecNo())), "DIAS_MAS=", LTrim(Str(FIELD->DIAS_MAS)), "NOMBRE=[" + FIELD->E_NOMBRE + "]"

   ? ""
   ? "=== Index: EJEC2X (E_NOMBRE) ==="
   OrdSetFocus( "EJEC2X" )
   ? "Active order:", OrdName(), "  key:", OrdKey()
   dbGoTop()
   ? "Top    rec", LTrim(Str(RecNo())), "NOMBRE=[" + FIELD->E_NOMBRE + "]"
   dbGoBottom()
   ? "Bottom rec", LTrim(Str(RecNo())), "NOMBRE=[" + FIELD->E_NOMBRE + "]"

   ? ""
   ? "=== Walk all rows via EJEC2X (name order) ==="
   OrdSetFocus( "EJEC2X" )
   dbGoTop()
   DO WHILE ! Eof()
      ? "  rec", LTrim(Str(RecNo())), "NOMBRE=[" + FIELD->E_NOMBRE + "]", ;
        "DIAS_MAS=" + LTrim(Str(FIELD->DIAS_MAS))
      dbSkip()
   ENDDO

   dbCloseArea()
   ? "Done."
   RETURN

PROCEDURE MyHandler( oErr )
   ? "ERROR:", oErr:Description, "[" + LTrim(Str(oErr:GenCode)) + "/" + ;
        LTrim(Str(oErr:SubCode)) + "]"
   ? "Operation:", oErr:Operation
   ErrorLevel( 1 )
   QUIT
   RETURN
