/*
 * adt_native_demo.prg — native ADT / ADI / ADM console sample for OpenADS.
 *
 * Creates a .adt table with memo (.adm), builds a .adi index, appends
 * rows, and seeks by key — all through Harbour contrib/rddads (ADSADT).
 *
 * by glokcode
 *
 * Build:  hbmk2 adt_native_demo.hbp   (after set OPENADS_LIB=...)
 * Run:    adt_native_demo.exe
 */
#include "ads.ch"
#include "rddsys.ch"

REQUEST ADS, ADSADT

PROCEDURE Main()

   LOCAL cDir := hb_DirTemp() + "openads_adt_native_demo"
   LOCAL cTbl := cDir + hb_ps() + "clientes.adt"

   ? "OpenADS native ADT / ADI / ADM demo"
   ? "by glokcode"
   ? "ACE DLL:", AdsVersion()
   ?

   AdsSetServerType( ADS_LOCAL_SERVER )
   SET FILETYPE TO ADT
   RddSetDefault( "ADSADT" )

   hb_DirCreate( cDir )

   IF ! AdsConnect( cDir )
      ? "AdsConnect failed:", DosError()
      ErrorLevel( 1 )
      RETURN
   ENDIF

   IF File( cTbl )
      FErase( cTbl )
      FErase( hb_FNameExtSet( cTbl, ".adi" ) )
      FErase( hb_FNameExtSet( cTbl, ".adm" ) )
   ENDIF

   DbCreate( cTbl, { ;
       { "Nome", "C", 20, 0 }, ;
       { "Qtd",  "N",  6, 0 }, ;
       { "Obs",  "M", 10, 0 } }, "ADSADT" )

   USE ( cTbl ) VIA "ADSADT" NEW EXCLUSIVE
   INDEX ON FIELD->Nome TAG NomeIdx

   DbAppend()
   FIELD->Nome := "Ana Silva"
   FIELD->Qtd  := 12
   FIELD->Obs  := "memo linha 1"
   DbAppend()
   FIELD->Nome := "Bruno Costa"
   FIELD->Qtd  := 7
   FIELD->Obs  := "memo linha 2"
   DbCommit()

   ? "Records:", LastRec()
   ?
   ? "Walk natural order:"
   dbGoTop()
   DO WHILE ! Eof()
      ? "  rec", RecNo(), ;
        "nome=[" + AllTrim( FIELD->Nome ) + "]" ;
        " qtd=" + LTrim( Str( FIELD->Qtd ) ) ;
        " obs=[" + AllTrim( FIELD->Obs ) + "]"
      dbSkip()
   ENDDO
   ?

   OrdSetFocus( "NomeIdx" )
   dbSeek( PadR( "Bruno Costa", 20 ) )
   ? "Seek 'Bruno Costa':", ;
     iif( Found(), "found rec " + LTrim( Str( RecNo() ) ), "not found" )
   IF Found()
      ? "  obs=[" + AllTrim( FIELD->Obs ) + "] qtd=" + LTrim( Str( FIELD->Qtd ) )
   ENDIF

   DbCloseArea()
   AdsDisconnect()

   ? "Done."
   RETURN