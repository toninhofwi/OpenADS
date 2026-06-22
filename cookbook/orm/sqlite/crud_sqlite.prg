/*
 * crud_sqlite.prg
 * ------------------------------------------------------------------
 * COOKBOOK / ORM track -- the companion Harbour ORM over OpenADS.
 * Back-end: SQLite (connection string sqlite://<file>).
 *
 * Level: INTERMEDIATE.
 *
 * What it shows, all through the ORM (no hand-written record loops):
 *   1. Open a connection from a URI.
 *   2. Create a table and seed invented rows.
 *   3. Read with a raw SELECT and with the FLUENT BUILDER, which
 *      translates Harbour method calls into SQL for you.
 *   4. Full Model CRUD: Create / Find / Get / Set / Save / Delete.
 *   5. An aggregate (Count).
 *
 * The exact same Harbour code runs against every back-end -- only the
 * connection string in cUri() changes (see the sibling folders dbf/,
 * adt/, postgresql/, mariadb/, odbc/, and complete/).
 *
 * Data is invented (a small "people" list). No real records.
 *
 * Build & run: see ../README.md (needs the companion ORM sources;
 * the sqlite back-end needs an OpenADS DLL built with OPENADS_WITH_SQLITE).
 * ------------------------------------------------------------------
 */

#include "hborm.ch"

/* A Model is just a class that names its table. Everything else
 * (CRUD, hydration to native types) comes from TORMModel. */
CREATE CLASS Person FROM TORMModel
   METHOD TableName() INLINE "people"
END CLASS

PROCEDURE Main()

   LOCAL oCn, oQ, oP, aRows

   ? "OpenADS cookbook -- ORM CRUD on SQLite"
   ? "engine:", hbo_Version()
   ?

   /* ---- 1. connect -------------------------------------------- */
   /* A local single-file database. Created on first use. */
   oCn := TORMConnection():New( cUri() )
   IF ! oCn:IsOpen()
      ? "connect failed:", hbo_LastErr()
      ? "(needs an OpenADS DLL built with OPENADS_WITH_SQLITE)"
      ErrorLevel( 1 )
      QUIT
   ENDIF
   TORMConnection_Default( oCn )         // models use this connection

   /* ---- 2. fresh schema + seed -------------------------------- */
   oCn:Execute( "DROP TABLE people" )    // ignore error if absent
   oCn:Execute( "CREATE TABLE people ( id INTEGER, name VARCHAR(40), uf CHAR(2), active Logical )" )

   oCn:Execute( "INSERT INTO people ( id, name, uf ) VALUES ( 1, 'Ana Souza',  'SP' )" )
   oCn:Execute( "INSERT INTO people ( id, name, uf ) VALUES ( 2, 'Bruno Lima', 'RJ' )" )
   oCn:Execute( "INSERT INTO people ( id, name, uf ) VALUES ( 3, 'Carla Reis', 'SP' )" )

   /* ---- 3a. read with a raw SELECT ---------------------------- */
   aRows := oCn:Query( "SELECT id, name, uf FROM people ORDER BY id" )
   ? "raw SELECT returned", LTrim( Str( Len( aRows ) ) ), "rows:"
   ShowRows( aRows )

   /* ---- 3b. read with the FLUENT BUILDER (Harbour -> SQL) ------ */
   /* These method calls are translated into a SQL statement. :ToSql()
    * lets you SEE the generated SQL before it runs -- handy for
    * learning and debugging the Harbour-to-SQL translation. */
   oQ := TORMQuery():New( oCn, "people" ):Where( "uf", "SP" ):OrderBy( "id", "DESC" )
   ? "fluent builder generated SQL:"
   ? "  " + oQ:ToSql()
   aRows := oQ:Get()
   ? "  -> returned", LTrim( Str( Len( aRows ) ) ), "rows (uf=SP, id DESC):"
   ShowRows( aRows )

   /* ---- 4. Model CRUD ----------------------------------------- */
   ? "Model CRUD:"
   oP := Person():New()
   oP:Create( { "id" => 4, "name" => "Davi Melo", "uf" => "MG", "active" => .T. } )
   ? "  created id=4"

   oP := Person():New():Find( 4 )
   ? "  find(4) -> name =", AllTrim( oP:Get( "name" ) ), " uf =", AllTrim( oP:Get( "uf" ) )

   oP:Set( "uf", "BA" )
   oP:Save()
   ? "  after Save(uf=BA), reload uf =", AllTrim( Person():New():Find( 4 ):Get( "uf" ) )

   oP:Delete()
   ? "  delete(4) -> find(4) is NIL:", Person():New():Find( 4 ) == NIL

   /* ---- 5. count remaining rows ------------------------------- */
   ? "  remaining rows:", LTrim( Str( Len( oCn:Query( "SELECT id FROM people" ) ) ) )

   /* ---- cleanup ----------------------------------------------- */
   oCn:Execute( "DROP TABLE people" )
   oCn:Close()

   ? ""
   ? "Done."
   RETURN

/* Connection string. Override with DEMO_DB_URI; default is a local file. */
STATIC FUNCTION cUri()
   LOCAL c := GetEnv( "DEMO_DB_URI" )
   RETURN iif( Empty( c ), "sqlite://./demo_people.db", c )

STATIC PROCEDURE ShowRows( aRows )
   LOCAL h
   FOR EACH h IN aRows
      ? "    id=" + AllTrim( hb_CStr( hb_HGetDef( h, "id", "" ) ) ) + ;
        "  " + PadR( AllTrim( hb_CStr( hb_HGetDef( h, "name", "" ) ) ), 12 ) + ;
        "  uf=" + AllTrim( hb_CStr( hb_HGetDef( h, "uf", "" ) ) )
   NEXT
   RETURN
