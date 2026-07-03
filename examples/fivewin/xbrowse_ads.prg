// xbrowse_ads.prg — FiveWin + xBrowse over an Advantage table, driven
// by Harbour's rddads contrib linked against OpenADS' ace64.dll
// (instead of a SAP-shipped ACE).
//
// It stages a small .dbf in a temp dir, opens it through the ADSCDX
// RDD, builds NAME/CITY orders, and pops FWH's xBrowser() over the
// workarea. With `/auto` on the command line it skips the GUI and just
// walks the workarea through the RDD (GoBottom / GoTop / Seek) and
// checks the result — handy for a "does it run against OpenADS' DLL"
// smoke check; without it, it's the interactive browser.
//
// Build: build_msvc64.cmd (FWH + Harbour + MSVC 64-bit + rddads +
// OpenADS' ace64), or build64.cmd (FWH bcc64). Put OpenADS' ace64.dll
// on PATH / next to the exe — NOT a SAP one.

#include "FiveWin.ch"
#include "xbrowse.ch"

REQUEST ADSCDX
REQUEST DBFCDX
// FWH's xBrowse SetRDD() builds its bKeyNo / bKeyCount codeblocks for an
// ADS workarea as MACRO strings that reference AdsKeyNo() / AdsKeyCount()
// (and AdsGetRelKeyPos/AdsSetRelKeyPos for >200-row tables). Those names
// appear only inside the macro text, so the linker dead-strips them —
// at run time the browse can't get its row position and renders blank.
// REQUEST keeps them in the image. (Needed for any FWH+rddads+ADS app,
// independent of which ace.dll is underneath.)
REQUEST ADSKEYNO, ADSKEYCOUNT, ADSGETRELKEYPOS, ADSSETRELKEYPOS

#define ADS_LOCAL_SERVER 1
#define ADS_CDX          2

//----------------------------------------------------------------------------//

FUNCTION Main( cMode )

   LOCAL lAuto := ( ValType( cMode ) == "C" .AND. Lower( AllTrim( cMode ) ) == "/auto" )
   LOCAL cDir  := TempFolder() + "\openads_fwh_demo"
   LOCAL cDbf  := cDir + "\customer.dbf"
   LOCAL cCdx  := cDir + "\customer.cdx"

   StageDbf( cDir, cDbf )

   AdsSetServerType( ADS_LOCAL_SERVER )
   AdsSetFileType( ADS_CDX )
   RddSetDefault( "ADSCDX" )

   USE ( cDbf ) VIA "ADSCDX" ALIAS CUST SHARED NEW
   IF Select( "CUST" ) == 0
      ? "FAIL: USE customer VIA ADSCDX"
      RETURN 1
   ENDIF

   IF !File( cCdx )
      INDEX ON CUST->NAME TAG NAME TO ( cCdx )
      INDEX ON CUST->CITY TAG CITY TO ( cCdx )
   ELSE
      SET INDEX TO ( cCdx )
   ENDIF
   OrdSetFocus( "NAME" )
   CUST->( DbGoTop() )

   IF lAuto
      RETURN AutoWalk()
   ENDIF

   ShowBrowse()

   CUST->( DbCloseArea() )
   ? "OK: FWH xBrowse + ADS (via OpenADS) ran"
   RETURN 0

//----------------------------------------------------------------------------//

STATIC FUNCTION ShowBrowse()

   LOCAL oWnd, oBrw

   DbSelectArea( "CUST" )

   DEFINE WINDOW oWnd FROM 1, 1 TO 28, 100 ;
      TITLE "OpenADS — FWH xBrowse over ADS (" + AdsVersion() + ")"

   @ 0, 0 XBROWSE oBrw OF oWnd ;
      ALIAS "CUST" AUTOCOLS AUTOSORT ;
      CELL LINES NOBORDER FOOTERS

   oBrw:CreateFromCode()
   oWnd:oClient := oBrw

   ACTIVATE WINDOW oWnd ;
      ON INIT  ( oBrw:SetFocus(), oBrw:GoTop(), oBrw:Refresh() ) ;
      ON RESIZE ( oBrw:adjust(), oBrw:Refresh() )

   RETURN nil

