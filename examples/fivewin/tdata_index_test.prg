/*
 * tdata_index_test.prg
 * ------------------------------------------------------------------
 * FiveWin TDatabase (extended) regression over OpenADS.
 *
 * Reproduces the community report: "multiple indexes per file, select
 * the active one per task -- you can't do that with OpenADS." The user
 * drives data through FiveWin's TDatabase OBJECT (database classes),
 * not raw work-area verbs, so this test does the same.
 *
 * What it exercises, head-less (/auto), through the TDatabase object:
 *   1. One DBF, ONE structural .cdx carrying THREE tags
 *      (TCODE on CODE, TNAME on NAME, TPRICE on PRICE).
 *   2. oDb:SetOrder( <tag> ) to make each index the ACTIVE order,
 *      then GoTop and assert the first row is the one that index
 *      sorts first -- i.e. switching the active order actually
 *      re-navigates the file.
 *   3. oDb:Seek( <value> ) under each active order and assert it lands
 *      on the right record (numeric key for TCODE/TPRICE, char TNAME).
 *   4. oDb:IndexOrder() / OrdKeyCount() sanity.
 *
 * Output is teed to <temp>\openads_fwh_idx\result.log AND the process
 * exit code is 0 (all pass) / 1 (first failure), so it works as a
 * head-less smoke test under /subsystem:windows. Data is invented.
 * Build: build_msvc64.cmd <openads-release-dir> tdata_index_test
 * ------------------------------------------------------------------
 */

#include "FiveWin.ch"

REQUEST ADSCDX, ADSNTX
REQUEST ADSKEYNO, ADSKEYCOUNT, ADSGETRELKEYPOS, ADSSETRELKEYPOS

#define ADS_LOCAL_SERVER 1
#define ADS_CDX          2

STATIC nFails := 0
STATIC cOut   := ""
STATIC cLog   := ""

//----------------------------------------------------------------------------//

FUNCTION Main( cMode )

   LOCAL cDir := TempFolder() + "\openads_fwh_idx"
   LOCAL cDbf := cDir + "\stock.dbf"
   LOCAL oDb, nRet

   HB_SYMBOL_UNUSED( cMode )

   cLog := cDir + "\result.log"
   StageDbf( cDir, cDbf )

   AdsSetServerType( ADS_LOCAL_SERVER )
   AdsSetFileType( ADS_CDX )
   RddSetDefault( "ADSCDX" )

   // --- control: same data/walk through Harbour's NATIVE DBFCDX (not
   //     OpenADS) -- proves the test logic expects ascending correctly ---
   NativeProbe( cDir )

   // --- raw work-area probe (no TDatabase): isolate engine behavior ---
   RawProbe( cDbf )

   // --- open through the FiveWin TDatabase OBJECT (the user's path) ---
   oDb := TDatabase():New( , cDbf, "ADSCDX", .T. /*shared*/ )
   IF ! oDb:Use()
      Echo( "FAIL: TDatabase:Use() on " + cDbf )
      RETURN Done( 1 )
   ENDIF

   Echo( "OpenADS: " + AdsVersion() )
   Echo( "tags in bag: " + LTrim( Str( oDb:OrdCount() ) ) )   // expect 3

   // ============================================================
   // 1. Active-order switching: each SetOrder re-navigates GoTop.
   //    sorts-first:  TCODE->100  TNAME->"Alpha"(100)  TPRICE->10.00(300)
   // ============================================================
   CheckOrderTop( oDb, "TCODE",  "CODE",       100 )
   CheckOrderTop( oDb, "TNAME",  "NAME",  "Alpha"   )
   CheckOrderTop( oDb, "TPRICE", "PRICE",  10.00    )

   // ============================================================
   // 2. Seek under each active order lands on the right record.
   // ============================================================
   oDb:SetOrder( "TCODE" )
   IF oDb:Seek( 200 ) .AND. AllTrim( oDb:NAME ) == "Bravo"
      Echo( "PASS: seek TCODE 200 -> Bravo" )
   ELSE
      Fail( "seek TCODE 200 -> '" + AllTrim( oDb:NAME ) + "' (expected Bravo)" )
   ENDIF

   oDb:SetOrder( "TNAME" )
   IF oDb:Seek( "Charlie" ) .AND. oDb:CODE == 300
      Echo( "PASS: seek TNAME Charlie -> 300" )
   ELSE
      Fail( "seek TNAME Charlie -> code " + LTrim( Str( oDb:CODE ) ) + " (expected 300)" )
   ENDIF

   oDb:SetOrder( "TPRICE" )
   IF oDb:Seek( 30.00 ) .AND. oDb:CODE == 100
      Echo( "PASS: seek TPRICE 30.00 -> 100" )
   ELSE
      Fail( "seek TPRICE 30.00 -> code " + LTrim( Str( oDb:CODE ) ) + " (expected 100)" )
   ENDIF

   // ============================================================
   // 3. IndexOrder()/OrdKeyCount sanity through the active order.
   // ============================================================
   oDb:SetOrder( "TNAME" )
   IF oDb:IndexOrder() == 2 .AND. oDb:OrdKeyCount() == 3
      Echo( "PASS: TNAME is order #2, 3 keys" )
   ELSE
      Fail( "IndexOrder=" + LTrim( Str( oDb:IndexOrder() ) ) + ;
            " keyCount=" + LTrim( Str( oDb:OrdKeyCount() ) ) + " (expected 2 / 3)" )
   ENDIF

   oDb:Close()
   AdsDisconnect()

   IF nFails == 0
      Echo( "OK: TDatabase multi-index over OpenADS -- all checks passed" )
      nRet := 0
   ELSE
      Echo( "FAILED: " + LTrim( Str( nFails ) ) + " check(s)" )
      nRet := 1
   ENDIF
   RETURN Done( nRet )

