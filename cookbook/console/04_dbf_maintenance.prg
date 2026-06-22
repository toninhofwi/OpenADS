/*
 * 04_dbf_maintenance.prg
 * ------------------------------------------------------------------
 * COOKBOOK / console track -- pure Harbour against OpenADS, NO ORM.
 *
 * Level: INTERMEDIATE.
 *
 * Classic DBF housekeeping, all with stock xBase verbs:
 *   - DELETE  : mark a record deleted (a soft, reversible flag).
 *   - SET DELETED ON/OFF : hide or show deleted records.
 *   - RECALL  : un-delete (recover) a marked record.
 *   - PACK    : physically remove every deleted record.
 *   - UPPER() : case-insensitive matching in a filter/index key.
 *   - SET FILTER : show only the rows matching a condition.
 *   - OrdScopeSet : restrict an indexed walk to a key range (scope).
 *
 * A "deleted" DBF record is not gone -- it is flagged. It stays hidden
 * while SET DELETED is ON and can be brought back with RECALL, until
 * PACK makes the removal permanent. This is the safety net DBF gives
 * you over a hard DELETE.
 *
 * Data: an invented contacts table (id, name, city).
 *
 * Build & run: see this folder's README.md / build.cmd.
 * ------------------------------------------------------------------
 */

#include "ads.ch"
#include "rddsys.ch"
#include "set.ch"

REQUEST ADS, ADSCDX, ADSNTX

PROCEDURE Main()

   LOCAL cDir := hb_DirTemp() + "oa_cookbook_04"
   LOCAL cDbf := cDir + hb_ps() + "contacts.dbf"
   LOCAL aData, aRow

   ? "OpenADS cookbook -- 04 DBF maintenance (pure Harbour, local)"
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
       { "ID",   "N",  4, 0 }, ;
       { "NAME", "C", 16, 0 }, ;
       { "CITY", "C", 14, 0 } }, "ADSCDX" )

   USE ( cDbf ) VIA "ADSCDX" NEW EXCLUSIVE
   /* Case-insensitive name order via UPPER() in the key expression. */
   INDEX ON UPPER( FIELD->NAME ) TAG BY_NAME

   aData := { ;
       { 1, "Ana",   "Lisbon" }, ;
       { 2, "Bruno", "Porto"  }, ;
       { 3, "Carla", "Lisbon" }, ;
       { 4, "Diego", "Braga"  }, ;
       { 5, "Elena", "Lisbon" } }

   FOR EACH aRow IN aData
      dbAppend()
      FIELD->ID   := aRow[ 1 ]
      FIELD->NAME := aRow[ 2 ]
      FIELD->CITY := aRow[ 3 ]
   NEXT
   dbCommit()

   ListAll( "All records (" + LTrim( Str( LastRec() ) ) + " rows)" )

   /* ---- DELETE (mark) records 2 and 4 -------------------------- */
   ? "-- delete records id=2 and id=4 (mark only) --"
   GoIdRecord( 2 ) ; dbDelete()
   GoIdRecord( 4 ) ; dbDelete()

   /* With SET DELETED OFF (default) deleted rows are still visible,
    * but Deleted() reports their state. */
   SET DELETED OFF
   ListAll( "SET DELETED OFF -- deleted rows still show (flagged)" )

   /* With SET DELETED ON they vanish from every scan/walk. */
   SET DELETED ON
   ListAll( "SET DELETED ON -- deleted rows are hidden" )

   /* ---- RECALL (recover) record 2 ------------------------------ */
   ? "-- recall (recover) id=2 --"
   SET DELETED OFF
   GoIdRecord( 2 ) ; dbRecall()
   SET DELETED ON
   ListAll( "After recall id=2 (id=4 still deleted)" )

   /* ---- PACK: physically drop the still-deleted rows ----------- */
   ? "-- pack (physically remove remaining deleted rows) --"
   SET DELETED OFF
   PACK                          // removes id=4 for good; indexes auto-rebuilt
   ListAll( "After PACK (" + LTrim( Str( LastRec() ) ) + " rows)" )

   /* ---- SET FILTER: only contacts in Lisbon -------------------- */
   ? "-- SET FILTER to UPPER(CITY)='LISBON' --"
   SET FILTER TO UPPER( FIELD->CITY ) == "LISBON"
   ListAll( "Filtered view (Lisbon only)" )
   SET FILTER TO                 // clear the filter

   /* ---- SCOPE: restrict the BY_NAME walk to keys 'B'..'D' ------- */
   ? "-- scope BY_NAME to keys in ['B'..'D'] --"
   OrdSetFocus( "BY_NAME" )
   OrdScope( 0, "B" )            // low bound  (TOP)
   OrdScope( 1, "D" )            // high bound (BOTTOM)
   ListWalk( "Scoped walk B..D" )
   OrdScope( 0, NIL )            // clear scopes
   OrdScope( 1, NIL )

   dbCloseArea()
   AdsDisconnect()

   ? ""
   ? "Done."
   RETURN

/* ---- helpers --------------------------------------------------- */

/* Position on the record whose ID == nId, scanning in record order. */
STATIC PROCEDURE GoIdRecord( nId )
   LOCAL nSave := OrdSetFocus()         // remember active order
   OrdSetFocus( 0 )                     // natural (record) order
   dbGoTop()
   DO WHILE ! Eof() .AND. FIELD->ID != nId
      dbSkip()
   ENDDO
   OrdSetFocus( nSave )
   RETURN

/* List every visible record in natural (record) order. */
STATIC PROCEDURE ListAll( cTitle )
   LOCAL nSave := OrdSetFocus()
   ? "  " + cTitle + ":"
   OrdSetFocus( 0 )
   dbGoTop()
   DO WHILE ! Eof()
      ? "    id=" + LTrim( Str( FIELD->ID ) ) + "  " + ;
        PadR( AllTrim( FIELD->NAME ), 8 ) + PadR( AllTrim( FIELD->CITY ), 10 ) + ;
        iif( Deleted(), "[deleted]", "" )
      dbSkip()
   ENDDO
   OrdSetFocus( nSave )
   ?
   RETURN

/* Walk in the active index order (used to show scope effect).
 * The nGuard counter is a defensive bound so the loop always
 * terminates even if the active order misbehaves. */
STATIC PROCEDURE ListWalk( cTitle )
   LOCAL nGuard := 0
   ? "  " + cTitle + ":"
   dbGoTop()
   DO WHILE ! Eof() .AND. ++nGuard <= 1000
      ? "    " + PadR( AllTrim( FIELD->NAME ), 8 ) + AllTrim( FIELD->CITY )
      dbSkip()
   ENDDO
   ?
   RETURN
