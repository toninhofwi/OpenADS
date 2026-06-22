/*
 * 06_adt.prg
 * ------------------------------------------------------------------
 * COOKBOOK / console track -- pure Harbour against OpenADS, NO ORM.
 *
 * Level: INTERMEDIATE.
 *
 * What it shows, end to end:
 *   1. Select the engine's NATIVE table format, ADT, instead of DBF.
 *   2. Create a table whose fields span several types -- numeric with
 *      decimals, character, date, logical -- and append invented rows.
 *   3. Read the rows back and confirm every value (and its Harbour
 *      type) round-trips exactly.
 *
 * DBF (examples 01-05) stores everything as text in a fixed-width
 * record; ADT is the engine's own binary format, which keeps each
 * field in a typed slot. From the program's side the API is identical
 * -- the ONLY change is `AdsSetFileType( ADS_ADT )` and the `ADSADT`
 * RDD -- so the same xBase verbs you already know just work. That is
 * the takeaway: picking the table format is a one-line decision.
 *
 * Index files: an ADT table indexes into an `.adi` (vs DBF's `.cdx`);
 * this example sticks to a record-order walk so it stays focused on
 * the format itself (index ordering is covered in 01/02).
 *
 * Data is 100% invented (a tiny product list). No real-world data.
 *
 * Build & run: see this folder's README.md, or run build.cmd.
 * ------------------------------------------------------------------
 */

#include "ads.ch"
#include "rddsys.ch"

REQUEST ADS, ADSADT, ADSCDX, ADSNTX

PROCEDURE Main()

   LOCAL cDir := hb_DirTemp() + "oa_cookbook_06"
   LOCAL cAdt := cDir + hb_ps() + "products.adt"
   LOCAL aData, aRow

   ? "OpenADS cookbook -- 06 ADT native table type (pure Harbour, local)"
   ? "Engine version reported by the DLL:", AdsVersion()
   ?

   /* ---- 1. local engine, but ask for the ADT file type --------- */
   AdsSetServerType( ADS_LOCAL_SERVER )
   AdsSetFileType( ADS_ADT )          // <-- the one line that picks ADT
   RddSetDefault( "ADSADT" )
   hb_DirCreate( cDir )

   IF ! AdsConnect( cDir )
      ? "AdsConnect failed, DosError =", DosError()
      ErrorLevel( 1 )
      QUIT
   ENDIF

   IF File( cAdt )
      FErase( cAdt )
      FErase( hb_FNameExtSet( cAdt, ".adi" ) )   // ADT index extension
   ENDIF

   /* ---- 2. create a multi-type table + append invented rows ---- */
   /*   N = numeric (with decimals), C = character, D = date, L = logical */
   DbCreate( cAdt, { ;
       { "CODE",   "N",  6, 0 }, ;
       { "NAME",   "C", 24, 0 }, ;
       { "PRICE",  "N", 10, 2 }, ;
       { "RELEASE","D",  8, 0 }, ;
       { "ACTIVE", "L",  1, 0 } }, "ADSADT" )

   USE ( cAdt ) VIA "ADSADT" NEW EXCLUSIVE

   aData := { ;
       { 100, "Cadeira Ergonomica", 749.90, hb_SToD( "20240310" ), .T. }, ;
       { 101, "Mesa Ajustavel",    1299.00, hb_SToD( "20240722" ), .T. }, ;
       { 102, "Luminaria LED",       89.50, hb_SToD( "20231201" ), .F. } }

   FOR EACH aRow IN aData
      dbAppend()
      FIELD->CODE    := aRow[ 1 ]
      FIELD->NAME    := aRow[ 2 ]
      FIELD->PRICE   := aRow[ 3 ]
      FIELD->RELEASE := aRow[ 4 ]
      FIELD->ACTIVE  := aRow[ 5 ]
   NEXT
   dbCommit()

   ? "Rows after append:", LastRec(), "| files on disk:"
   ? "   products.adt exists:", File( cAdt )
   ?

   /* ---- 3. read back and confirm each value + its type --------- */
   ? "Stored rows (value | Harbour type):"
   dbGoTop()
   DO WHILE ! Eof()
      ? "  code=" + Str( FIELD->CODE, 4 ) + " (" + ValType( FIELD->CODE ) + ")" + ;
        "  " + PadR( AllTrim( FIELD->NAME ), 20 ) + ;
        " price=" + PadL( LTrim( Str( FIELD->PRICE, 10, 2 ) ), 8 ) + " (" + ValType( FIELD->PRICE ) + ")" + ;
        " release=" + DToC( FIELD->RELEASE ) + " (" + ValType( FIELD->RELEASE ) + ")" + ;
        " active=" + iif( FIELD->ACTIVE, "yes", "no " ) + " (" + ValType( FIELD->ACTIVE ) + ")"
      dbSkip()
   ENDDO
   ?

   /* a quick spot check that a typed value compares as itself */
   dbGoTop()
   ? "Spot check: first row PRICE == 749.90 ?", ( FIELD->PRICE == 749.90 )
   ? "Spot check: third row ACTIVE is .F. ?", ( dbGoto( 3 ), FIELD->ACTIVE == .F. )

   /* ---- 4. clean up -------------------------------------------- */
   dbCloseArea()
   AdsDisconnect()

   ? "Done."
   RETURN
