/*
 * crud_browse.prg
 * ------------------------------------------------------------------
 * COOKBOOK / FiveWin track -- a GUI over OpenADS.
 *
 * Level: INTERMEDIATE.
 *
 * What it shows, end to end:
 *   1. Open an OpenADS table through the stock `rddads` RDD (same as
 *      the console track -- the engine does not change, only the UI).
 *   2. Put FiveWin's xBrowse over the work area (a live grid).
 *   3. A small CRUD form: New / Edit / Delete rows through a FWH dialog
 *      with GET fields, each operation landing on the engine via the
 *      ordinary xBase verbs (dbAppend / @..GET..replace / dbDelete).
 *
 * The CRUD logic is factored into AddRow / EditRow / DelRow so it can
 * be exercised WITHOUT a window: run with `/auto` on the command line
 * and the program drives those same functions head-less and verifies
 * the results (handy as a build smoke test). With no argument it opens
 * the interactive browser + buttons.
 *
 * Data is 100% invented (a tiny product list). No real-world data.
 *
 * Build: build.cmd (FiveWin + Harbour + MSVC 64-bit + rddads + the
 * OpenADS engine DLL). Put the OpenADS DLL next to the .exe / on PATH.
 * ------------------------------------------------------------------
 */

#include "FiveWin.ch"
#include "xbrowse.ch"

REQUEST ADSCDX, ADSNTX
// FWH's xBrowse builds its row-position codeblocks for an ADS work area
// as MACRO strings referencing AdsKeyNo()/AdsKeyCount() (and the
// RelKeyPos pair for larger tables). Those names live only in macro text,
// so the linker would dead-strip them and the grid would render blank.
// REQUEST keeps them in the image (needed for any FWH + rddads + ADS app).
REQUEST ADSKEYNO, ADSKEYCOUNT, ADSGETRELKEYPOS, ADSSETRELKEYPOS

#define ADS_LOCAL_SERVER 1
#define ADS_CDX          2

//----------------------------------------------------------------------------//

FUNCTION Main( cMode )

   LOCAL lAuto := ( ValType( cMode ) == "C" .AND. Lower( AllTrim( cMode ) ) == "/auto" )
   LOCAL cDir  := TempFolder() + "\openads_fwh_crud"
   LOCAL cDbf  := cDir + "\products.dbf"

   StageDbf( cDir, cDbf )

   AdsSetServerType( ADS_LOCAL_SERVER )
   AdsSetFileType( ADS_CDX )
   RddSetDefault( "ADSCDX" )

   USE ( cDbf ) VIA "ADSCDX" ALIAS PROD SHARED NEW
   IF Select( "PROD" ) == 0
      ? "FAIL: USE products VIA ADSCDX"
      RETURN 1
   ENDIF
   INDEX ON PROD->CODE TAG CODE
   OrdSetFocus( "CODE" )
   PROD->( DbGoTop() )

   IF lAuto
      RETURN AutoCrud()           // head-less: exercise CRUD, no GUI
   ENDIF

   ShowBrowse()                   // interactive grid + New/Edit/Delete

   PROD->( DbCloseArea() )
   AdsDisconnect()
   RETURN 0

//----------------------------------------------------------------------------//
// CRUD primitives -- the GUI and the /auto smoke both call THESE, so the
// behavior under test is identical with or without a window.

STATIC FUNCTION AddRow( nCode, cName, nPrice, lActive )
   PROD->( DbAppend() )
   PROD->CODE   := nCode
   PROD->NAME   := cName
   PROD->PRICE  := nPrice
   PROD->ACTIVE := lActive
   PROD->( DbCommit() )
   RETURN PROD->( RecNo() )

STATIC FUNCTION EditRow( nRec, cName, nPrice, lActive )
   PROD->( DbGoto( nRec ) )
   IF PROD->( Eof() ) ; RETURN .F. ; ENDIF
   PROD->NAME   := cName
   PROD->PRICE  := nPrice
   PROD->ACTIVE := lActive
   PROD->( DbCommit() )
   RETURN .T.

