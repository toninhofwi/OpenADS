/*
 * crud_dbf.prg
 * ------------------------------------------------------------------
 * COOKBOOK / ORM track -- the companion Harbour ORM over OpenADS.
 * Back-end: DBF file tables (navigational; connection = a folder path).
 *
 * Level: INTERMEDIATE.
 *
 * Same ORM CRUD as sqlite/crud_sqlite.prg, but the connection string
 * is a DIRECTORY, so tables are DBF files inside it. There is no SQL
 * server here: the ORM walks records through the engine's cursor API.
 * The Model layer (Find / Save / Delete) honours the deletion flag on
 * this navigational path.
 *
 * Data is invented (a small "people" list). No real records.
 *
 * Build & run: see ../README.md. This back-end needs no special build
 * flag -- DBF support is always present.
 * ------------------------------------------------------------------
 */

#include "hborm.ch"

CREATE CLASS Person FROM TORMModel
   METHOD TableName() INLINE "people"
END CLASS

PROCEDURE Main()

   LOCAL oCn, oQ, oP, aRows

   ? "OpenADS cookbook -- ORM CRUD on DBF (navigational)"
   ? "engine:", hbo_Version()
   ?

   /* ---- 1. connect to a data DIRECTORY ------------------------ */
   PrepareDir( cDir() )
   oCn := TORMConnection():New( cDir() )
   IF ! oCn:IsOpen()
      ? "connect failed:", hbo_LastErr()
      ErrorLevel( 1 )
      QUIT
   ENDIF
   TORMConnection_Default( oCn )

   /* ---- 2. fresh schema + seed -------------------------------- */
   /* On a navigational back-end CREATE TABLE makes a DBF file. Give an
    * explicit column list on INSERT (positional VALUES is SQL-only). */
   oCn:Execute( "DROP TABLE people" )
   oCn:Execute( "CREATE TABLE people ( id INTEGER, name VARCHAR(40), uf CHAR(2), active Logical )" )

   oCn:Execute( "INSERT INTO people ( id, name, uf ) VALUES ( 1, 'Ana Souza',  'SP' )" )
   oCn:Execute( "INSERT INTO people ( id, name, uf ) VALUES ( 2, 'Bruno Lima', 'RJ' )" )
   oCn:Execute( "INSERT INTO people ( id, name, uf ) VALUES ( 3, 'Carla Reis', 'SP' )" )

   /* ---- 3. read: raw SELECT + fluent builder ------------------ */
   aRows := oCn:Query( "SELECT id, name, uf FROM people ORDER BY id" )
   ? "all people (" + LTrim( Str( Len( aRows ) ) ) + "):"
   ShowRows( aRows )

   oQ := TORMQuery():New( oCn, "people" ):Where( "uf", "SP" ):OrderBy( "id", "DESC" )
   ? "builder SQL:", oQ:ToSql()
   ShowRows( oQ:Get() )

   /* ---- 4. Model CRUD ----------------------------------------- */
   ? "Model CRUD:"
   Person():New():Create( { "id" => 4, "name" => "Davi Melo", "uf" => "MG", "active" => .T. } )
   oP := Person():New():Find( 4 )
   ? "  find(4) name =", AllTrim( oP:Get( "name" ) )
   oP:Set( "uf", "BA" ) ; oP:Save()
   ? "  reload uf =", AllTrim( Person():New():Find( 4 ):Get( "uf" ) )
   oP:Delete()
   ? "  find(4) after delete is NIL:", Person():New():Find( 4 ) == NIL

   oCn:Execute( "DROP TABLE people" )
   oCn:Close()

   ? ""
   ? "Done."
   RETURN

/* Data directory. Override with DEMO_DB_DIR; default is a local folder. */
STATIC FUNCTION cDir()
   LOCAL c := GetEnv( "DEMO_DB_DIR" )
   RETURN iif( Empty( c ), ".." + hb_ps() + "_demo_dbf_data", c )

STATIC PROCEDURE PrepareDir( cDir )
   IF ! hb_DirExists( cDir )
      hb_DirCreate( cDir )
   ENDIF
   RETURN

STATIC PROCEDURE ShowRows( aRows )
   LOCAL h
   FOR EACH h IN aRows
      ? "    id=" + AllTrim( hb_CStr( hb_HGetDef( h, "id", "" ) ) ) + ;
        "  " + PadR( AllTrim( hb_CStr( hb_HGetDef( h, "name", "" ) ) ), 12 ) + ;
        "  uf=" + AllTrim( hb_CStr( hb_HGetDef( h, "uf", "" ) ) )
   NEXT
   RETURN
