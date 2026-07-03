// mini_xbrowse.prg — the smallest possible FiveWin xBrowse over a DBF.
// Baseline for debugging the ADS variant: run it plain (DBFCDX) — if
// the rows show, the FWH/xBrowse plumbing is fine; then run with /ads
// (opens the same file VIA "ADSCDX" → Harbour rddads → OpenADS' DLL)
// and see if it still shows.
//
// Build:  build_msvc64.cmd  <openads-ace64-dir>  mini_xbrowse
// Run:    mini_xbrowse.exe          (plain DBFCDX)
//         mini_xbrowse.exe /ads     (via ADSCDX → OpenADS' ace64.dll)

#include "FiveWin.ch"
#include "xbrowse.ch"
#include "dbinfo.ch"

REQUEST DBFCDX
REQUEST ADSCDX
// FWH's xBrowse SetRDD() builds its bKeyNo / bKeyCount codeblocks for an
// ADS workarea as MACRO strings that call AdsKeyNo() / AdsKeyCount()
// (and AdsGetRelKeyPos/AdsSetRelKeyPos for >200-row tables). Because
// those names appear only inside the macro text, the linker dead-strips
// them — at run time the browse can't get its row position and shows
// blank. REQUEST keeps them in the image.
REQUEST ADSKEYNO, ADSKEYCOUNT, ADSGETRELKEYPOS, ADSSETRELKEYPOS

FUNCTION Main( cMode )

   LOCAL oWnd, oBrw
   LOCAL lAds := ( ValType( cMode ) == "C" .AND. "/ads" $ Lower( cMode ) )
   LOCAL cDir := TempFolder() + "\openads_fwh_demo"
   LOCAL cDbf := cDir + "\mini.dbf"

   StageDbf( cDir, cDbf )

   IF lAds
      AdsSetServerType( 1 )       // ADS_LOCAL_SERVER
      AdsSetFileType( 2 )         // ADS_CDX
      USE ( cDbf ) VIA "ADSCDX" ALIAS MINI SHARED NEW
   ELSE
      USE ( cDbf ) ALIAS MINI SHARED NEW
   ENDIF
   IF Select( "MINI" ) == 0
      MsgStop( "USE failed" + iif( lAds, " (ADSCDX)", "" ) )
      RETURN 1
   ENDIF
   MINI->( DbGoTop() )

   // DEBUG: probe the exact ops xBrowse relies on to paint rows.
   // DiagProbe()

   DEFINE WINDOW oWnd FROM 2, 4 TO 24, 70 ;
      TITLE iif( lAds, "ADSCDX (OpenADS) — " + AdsVersion(), "DBFCDX (baseline)" ) + ;
            "  |  " + AllTrim( Str( MINI->( RecCount() ) ) ) + " records"

   @ 0, 0 XBROWSE oBrw OF oWnd ;
      ALIAS "MINI" AUTOCOLS AUTOSORT ;
      CELL LINES NOBORDER

   oBrw:CreateFromCode()
   oWnd:oClient := oBrw

   ACTIVATE WINDOW oWnd ;
      ON INIT ( oBrw:SetFocus(), oBrw:GoTop(), oBrw:Refresh() )

   MINI->( DbCloseArea() )
   RETURN 0

//----------------------------------------------------------------------------//

// Dumps what xBrowse actually computed for this workarea.
STATIC FUNCTION DumpBrw( oBrw )
   LOCAL c := ""
   c += "nDataType=" + cValToChar( oBrw:nDataType ) + "  (DATATYPE_RDD=" + cValToChar( DATATYPE_RDD ) + ")" + CRLF
   c += "cAlias=[" + cValToChar( oBrw:cAlias ) + "]  RowCount(visible)=" + cValToChar( oBrw:RowCount() ) + CRLF
   c += "Len(aCols)=" + cValToChar( Len( oBrw:aCols ) ) + CRLF
   c += "nLen=" + cValToChar( oBrw:nLen ) + CRLF
   c += "bKeyNo type=" + ValType( oBrw:bKeyNo ) + "  -> " + ;
        cValToChar( iif( ValType( oBrw:bKeyNo ) == "B", Eval( oBrw:bKeyNo, nil, oBrw ), "?" ) ) + CRLF
   c += "bKeyCount type=" + ValType( oBrw:bKeyCount ) + "  -> " + ;
        cValToChar( iif( ValType( oBrw:bKeyCount ) == "B", Eval( oBrw:bKeyCount ), "?" ) ) + CRLF
   c += "bGoTop -> " + cValToChar( iif( ValType( oBrw:bGoTop ) == "B", ( Eval( oBrw:bGoTop ), "(ran)" ), "?" ) ) + CRLF
   c += "bEof  -> " + cValToChar( iif( ValType( oBrw:bEof ) == "B", Eval( oBrw:bEof ), "?" ) ) + CRLF
   c += "bBof  -> " + cValToChar( iif( ValType( oBrw:bBof ) == "B", Eval( oBrw:bBof ), "?" ) ) + CRLF
   IF Len( oBrw:aCols ) >= 1
      c += "col1 cHeader=[" + cValToChar( oBrw:aCols[1]:cHeader ) + "] nWidth=" + cValToChar( oBrw:aCols[1]:nWidth ) + ;
           " bStrData->[" + cValToChar( iif( ValType( oBrw:aCols[1]:bStrData ) == "B", Eval( oBrw:aCols[1]:bStrData, oBrw:aCols[1] ), "?" ) ) + "]" + CRLF
   ENDIF
   MsgInfo( c, "xBrowse internal state (" + ( oBrw:cAlias )->( RddName() ) + ")" )
   RETURN nil

