/*
 * crud_postgresql.prg
 * ------------------------------------------------------------------
 * COOKBOOK / ORM track -- the companion Harbour ORM over OpenADS.
 * Back-end: PostgreSQL (postgresql://user:pass@host:port/db).
 *
 * Level: INTERMEDIATE.
 *
 * Identical CRUD to sqlite/crud_sqlite.prg -- only the connection
 * string differs. PostgreSQL is a client/server engine, so you point
 * the URI at a running server and a database you can create tables in.
 *
 * To run, set the URI in the environment (never hardcode credentials):
 *   set DEMO_PG_URI=postgresql://app:secret@127.0.0.1:5432/demo   (Windows)
 *   export DEMO_PG_URI=postgresql://app:secret@127.0.0.1:5432/demo (POSIX)
 * If it is unset, the example prints how to set it and exits cleanly.
 *
 * Needs an OpenADS DLL built with OPENADS_WITH_POSTGRESQL.
 * Data is invented. Build & run: see ../README.md.
 * ------------------------------------------------------------------
 */

#include "hborm.ch"

CREATE CLASS Person FROM TORMModel
   METHOD TableName() INLINE "people"
END CLASS

PROCEDURE Main()

   LOCAL oCn, oQ, oP, aRows
   LOCAL cUri := AllTrim( GetEnv( "DEMO_PG_URI" ) )

   ? "OpenADS cookbook -- ORM CRUD on PostgreSQL"
   ? "engine:", hbo_Version()
   ?

   IF Empty( cUri )
      ? "Set DEMO_PG_URI to a PostgreSQL connection string, e.g.:"
      ? "  postgresql://app:secret@127.0.0.1:5432/demo"
      ? "(skipped -- nothing to connect to)"
      RETURN
   ENDIF

   oCn := TORMConnection():New( cUri )
   IF ! oCn:IsOpen()
      ? "connect failed:", hbo_LastErr()
      ? "(server reachable? DLL built with OPENADS_WITH_POSTGRESQL?)"
      ErrorLevel( 1 )
      QUIT
   ENDIF
   TORMConnection_Default( oCn )

   /* fresh schema + invented rows */
   oCn:Execute( "DROP TABLE people" )
   oCn:Execute( "CREATE TABLE people ( id INTEGER, name VARCHAR(40), uf CHAR(2) )" )
   oCn:Execute( "INSERT INTO people ( id, name, uf ) VALUES ( 1, 'Ana Souza',  'SP' )" )
   oCn:Execute( "INSERT INTO people ( id, name, uf ) VALUES ( 2, 'Bruno Lima', 'RJ' )" )
   oCn:Execute( "INSERT INTO people ( id, name, uf ) VALUES ( 3, 'Carla Reis', 'SP' )" )

   aRows := oCn:Query( "SELECT id, name, uf FROM people ORDER BY id" )
   ? "all people (" + LTrim( Str( Len( aRows ) ) ) + "):"
   ShowRows( aRows )

   /* fluent builder -> SQL */
   oQ := TORMQuery():New( oCn, "people" ):Where( "uf", "SP" ):OrderBy( "id", "DESC" )
   ? "builder SQL:", oQ:ToSql()
   ShowRows( oQ:Get() )

   /* Model CRUD */
   ? "Model CRUD:"
   Person():New():Create( { "id" => 4, "name" => "Davi Melo", "uf" => "MG" } )
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

STATIC PROCEDURE ShowRows( aRows )
   LOCAL h
   FOR EACH h IN aRows
      ? "    id=" + AllTrim( hb_CStr( hb_HGetDef( h, "id", "" ) ) ) + ;
        "  " + PadR( AllTrim( hb_CStr( hb_HGetDef( h, "name", "" ) ) ), 12 ) + ;
        "  uf=" + AllTrim( hb_CStr( hb_HGetDef( h, "uf", "" ) ) )
   NEXT
   RETURN