//----------------------------------------------------------------------------//
// Make <tag> the active order, GoTop, assert <field> of the first row.

STATIC FUNCTION CheckOrderTop( oDb, cTag, cField, uExpect )

   LOCAL uGot

   oDb:SetOrder( cTag )
   oDb:GoTop()
   uGot := oDb:FieldGet( oDb:FieldPos( cField ) )

   IF ValType( uGot ) == "C"
      uGot := AllTrim( uGot )
   ENDIF

   IF uGot == uExpect
      Echo( "PASS: order " + cTag + " GoTop " + cField + " = " + cValToChar( uExpect ) )
   ELSE
      Fail( "order " + cTag + " GoTop " + cField + " = " + cValToChar( uGot ) + ;
            " (expected " + cValToChar( uExpect ) + ")" )
   ENDIF
   RETURN nil

//----------------------------------------------------------------------------//
// Raw work-area probe: USE + OrdSetFocus + GoTop, no TDatabase in the path.
// Tells us whether any GoTop/order issue is in the engine or in the class.

// Native Harbour DBFCDX control: stage a separate DBF with the engine's
// own CDX RDD and walk it. Expected ascending 100 200 300 -- if this
// passes and the ADSCDX walk reverses, the bug is OpenADS-side.

STATIC FUNCTION NativeProbe( cDir )

   LOCAL cDbf := cDir + "\native.dbf"
   LOCAL cCdx := cDir + "\native.cdx"
   LOCAL cSeq := ""

   IF File( cDbf ) ; FErase( cDbf ) ; FErase( cCdx ) ; ENDIF

   DbCreate( cDbf, { { "CODE", "N", 6, 0 }, { "NAME", "C", 20, 0 } }, "DBFCDX" )
   USE ( cDbf ) VIA "DBFCDX" ALIAS NTV SHARED NEW
   NTV->( DbAppend() ) ; NTV->CODE := 300 ; NTV->NAME := "Charlie"
   NTV->( DbAppend() ) ; NTV->CODE := 100 ; NTV->NAME := "Alpha"
   NTV->( DbAppend() ) ; NTV->CODE := 200 ; NTV->NAME := "Bravo"
   NTV->( DbCommit() )
   INDEX ON NTV->CODE TAG TCODE TO ( cCdx )

   OrdSetFocus( "TCODE" )
   NTV->( DbGoTop() )
   DO WHILE ! NTV->( Eof() )
      cSeq += LTrim( Str( NTV->CODE ) ) + " "
      NTV->( DbSkip() )
   ENDDO
   Echo( "native DBFCDX TCODE walk = [" + AllTrim( cSeq ) + "] (expect 100 200 300)" )

   NTV->( DbCloseArea() )
   RETURN nil

//----------------------------------------------------------------------------//
// Open the OpenADS-written stock.dbf/.cdx with Harbour's NATIVE DBFCDX
// reader and walk TCODE. Decides write-side vs read-side for the bug.

STATIC FUNCTION CrossReadProbe( cDbf )

   LOCAL cSeq := ""

   USE ( cDbf ) VIA "DBFCDX" ALIAS XRD SHARED NEW
   IF Select( "XRD" ) == 0
      Echo( "xread: native DBFCDX could not open OpenADS .cdx" ) ; RETURN nil
   ENDIF
   OrdSetFocus( "TCODE" )
   XRD->( DbGoTop() )
   DO WHILE ! XRD->( Eof() )
      cSeq += LTrim( Str( XRD->CODE ) ) + " "
      XRD->( DbSkip() )
   ENDDO
   Echo( "xread: native DBFCDX reads OpenADS cdx, TCODE walk = [" + ;
         AllTrim( cSeq ) + "] (ascending => write OK / read bug)" )
   XRD->( DbCloseArea() )
   RETURN nil