STATIC FUNCTION DelRow( nRec )
   PROD->( DbGoto( nRec ) )
   IF PROD->( Eof() ) ; RETURN .F. ; ENDIF
   PROD->( DbDelete() )           // mark deleted (xBase convention)
   PROD->( DbCommit() )
   RETURN .T.

STATIC FUNCTION VisibleCount()
   LOCAL n := 0
   LOCAL lOld := Set( _SET_DELETED, .T. )
   PROD->( DbGoTop() )
   DO WHILE ! PROD->( Eof() )
      n++
      PROD->( DbSkip() )
   ENDDO
   Set( _SET_DELETED, lOld )
   RETURN n

//----------------------------------------------------------------------------//
// /auto: drive the CRUD primitives head-less and verify the outcomes.

STATIC FUNCTION AutoCrud()

   LOCAL nStart := VisibleCount()
   LOCAL nRec

   /* New */
   nRec := AddRow( 999, "Test Widget", 49.90, .T. )
   IF VisibleCount() != nStart + 1
      ? "FAIL: after AddRow visible =", VisibleCount() ; RETURN Bail()
   ENDIF

   /* Edit */
   IF ! EditRow( nRec, "Test Widget v2", 59.90, .T. )
      ? "FAIL: EditRow" ; RETURN Bail()
   ENDIF
   PROD->( DbGoto( nRec ) )
   IF AllTrim( PROD->NAME ) != "Test Widget v2" .OR. PROD->PRICE != 59.90
      ? "FAIL: edit not persisted ('" + AllTrim( PROD->NAME ) + "')" ; RETURN Bail()
   ENDIF

   /* Delete */
   IF ! DelRow( nRec )
      ? "FAIL: DelRow" ; RETURN Bail()
   ENDIF
   IF VisibleCount() != nStart
      ? "FAIL: after DelRow visible =", VisibleCount(), "expected", nStart ; RETURN Bail()
   ENDIF

   PROD->( DbCloseArea() )
   AdsDisconnect()
   ? "OK: FWH /auto -- CRUD on OpenADS (" + AdsVersion() + "): add/edit/delete verified"
   RETURN 0

STATIC FUNCTION Bail()
   PROD->( DbCloseArea() )
   AdsDisconnect()
   RETURN 1

//----------------------------------------------------------------------------//
// Interactive grid + a button bar wired to the CRUD dialog.

STATIC FUNCTION ShowBrowse()

   LOCAL oWnd, oBrw, oBar

   DbSelectArea( "PROD" )

   DEFINE WINDOW oWnd FROM 1, 1 TO 30, 110 ;
      TITLE "OpenADS -- FWH CRUD over ADS (" + AdsVersion() + ")"

   DEFINE BUTTONBAR oBar OF oWnd SIZE 90, 30 2007
   DEFINE BUTTON OF oBar PROMPT "New"    ACTION ( EditDialog( .T., oBrw ) )
   DEFINE BUTTON OF oBar PROMPT "Edit"   ACTION ( EditDialog( .F., oBrw ) )
   DEFINE BUTTON OF oBar PROMPT "Delete" ACTION ( DelCurrent( oBrw ) )

   @ 0, 0 XBROWSE oBrw OF oWnd ;
      ALIAS "PROD" AUTOCOLS ;
      CELL LINES NOBORDER FOOTERS

   oBrw:CreateFromCode()
   oWnd:oClient := oBrw

   ACTIVATE WINDOW oWnd ;
      ON INIT  ( oBrw:SetFocus(), oBrw:GoTop(), oBrw:Refresh() ) ;
      ON RESIZE ( oBrw:adjust(), oBrw:Refresh() )

   RETURN nil

//----------------------------------------------------------------------------//
// One dialog for both New and Edit. For New we collect into locals and
// AddRow; for Edit we pre-fill from the current record and EditRow.

