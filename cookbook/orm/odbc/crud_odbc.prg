/*
 * crud_odbc.prg
 * ------------------------------------------------------------------
 * COOKBOOK / ORM track -- the companion Harbour ORM over OpenADS.
 * Back-end: any ODBC engine (odbc://<ODBC connection string>).
 *
 * Level: INTERMEDIATE.
 *
 * Identical CRUD to sqlite/crud_sqlite.prg -- only the connection
 * string differs. ODBC is the universal door: one driver manager, many
 * engines (SQL Server, Access, MySQL, PostgreSQL, Firebird...). You give
 * a normal ODBC connection string after the odbc:// scheme; either a DSN
 * or a full driver-and-attributes string works.
 *
 * To run, set the URI in the environment (never hardcode credentials):
 *   set DEMO_ODBC_URI=odbc://DSN=demo;UID=app;PWD=secret           (Windows)
 *   set DEMO_ODBC_URI=odbc://Driver={ODBC Driver 18 for SQL Server};Server=127.0.0.1;Database=demo;UID=app;PWD=secret;Encrypt=no
 *   export DEMO_ODBC_URI=odbc://DSN=demo;UID=app;PWD=secret        (POSIX/unixODBC)
 * If it is unset, the example prints how to set it and exits cleanly.
 *
 * Needs an OpenADS DLL built with OPENADS_WITH_ODBC, plus an installed
 * ODBC driver (and DSN, if you use one). Data is invented.
 * Build & run: see ../README.md.
 * ------------------------------------------------------------------
 */

#include "hborm.ch"

CREATE CLASS Person FROM TORMModel
   METHOD TableName() INLINE "people"
END CLASS

PROCEDURE Main()

   LOCAL oCn, oQ, oP, aRows
   LOCAL cUri := AllTrim( GetEnv( "DEMO_ODBC_URI" ) )

   ? "OpenADS cookbook -- ORM CRUD over ODBC"
   ? "engine:", hbo_Version()
   ?

   IF Empty( cUri )
      ? "Set DEMO_ODBC_URI to an odbc:// connection string, e.g.:"
      ? "  odbc://DSN=demo;UID=app;PWD=secret"
      ? "  odbc://Driver={ODBC Driver 18 for SQL Server};Server=127.0.0.1;Database=demo;UID=app;PWD=secret;Encrypt=no"
      ? "(skipped -- nothing to connect to)"
      RETURN
   ENDIF

   oCn := TORMConnection():New( cUri )
   IF ! oCn:IsOpen()
      ? "connect failed:", hbo_LastErr()
      ? "(driver installed? DSN valid? DLL built with OPENADS_WITH_ODBC?)"
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