//----------------------------------------------------------------------------//

STATIC FUNCTION RawProbe( cDbf )

   LOCAL cSeq := ""

   USE ( cDbf ) VIA "ADSCDX" ALIAS RAW SHARED NEW
   IF Select( "RAW" ) == 0
      Echo( "raw: USE failed" ) ; RETURN nil
   ENDIF

   OrdSetFocus( "TCODE" )
   RAW->( DbGoTop() )
   Echo( "raw: OrdSetFocus(TCODE)  ordName=" + RAW->( OrdName() ) + ;
         "  GoTop CODE=" + LTrim( Str( RAW->CODE ) ) + " (expect 100)" )

   OrdSetFocus( "TPRICE" )
   RAW->( DbGoTop() )
   Echo( "raw: OrdSetFocus(TPRICE) ordName=" + RAW->( OrdName() ) + ;
         "  GoTop PRICE=" + LTrim( Str( RAW->PRICE ) ) + " (expect 10.00)" )

   OrdSetFocus( "TCODE" )
   RAW->( DbGoBottom() )
   Echo( "raw: OrdSetFocus(TCODE)  GoBottom CODE=" + LTrim( Str( RAW->CODE ) ) + " (expect 300)" )

   // full forward walk under TCODE: GoTop then Skip to end.
   // expected key order: 100, 200, 300
   OrdSetFocus( "TCODE" )
   RAW->( DbGoTop() )
   DO WHILE ! RAW->( Eof() )
      cSeq += LTrim( Str( RAW->CODE ) ) + " "
      RAW->( DbSkip() )
   ENDDO
   Echo( "raw: TCODE forward walk = [" + AllTrim( cSeq ) + "] (expect 100 200 300)" )

   RAW->( DbCloseArea() )
   RETURN nil

//----------------------------------------------------------------------------//
// Echo tees to console (?) and to an in-memory buffer flushed at exit.

STATIC FUNCTION Echo( cMsg )
   QOut( cMsg )
   cOut += cMsg + hb_eol()
   // Flush incrementally so a mid-run hang still leaves a partial log.
   IF ! Empty( cLog )
      MemoWrit( cLog, cOut )
   ENDIF
   RETURN nil

STATIC FUNCTION Fail( cMsg )
   Echo( "FAIL: " + cMsg )
   nFails++
   RETURN nil

STATIC FUNCTION Done( nRet )
   IF ! Empty( cLog )
      MemoWrit( cLog, cOut )
   ENDIF
   RETURN nRet

//----------------------------------------------------------------------------//

STATIC FUNCTION TempFolder()
   LOCAL c := GetEnv( "TEMP" )
   IF Empty( c ) ; c := GetEnv( "TMP" ) ; ENDIF
   IF Empty( c ) ; c := "C:\Temp" ; ENDIF
   RETURN c

STATIC FUNCTION StageDbf( cDir, cDbf )

   LOCAL aData, aRec
   LOCAL cCdx := StrTran( cDbf, ".dbf", ".cdx" )

   IF ! lIsDir( cDir ) ; MakeDir( cDir ) ; ENDIF
   IF File( cDbf ) ; FErase( cDbf ) ; FErase( cCdx ) ; ENDIF

   DbCreate( cDbf, { ;
       { "CODE",  "N",  6, 0 }, ;
       { "NAME",  "C", 20, 0 }, ;
       { "PRICE", "N", 10, 2 } }, "ADSCDX" )

   // deliberately unsorted so each order produces a different GoTop
   aData := { { 300, "Charlie", 10.00 }, ;
              { 100, "Alpha",   30.00 }, ;
              { 200, "Bravo",   20.00 } }

   USE ( cDbf ) VIA "ADSCDX" ALIAS _STG SHARED NEW
   FOR EACH aRec IN aData
      _STG->( DbAppend() )
      _STG->CODE  := aRec[ 1 ]
      _STG->NAME  := aRec[ 2 ]
      _STG->PRICE := aRec[ 3 ]
   NEXT
   _STG->( DbCommit() )

   // three tags into the one structural .cdx (auto-opens with the table)
   INDEX ON _STG->CODE  TAG TCODE  TO ( cCdx )
   INDEX ON _STG->NAME  TAG TNAME  TO ( cCdx )
   INDEX ON _STG->PRICE TAG TPRICE TO ( cCdx )

   _STG->( DbCloseArea() )
   RETURN nil

//----------------------------------------------------------------------------//
