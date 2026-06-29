/*
 * colonias.prg — FiveWin xBrowse over a remote OpenADS table.
 *
 * Build:  build_gui.cmd
 * Run:    colonias.exe
 */
#include "ads.ch"
#include "FiveWin.ch"
#include "xbrowse.ch"

REQUEST ADSCDX
REQUEST ADSKEYNO, ADSKEYCOUNT, ADSGETRELKEYPOS, ADSSETRELKEYPOS

PROCEDURE Main()

   LOCAL hConn := 0
   LOCAL cUri  := GetEnv( "OADS_REMOTE_URI" )
   LOCAL nErr  := 0
   LOCAL cErr  := Space( 200 )

   IF Empty( cUri )
      cUri := "tcp://127.0.0.1:6262/"
   ENDIF

   IF ! InicializarOpenADS( .F. )
      AdsGetLastError( @nErr, @cErr )
      MsgStop( "InicializarOpenADS fallo." + CRLF + ;
               "Codigo: " + hb_ValToStr( nErr ) + CRLF + ;
               "Detalle: " + AllTrim( cErr ) + CRLF + ;
               "URI: " + cUri )
      RETURN
   ENDIF

   MsgInfo( "Conectado a " + cUri + CRLF + ;
            "Registros: " + hb_ValToStr( CCOLONIA->( LastRec() ) ) )

   RunXBrowse()

   RETURN

STATIC PROCEDURE RunXBrowse()

   LOCAL oWnd, oBrw

   DEFINE WINDOW oWnd FROM 2, 4 TO 24, 90 ;
      TITLE "Catalogo de Colonias (OpenADS remoto) — " + AdsVersion()

   @ 0, 0 XBROWSE oBrw OF oWnd ;
      ALIAS "CCOLONIA" AUTOCOLS ;
      CELL LINES NOBORDER

   oBrw:CreateFromCode()
   oWnd:oClient := oBrw

   ACTIVATE WINDOW oWnd ;
      ON INIT ( oBrw:SetFocus(), oBrw:GoTop(), oBrw:Refresh() )

   CCOLONIA->( DbCloseArea() )
   AdsDisconnect()

   RETURN