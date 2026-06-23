/*
 * 01_hello_table.prg
 * ------------------------------------------------------------------
 * COOKBOOK / console track -- pure Harbour against OpenADS, NO ORM.
 *
 * Level: SIMPLE (start here).
 *
 * What it shows, end to end:
 *   1. Point the standard Harbour `rddads` RDD at the OpenADS engine.
 *   2. Open a LOCAL data directory (in-process engine -- no server).
 *   3. Create a table from scratch with a fictitious schema.
 *   4. Build a compound index and append a few invented rows.
 *   5. Walk the table in index order and run an exact `dbSeek`.
 *
 * Everything below uses ONLY the stock high-level xBase verbs
 * (USE / INDEX ON / dbAppend / dbSeek / dbSkip). The single thing
 * that makes those verbs land on OpenADS instead of any other RDD
 * is the `VIA "ADSCDX"` clause plus the OpenADS DLL being the one
 * resolved on PATH at run time.
 *
 * Data is 100% invented (a tiny "people" list). No real-world data.
 *
 * Build & run: see this folder's README.md, or run build.cmd.
 * ------------------------------------------------------------------
 */

#include "ads.ch"        // ADS_* constants (ADS_LOCAL_SERVER, ADS_CDX, ...)
#include "rddsys.ch"

/* rddads must be REQUESTed so the linker pulls the RDD in and it
 * registers itself at startup. #include alone is not enough. */
REQUEST ADS, ADSCDX, ADSNTX

PROCEDURE Main()

   LOCAL cDir := hb_DirTemp() + "oa_cookbook_01"
   LOCAL cDbf := cDir + hb_ps() + "people.dbf"
   LOCAL aData, aRow

   ? "OpenADS cookbook -- 01 hello table (pure Harbour, local)"
   ? "Engine version reported by the DLL:", AdsVersion()
   ?

   /* ---- 1. choose engine mode + default RDD -------------------- */
   /* LOCAL means the engine runs inside this process (no daemon).
    * For a remote server you would set ADS_REMOTE_SERVER and connect
    * to a tcp:// path instead -- see 07_remote_server.prg. */
   AdsSetServerType( ADS_LOCAL_SERVER )
   AdsSetFileType( ADS_CDX )          // .cdx compound indexes + .dbf tables
   RddSetDefault( "ADSCDX" )

   /* ---- 2. connect to a local data directory ------------------- */
   /* For the local engine the "connection path" is just a folder.
    * Tables created/opened afterwards live inside it. */
   hb_DirCreate( cDir )

   IF ! AdsConnect( cDir )
      ? "AdsConnect failed, DosError =", DosError()
      ErrorLevel( 1 )
      QUIT
   ENDIF

   /* fresh start: drop any leftovers from a previous run */
   IF File( cDbf )
      FErase( cDbf )
      FErase( hb_FNameExtSet( cDbf, ".cdx" ) )
   ENDIF

   /* ---- 3. create a table with an invented schema -------------- */
   /* Field list: { name, type, length, decimals }.
    *   C = character, N = numeric, D = date, L = logical, M = memo */
   DbCreate( cDbf, { ;
       { "ID",     "N",  6, 0 }, ;
       { "NAME",   "C", 30, 0 }, ;
       { "CITY",   "C", 20, 0 }, ;
       { "AGE",    "N",  3, 0 }, ;
       { "JOINED", "D",  8, 0 } }, "ADSCDX" )

   /* ---- 4. open EXCLUSIVE, index, append fictitious rows -------- */
   USE ( cDbf ) VIA "ADSCDX" NEW EXCLUSIVE

   /* Compound key: UPPER(NAME) so the order is case-insensitive. */
   INDEX ON UPPER( FIELD->NAME ) TAG BY_NAME

   aData := { ;
       { 1, "Alice Moreira", "Lisbon",   30, hb_SToD( "20240115" ) }, ;
       { 2, "bruno costa",   "Porto",    25, hb_SToD( "20240220" ) }, ;
       { 3, "Carla Dias",    "Braga",    41, hb_SToD( "20231005" ) }, ;
       { 4, "diego Faria",   "Lisbon",   38, hb_SToD( "20240601" ) } }

   FOR EACH aRow IN aData
      dbAppend()
      FIELD->ID     := aRow[ 1 ]
      FIELD->NAME   := aRow[ 2 ]
      FIELD->CITY   := aRow[ 3 ]
      FIELD->AGE    := aRow[ 4 ]
      FIELD->JOINED := aRow[ 5 ]
   NEXT
   dbCommit()                 // flush buffers to disk

   ? "Rows after append:", LastRec()
   ?

   /* ---- 5a. walk in index order -------------------------------- */
   ? "People in BY_NAME (case-insensitive) order:"
   OrdSetFocus( "BY_NAME" )
   dbGoTop()
   DO WHILE ! Eof()
      ? "  #" + Str( FIELD->ID, 2 ) + "  " + ;
        PadR( FIELD->NAME, 16 ) + " " + ;
        PadR( FIELD->CITY, 10 ) + " age " + LTrim( Str( FIELD->AGE ) )
      dbSkip()
   ENDDO
   ?

   /* ---- 5b. exact seek on the indexed key ---------------------- */
   /* The key was built on UPPER(NAME), so seek with an upper value. */
   dbSeek( "CARLA DIAS" )
   ? "Seek 'CARLA DIAS':", ;
     iif( Found(), "found at record " + LTrim( Str( RecNo() ) ) + ;
                   " (city " + AllTrim( FIELD->CITY ) + ")", "not found" )

   /* ---- 6. clean up -------------------------------------------- */
   dbCloseArea()
   AdsDisconnect()

   ? "Done."
   RETURN
