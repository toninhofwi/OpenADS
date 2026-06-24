/*
 * ads_prepare_bench.prg - create bench.dbf + bench.cdx via SAP ADS (ace64).
 * Usage: ads_prepare_sap_64.exe <data_dir> [rows]
 */

#include "ads.ch"
#include "dbstruct.ch"
#include "fileio.ch"

REQUEST ADS

PROCEDURE Main( cDir, cRows )

   LOCAL nRows := Val( IIf( Empty( cRows ), "10000", cRows ) )
   LOCAL n, cDbf, cCdx, cTag, nAmt
   LOCAL aStruct := { ;
      { "ID",  "N", 8, 0 }, ;
      { "TAG", "C", 4, 0 }, ;
      { "AMT", "N", 8, 2 } }

   IF nRows < 100
      nRows := 100
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
   cCdx := hb_FNameExtSet( cDbf, ".cdx" )

   rddSetDefault( "ADS" )
   AdsSetServerType( ADS_LOCAL_SERVER )
   AdsSetFileType( ADS_CDX )
   AdsRightsCheck( .F. )

   IF !AdsConnect( cDir )
      ? "FAIL AdsConnect " + cDir
      ErrorLevel( 1 )
      RETURN
   ENDIF

   IF File( cDbf )
      FErase( cDbf )
   ENDIF
   IF File( hb_FNameExtSet( cDbf, ".cdx" ) )
      FErase( hb_FNameExtSet( cDbf, ".cdx" ) )
   ENDIF

   DbCreate( cDbf, aStruct, "ADS" )
   USE ( cDbf ) VIA "ADS" ALIAS bench EXCLUSIVE
   FOR n := 1 TO nRows
      cTag := Replicate( Chr( Asc( "A" ) + ( ( n - 1 ) % 26 ) ), 4 )
      nAmt := Round( ( ( n * 17 + 31 ) % 10000 ) / 100, 2 )
      APPEND BLANK
      bench->ID  := n
      bench->TAG := cTag
      bench->AMT := nAmt
   NEXT
   bench->( dbCloseArea() )
   IF File( cCdx )
      FErase( cCdx )
   ENDIF
   AdsDisconnect()

   ? "PREPARE_OK rows=" + LTrim( Str( nRows ) ) + " dir=" + cDir
RETURN