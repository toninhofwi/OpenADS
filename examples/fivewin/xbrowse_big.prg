// xbrowse_big.prg — FiveWin xBrowse over a *remote* 1 GB Advantage
// table, through Harbour's rddads → OpenADS' ace64.dll → openads_serverd
// over TCP. Shows that the xBrowse drives a multi-million-row table on
// the wire (relative-position scrollbar, etc.).
//
// Server URI: env var OPENADS_XS_REMOTE, else the documented dev box.
// Table name: env var OPENADS_XS_BIGTABLE, else "big.dbf".
//
// Build:  build_msvc64.cmd  <openads-ace64-dir>  xbrowse_big
// Run:    xbrowse_big.exe          (interactive xBrowse window)
//         xbrowse_big.exe /auto    (headless: open, nav, close; exit 0)

#include "FiveWin.ch"
#include "xbrowse.ch"

REQUEST ADSCDX
REQUEST DBFCDX
// FWH's xBrowse SetRDD() macro-builds bKeyNo/bKeyCount for ADS workareas
// referencing these by name; the linker would dead-strip them otherwise.
REQUEST ADSKEYNO, ADSKEYCOUNT, ADSGETRELKEYPOS, ADSSETRELKEYPOS

#define ADS_REMOTE_SERVER 2
#define ADS_CDX           2

FUNCTION Main( cMode )

   LOCAL oWnd, oBrw
   LOCAL cM    := iif( ValType( cMode ) == "C", Lower( AllTrim( cMode ) ), "" )
   LOCAL lAuto  := ( cM == "/auto" )
   LOCAL lSpeed := ( cM == "/speed" )
   LOCAL cUri  := GetEnv( "OPENADS_XS_REMOTE" )
   LOCAL cTbl  := GetEnv( "OPENADS_XS_BIGTABLE" )
   LOCAL hConn := 0
   LOCAL rc

   IF Empty( cUri ) ; cUri := "tcp://192.168.18.184:16262//tmp/openads_mac" ; ENDIF
   IF Empty( cTbl ) ; cTbl := "big.dbf" ; ENDIF

   AdsSetFileType( ADS_CDX )
   // Harbour rddads' AdsConnect60() returns .T./.F. and stores the
   // handle in the 6th (by-ref) arg; it also becomes the default
   // connection that the next USE picks up.
   IF ! AdsConnect60( cUri, ADS_REMOTE_SERVER, NIL, NIL, 0, @hConn )
      MsgStop( "AdsConnect60 failed (remote)" + CRLF + "URI: " + cUri )
      RETURN 1
   ENDIF

   USE ( cTbl ) VIA "ADSCDX" ALIAS BIG SHARED NEW
   IF Select( "BIG" ) == 0
      MsgStop( "USE " + cTbl + " VIA ADSCDX failed (remote)" )
      AdsDisConnect( hConn )
      RETURN 1
   ENDIF
   BIG->( DbGoTop() )

   IF lAuto
      rc := AutoWalk()
      AdsDisConnect( hConn )
      RETURN rc
   ENDIF

   DEFINE WINDOW oWnd FROM 1, 1 TO 30, 116 ;
      TITLE "OpenADS remote 1 GB — " + AdsVersion() + "  |  " + ;
            AllTrim( Transform( BIG->( RecCount() ), "999,999,999" ) ) + " records  |  " + cUri

   @ 0, 0 XBROWSE oBrw OF oWnd ;
      ALIAS "BIG" AUTOCOLS AUTOSORT ;
      CELL LINES NOBORDER FOOTERS

   oBrw:CreateFromCode()
   oWnd:oClient := oBrw

   ACTIVATE WINDOW oWnd ;
      ON INIT ( oBrw:SetFocus(), oBrw:GoTop(), oBrw:Refresh(), ;
                iif( lSpeed, SpeedTest( oBrw, oWnd ), nil ) )

   BIG->( DbCloseArea() )
   AdsDisConnect( hConn )
   ? "OK: FWH xBrowse over remote 1GB DBF ran"
   RETURN 0