//----------------------------------------------------------------------------//
// /auto: walk the workarea through the RDD, verify, no GUI.

STATIC FUNCTION AutoWalk()

   LOCAL nRecs := CUST->( RecCount() )
   LOCAL cTop, cBottom

   IF nRecs <= 0
      ? "FAIL: RecCount() =", nRecs ; CUST->( DbCloseArea() ) ; RETURN 1
   ENDIF
   CUST->( DbGoTop() )    ; cTop    := AllTrim( CUST->NAME )
   CUST->( DbGoBottom() ) ; cBottom := AllTrim( CUST->NAME )
   IF cTop != "Alice"  ; ? "FAIL: NAME top ='" + cTop + "'"    ; CUST->( DbCloseArea() ) ; RETURN 1 ; ENDIF
   IF cBottom != "Edward" ; ? "FAIL: NAME bottom ='" + cBottom + "'" ; CUST->( DbCloseArea() ) ; RETURN 1 ; ENDIF
   CUST->( OrdSetFocus( "CITY" ) ) ; CUST->( DbGoTop() )
   IF AllTrim( CUST->CITY ) != "Barcelona"
      ? "FAIL: CITY top ='" + AllTrim( CUST->CITY ) + "'" ; CUST->( DbCloseArea() ) ; RETURN 1
   ENDIF
   CUST->( DbSeek( "Bilbao" ) )
   IF !Found() .OR. AllTrim( CUST->NAME ) != "Edward"
      ? "FAIL: seek 'Bilbao' -> '" + AllTrim( CUST->NAME ) + "'" ; CUST->( DbCloseArea() ) ; RETURN 1
   ENDIF
   CUST->( DbCloseArea() )
   ? "OK: FWH /auto — ADS via OpenADS:", AllTrim( Str( nRecs ) ), "records, nav + seek OK"
   RETURN 0

//----------------------------------------------------------------------------//

STATIC FUNCTION TempFolder()
   LOCAL c := GetEnv( "TEMP" )
   IF Empty( c ) ; c := GetEnv( "TMP" ) ; ENDIF
   IF Empty( c ) ; c := "C:\Temp" ; ENDIF
   RETURN c

//----------------------------------------------------------------------------//
// Stage a 3-field .dbf with a handful of rows (only if it doesn't exist).

STATIC FUNCTION StageDbf( cDir, cDbf )

   LOCAL aStru, aData, aRec

   IF !lIsDir( cDir ) ; MakeDir( cDir ) ; ENDIF
   IF File( cDbf ) ; RETURN nil ; ENDIF

   aStru := { { "NAME", "C", 20, 0 }, ;
              { "CITY", "C", 20, 0 }, ;
              { "AGE" , "N",  3, 0 } }
   DbCreate( cDbf, aStru, "ADSCDX" )

   aData := { { "Alice"  , "Madrid"   , 30 }, ;
              { "Bob"    , "Barcelona", 41 }, ;
              { "Charlie", "Valencia" , 25 }, ;
              { "Diana"  , "Sevilla"  , 37 }, ;
              { "Edward" , "Bilbao"   , 52 } }

   USE ( cDbf ) VIA "ADSCDX" ALIAS _STG SHARED NEW
   FOR EACH aRec IN aData
      _STG->( DbAppend() )
      _STG->NAME := aRec[ 1 ]
      _STG->CITY := aRec[ 2 ]
      _STG->AGE  := aRec[ 3 ]
   NEXT
   _STG->( DbCommit() )
   _STG->( DbCloseArea() )
   RETURN nil

//----------------------------------------------------------------------------//