STATIC FUNCTION EditDialog( lNew, oBrw )

   LOCAL oDlg
   LOCAL nCode   := iif( lNew, 0, PROD->CODE )
   LOCAL cName   := iif( lNew, Space( 24 ), PROD->NAME )
   LOCAL nPrice  := iif( lNew, 0, PROD->PRICE )
   LOCAL lActive := iif( lNew, .T., PROD->ACTIVE )
   LOCAL nRec    := PROD->( RecNo() )

   DEFINE DIALOG oDlg FROM 0, 0 TO 18, 50 ;
      TITLE iif( lNew, "New product", "Edit product" )

   @ 1.0, 2 SAY "Code:"   OF oDlg
   @ 1.0, 8 GET nCode     OF oDlg PICTURE "999999" WHEN lNew
   @ 2.2, 2 SAY "Name:"   OF oDlg
   @ 2.2, 8 GET cName     OF oDlg
   @ 3.4, 2 SAY "Price:"  OF oDlg
   @ 3.4, 8 GET nPrice    OF oDlg PICTURE "@E 999999.99"
   @ 4.6, 8 CHECKBOX lActive PROMPT "Active" OF oDlg

   @ 6.2, 8 BUTTON "&OK" OF oDlg ACTION oDlg:End() SIZE 40, 12
   @ 6.2, 26 BUTTON "&Cancel" OF oDlg ACTION ( nCode := -1, oDlg:End() ) SIZE 40, 12

   ACTIVATE DIALOG oDlg CENTERED

   IF nCode == -1 ; RETURN nil ; ENDIF       // cancelled

   IF lNew
      AddRow( nCode, cName, nPrice, lActive )
   ELSE
      EditRow( nRec, cName, nPrice, lActive )
   ENDIF
   oBrw:Refresh()
   RETURN nil

STATIC FUNCTION DelCurrent( oBrw )
   IF PROD->( Eof() ) ; RETURN nil ; ENDIF
   IF MsgYesNo( "Delete '" + AllTrim( PROD->NAME ) + "' ?" )
      DelRow( PROD->( RecNo() ) )
      oBrw:Refresh()
   ENDIF
   RETURN nil

//----------------------------------------------------------------------------//

STATIC FUNCTION TempFolder()
   LOCAL c := GetEnv( "TEMP" )
   IF Empty( c ) ; c := GetEnv( "TMP" ) ; ENDIF
   IF Empty( c ) ; c := "C:\Temp" ; ENDIF
   RETURN c

STATIC FUNCTION StageDbf( cDir, cDbf )

   LOCAL aData, aRec

   IF ! lIsDir( cDir ) ; MakeDir( cDir ) ; ENDIF
   IF File( cDbf ) ; FErase( cDbf ) ; FErase( StrTran( cDbf, ".dbf", ".cdx" ) ) ; ENDIF

   DbCreate( cDbf, { ;
       { "CODE",   "N",  6, 0 }, ;
       { "NAME",   "C", 24, 0 }, ;
       { "PRICE",  "N", 10, 2 }, ;
       { "ACTIVE", "L",  1, 0 } }, "ADSCDX" )

   aData := { { 100, "Cadeira Ergonomica", 749.90, .T. }, ;
              { 101, "Mesa Ajustavel",    1299.00, .T. }, ;
              { 102, "Luminaria LED",       89.50, .F. } }

   USE ( cDbf ) VIA "ADSCDX" ALIAS _STG SHARED NEW
   FOR EACH aRec IN aData
      _STG->( DbAppend() )
      _STG->CODE   := aRec[ 1 ]
      _STG->NAME   := aRec[ 2 ]
      _STG->PRICE  := aRec[ 3 ]
      _STG->ACTIVE := aRec[ 4 ]
   NEXT
   _STG->( DbCommit() )
   _STG->( DbCloseArea() )
   RETURN nil

//----------------------------------------------------------------------------//
