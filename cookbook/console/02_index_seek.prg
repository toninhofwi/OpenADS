/*
 * 02_index_seek.prg
 * ------------------------------------------------------------------
 * COOKBOOK / console track -- pure Harbour against OpenADS, NO ORM.
 *
 * Level: INTERMEDIATE.
 *
 * What it shows:
 *   - Several index tags in one .cdx (a numeric one and a text one).
 *   - Switching the active order with OrdSetFocus.
 *   - Exact dbSeek (key must match) vs. soft seek (nearest >= key).
 *   - Walking a range between two keys.
 *
 * An index lets the engine jump straight to a key and walk records in
 * key order, instead of scanning the whole table. You can keep several
 * tags in the same .cdx and switch between them at will.
 *
 * Data: an invented product catalog (code, name, price).
 *
 * Build & run: see this folder's README.md / build.cmd.
 * ------------------------------------------------------------------
 */

#include "ads.ch"
#include "rddsys.ch"
#include "set.ch"

REQUEST ADS, ADSCDX, ADSNTX

PROCEDURE Main()

   LOCAL cDir := hb_DirTemp() + "oa_cookbook_02"
   LOCAL cDbf := cDir + hb_ps() + "catalog.dbf"
   LOCAL aData, aRow

   ? "OpenADS cookbook -- 02 index & seek (pure Harbour, local)"
   ?

   AdsSetServerType( ADS_LOCAL_SERVER )
   AdsSetFileType( ADS_CDX )
   RddSetDefault( "ADSCDX" )
   hb_DirCreate( cDir )

   IF ! AdsConnect( cDir )
      ? "AdsConnect failed, DosError =", DosError()
      ErrorLevel( 1 )
      QUIT
   ENDIF

   IF File( cDbf )
      FErase( cDbf )
      FErase( hb_FNameExtSet( cDbf, ".cdx" ) )
   ENDIF

   DbCreate( cDbf, { ;
       { "CODE",  "N",  5, 0 }, ;
       { "NAME",  "C", 24, 0 }, ;
       { "PRICE", "N",  9, 2 } }, "ADSCDX" )

   USE ( cDbf ) VIA "ADSCDX" NEW EXCLUSIVE

   /* Two tags in the SAME catalog.cdx file:
    *   BY_CODE  -- numeric key, natural order
    *   BY_NAME  -- text key, case-insensitive via UPPER()                */
   INDEX ON FIELD->CODE          TAG BY_CODE
   INDEX ON UPPER( FIELD->NAME ) TAG BY_NAME

   aData := { ;
       { 1050, "Stapler",       12.90 }, ;
       { 1010, "Notebook",       4.50 }, ;
       { 1099, "Highlighter",    2.25 }, ;
       { 1042, "Desk lamp",     34.00 }, ;
       { 1003, "Paper clips",    1.10 } }

   FOR EACH aRow IN aData
      dbAppend()
      FIELD->CODE  := aRow[ 1 ]
      FIELD->NAME  := aRow[ 2 ]
      FIELD->PRICE := aRow[ 3 ]
   NEXT
   dbCommit()

   /* ---- walk in numeric CODE order ----------------------------- */
   ? "By CODE (ascending):"
   OrdSetFocus( "BY_CODE" )
   dbGoTop()
   DO WHILE ! Eof()
      ? "  " + Str( FIELD->CODE, 5 ) + "  " + PadR( FIELD->NAME, 16 ) + ;
        Str( FIELD->PRICE, 8, 2 )
      dbSkip()
   ENDDO
   ?

   /* ---- walk in text NAME order -------------------------------- */
   ? "By NAME (A->Z, case-insensitive):"
   OrdSetFocus( "BY_NAME" )
   dbGoTop()
   DO WHILE ! Eof()
      ? "  " + PadR( FIELD->NAME, 16 ) + " (code " + LTrim( Str( FIELD->CODE ) ) + ")"
      dbSkip()
   ENDDO
   ?

   /* ---- exact seek on the numeric key -------------------------- */
   OrdSetFocus( "BY_CODE" )
   dbSeek( 1042 )
   ? "Exact seek CODE=1042:", ;
     iif( Found(), AllTrim( FIELD->NAME ), "not found" )

   /* A code that does not exist -> exact seek fails. */
   dbSeek( 1041 )
   ? "Exact seek CODE=1041:", iif( Found(), AllTrim( FIELD->NAME ), "not found" )

   /* ---- soft seek: land on the nearest key >= the one asked ----- */
   /* SET SOFTSEEK ON makes a failed exact match position on the next
    * higher key instead of EOF, so you can do "first product from
    * code 1041 onward". */
   SET SOFTSEEK ON
   dbSeek( 1041 )
   ? "Soft seek CODE>=1041 lands on:", ;
     LTrim( Str( FIELD->CODE ) ) + " " + AllTrim( FIELD->NAME )
   SET SOFTSEEK OFF
   ?

   /* ---- range walk: every product with CODE in [1010..1050] ----- */
   ? "Range CODE 1010..1050:"
   OrdSetFocus( "BY_CODE" )
   SET SOFTSEEK ON
   dbSeek( 1010 )                      // first key >= 1010
   SET SOFTSEEK OFF
   DO WHILE ! Eof() .AND. FIELD->CODE <= 1050
      ? "  " + Str( FIELD->CODE, 5 ) + "  " + AllTrim( FIELD->NAME )
      dbSkip()
   ENDDO

   dbCloseArea()
   AdsDisconnect()

   ? ""
   ? "Done."
   RETURN