//----------------------------------------------------------------------------//
// /speed: time the navigation an xBrowse drives over the wire on the
// 1 GB table, then show the numbers and close.

STATIC FUNCTION SpeedTest( oBrw, oWnd )
   LOCAL nRecs := BIG->( RecCount() )
   LOCAL n, t, c := "Table: " + AllTrim( Transform( nRecs, "999,999,999" ) ) + " records (1 GB), over the wire" + CRLF + CRLF

   t := Seconds() ; oBrw:GoTop()    ; oBrw:Refresh() ; SysRefresh()
   c += "GoTop          : " + Str( ( Seconds() - t ) * 1000, 8, 1 ) + " ms" + CRLF
   t := Seconds() ; oBrw:GoBottom() ; oBrw:Refresh() ; SysRefresh()
   c += "GoBottom       : " + Str( ( Seconds() - t ) * 1000, 8, 1 ) + " ms   (recno " + AllTrim( Str( BIG->( RecNo() ) ) ) + ")" + CRLF
   oBrw:GoTop() ; SysRefresh()
   t := Seconds()
   FOR n := 1 TO 50 ; oBrw:PageDown() ; oBrw:Refresh() ; SysRefresh() ; NEXT
   c += "50x PageDown   : " + Str( ( Seconds() - t ) * 1000, 8, 1 ) + " ms   (" + Str( ( ( Seconds() - t ) * 1000 ) / 50, 6, 2 ) + " ms/page, recno " + AllTrim( Str( BIG->( RecNo() ) ) ) + ")" + CRLF
   oBrw:GoTop() ; SysRefresh()
   t := Seconds()
   FOR n := 1 TO 500 ; oBrw:GoDown() ; NEXT ; oBrw:Refresh() ; SysRefresh()
   c += "500x GoDown    : " + Str( ( Seconds() - t ) * 1000, 8, 1 ) + " ms   (" + Str( ( ( Seconds() - t ) * 1000 ) / 500, 6, 3 ) + " ms/row, recno " + AllTrim( Str( BIG->( RecNo() ) ) ) + ")" + CRLF
   t := Seconds()
   FOR n := 1 TO 20 ; BIG->( DbGoto( ( n * 251237 ) % nRecs + 1 ) ) ; oBrw:Refresh() ; SysRefresh() ; NEXT
   c += "20x random GoTo: " + Str( ( Seconds() - t ) * 1000, 8, 1 ) + " ms   (" + Str( ( ( Seconds() - t ) * 1000 ) / 20, 6, 2 ) + " ms/jump)" + CRLF
   FW_MemoEdit( c, "OpenADS — FWH xBrowse over remote 1 GB DBF: speed" )  // copyable
   oWnd:End()
   RETURN nil

//----------------------------------------------------------------------------//

STATIC FUNCTION AutoWalk()
   LOCAL nRecs := BIG->( RecCount() )
   LOCAL r1, rN
   IF nRecs < 1000000
      ? "FAIL: RecCount =", nRecs ; BIG->( DbCloseArea() ) ; RETURN 1
   ENDIF
   BIG->( DbGoTop() )    ; r1 := BIG->( RecNo() )
   BIG->( DbGoBottom() ) ; rN := BIG->( RecNo() )
   IF r1 != 1 .OR. rN != nRecs
      ? "FAIL: GoTop/GoBottom recno =", r1, rN, "(expected 1,", nRecs, ")" ; BIG->( DbCloseArea() ) ; RETURN 1
   ENDIF
   BIG->( DbGoto( Int( nRecs / 2 ) ) )
   IF BIG->( RecNo() ) != Int( nRecs / 2 )
      ? "FAIL: DbGoto(mid)" ; BIG->( DbCloseArea() ) ; RETURN 1
   ENDIF
   BIG->( DbCloseArea() )
   ? "OK: FWH /auto remote 1GB —", AllTrim( Str( nRecs ) ), "records, GoTop/GoBottom/GoTo OK"
   RETURN 0