// Reports what the workarea returns for the ops xBrowse uses per row.
STATIC FUNCTION DiagProbe()
   LOCAL cR := ""
   LOCAL i, aF

   cR += "RddName=" + MINI->( RddName() ) + "  FCount=" + Str( MINI->( FCount() ), 3 ) + CRLF
   cR += "RecCount=" + Str( MINI->( RecCount() ), 4 ) + "  LastRec=" + Str( MINI->( LastRec() ), 4 ) + CRLF
   cR += "OrdNumber=" + Str( MINI->( OrdNumber() ), 3 ) + ;
         "  IndexOrd=" + Str( MINI->( IndexOrd() ), 3 ) + ;
         "  OrdName=[" + MINI->( OrdName() ) + "]" + CRLF
   cR += ">>> OrdKeyCount=" + Str( MINI->( OrdKeyCount() ), 5 ) + ;
         "  OrdKeyNo=" + Str( MINI->( OrdKeyNo() ), 5 ) + " <<<" + CRLF
   cR += REPLICATE( "-", 40 ) + CRLF + "DbStruct():" + CRLF
   FOR EACH aF IN MINI->( DbStruct() )
      cR += "  [" + aF[ 1 ] + "] type=" + aF[ 2 ] + " len=" + Str( aF[ 3 ], 4 ) + " dec=" + Str( aF[ 4 ], 3 ) + CRLF
   NEXT
   cR += REPLICATE( "-", 40 ) + CRLF
   FOR i := 1 TO 3
      MINI->( DbGoto( i ) )
      cR += "rec " + Str(i,1) + ": NAME=[" + AllTrim( MINI->NAME ) + "] CITY=[" + AllTrim( MINI->CITY ) + "] AGE=" + Str( MINI->AGE, 4 ) + CRLF
   NEXT
   MINI->( DbGoTop() )
   MsgInfo( cR, "xBrowse probe (" + MINI->( RddName() ) + ")" )
   RETURN nil

STATIC FUNCTION TempFolder()
   LOCAL c := GetEnv( "TEMP" )
   IF Empty( c ) ; c := GetEnv( "TMP" ) ; ENDIF
   IF Empty( c ) ; c := "C:\Temp" ; ENDIF
   RETURN c

STATIC FUNCTION StageDbf( cDir, cDbf )
   LOCAL aRec
   IF !lIsDir( cDir ) ; MakeDir( cDir ) ; ENDIF
   IF File( cDbf ) ; RETURN nil ; ENDIF
   DbCreate( cDbf, { { "NAME", "C", 20, 0 }, { "CITY", "C", 20, 0 }, { "AGE", "N", 3, 0 } } )
   USE ( cDbf ) ALIAS _S SHARED NEW
   FOR EACH aRec IN { { "Alice", "Madrid", 30 }, { "Bob", "Barcelona", 41 }, ;
                      { "Charlie", "Valencia", 25 }, { "Diana", "Sevilla", 37 }, ;
                      { "Edward", "Bilbao", 52 } }
      _S->( DbAppend() )
      _S->NAME := aRec[ 1 ] ; _S->CITY := aRec[ 2 ] ; _S->AGE := aRec[ 3 ]
   NEXT
   _S->( DbCommit() )
   _S->( DbCloseArea() )
   RETURN nil
