/*
 * 05_sql.prg
 * ------------------------------------------------------------------
 * COOKBOOK / console track -- pure Harbour against OpenADS, NO ORM.
 *
 * Level: INTERMEDIATE.
 *
 * What it shows, end to end:
 *   1. Create + populate an ordinary table with the xBase verbs.
 *   2. Run SQL straight against the engine with AdsExecuteSQLDirect:
 *        - a filtered SELECT (WHERE) with ORDER BY
 *        - an aggregate (COUNT / SUM) over a filter
 *        - SELECT DISTINCT
 *   3. Read each result back as a normal work area -- a SQL result set
 *      is just another cursor you walk with dbGoTop / dbSkip.
 *
 * The whole point: the SAME engine you drive navigationally (examples
 * 01-04) also answers SQL. You do NOT need the ORM for this -- the
 * stock `rddads` SQL entry points are enough. The ORM track builds a
 * fluent builder ON TOP of exactly these calls.
 *
 * Two SQL calls make it work:
 *   AdsCreateSQLStatement( <alias> )  opens a work area to hold a cursor
 *   AdsExecuteSQLDirect( <sql> )      runs the statement; that work area
 *                                     then IS the result set
 *
 * Data is 100% invented (a tiny "sales" list). No real-world data.
 *
 * Build & run: see this folder's README.md, or run build.cmd.
 * ------------------------------------------------------------------
 */

#include "ads.ch"
#include "rddsys.ch"

REQUEST ADS, ADSCDX, ADSNTX

PROCEDURE Main()

   LOCAL cDir := hb_DirTemp() + "oa_cookbook_05"
   LOCAL cDbf := cDir + hb_ps() + "sales.dbf"
   LOCAL aData, aRow

   ? "OpenADS cookbook -- 05 SQL (pure Harbour, local)"
   ? "Engine version reported by the DLL:", AdsVersion()
   ?

   /* ---- 1. local engine + default RDD -------------------------- */
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

   /* ---- 2. build a small table the navigational way ------------ */
   DbCreate( cDbf, { ;
       { "ID",    "N",  6, 0 }, ;
       { "SELLER","C", 16, 0 }, ;
       { "UF",    "C",  2, 0 }, ;
       { "AMOUNT","N", 10, 2 } }, "ADSCDX" )

   USE ( cDbf ) VIA "ADSCDX" NEW EXCLUSIVE

   aData := { ;
       { 1, "Ana",   "SP", 1200.00 }, ;
       { 2, "Bruno", "RJ",  800.00 }, ;
       { 3, "Carla", "SP",  450.00 }, ;
       { 4, "Diego", "MG",  990.00 }, ;
       { 5, "Ana",   "SP",  300.00 } }

   FOR EACH aRow IN aData
      dbAppend()
      FIELD->ID     := aRow[ 1 ]
      FIELD->SELLER := aRow[ 2 ]
      FIELD->UF     := aRow[ 3 ]
      FIELD->AMOUNT := aRow[ 4 ]
   NEXT
   dbCommit()
   dbCloseArea()              // close it; SQL reaches the file by name
   ? "Seeded", LTrim( Str( Len( aData ) ) ), "rows into sales.dbf"
   ?

   /* ---- 3a. a filtered SELECT ---------------------------------- */
   ? "SELECT * FROM sales WHERE UF = 'SP' ORDER BY AMOUNT DESC"
   RunSQL( "SELECT ID, SELLER, UF, AMOUNT FROM sales " + ;
           "WHERE UF = 'SP' ORDER BY AMOUNT DESC", "Q1" )

   /* ---- 3b. an aggregate over a filter ------------------------- */
   /* How many SP sales and their total amount -- one summary row. */
   ? "SELECT COUNT(*), SUM(AMOUNT) FROM sales WHERE UF = 'SP'"
   RunSQL( "SELECT COUNT(*) AS N, SUM(AMOUNT) AS TOTAL FROM sales " + ;
           "WHERE UF = 'SP'", "Q2" )

   /* ---- 3c. the distinct list of states ------------------------ */
   ? "SELECT DISTINCT UF FROM sales ORDER BY UF"
   RunSQL( "SELECT DISTINCT UF FROM sales ORDER BY UF", "Q3" )

   /* ---- 4. clean up -------------------------------------------- */
   AdsDisconnect()
   ? "Done."
   RETURN

/* ------------------------------------------------------------------
 * Run one SQL statement and dump the resulting cursor.
 *
 * AdsCreateSQLStatement opens a brand-new work area (aliased cAlias)
 * that is set up to hold a SQL cursor; it becomes the current area.
 * AdsExecuteSQLDirect then runs the statement INTO that area, so right
 * afterwards the area is just a read-only table you browse normally.
 * ------------------------------------------------------------------ */
STATIC PROCEDURE RunSQL( cSql, cAlias )
   LOCAL i, nFields

   IF ! AdsCreateSQLStatement( cAlias )
      ? "  (could not create SQL statement area)"
      RETURN
   ENDIF

   IF ! AdsExecuteSQLDirect( cSql )
      ? "  (statement failed, DosError =", DosError(), ")"
      dbCloseArea()
      RETURN
   ENDIF

   /* Header: the cursor exposes its columns like any work area. */
   nFields := FCount()
   FOR i := 1 TO nFields
      ?? iif( i == 1, "  ", " | " ) + RTrim( FieldName( i ) )
   NEXT
   ?

   /* Body: walk the result set with the ordinary navigational verbs. */
   dbGoTop()
   DO WHILE ! Eof()
      FOR i := 1 TO nFields
         ?? iif( i == 1, "  ", " | " ) + AllTrim( hb_CStr( FieldGet( i ) ) )
      NEXT
      ?
      dbSkip()
   ENDDO

   dbCloseArea()              // close the cursor
   ?
   RETURN
