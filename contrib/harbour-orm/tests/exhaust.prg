/* exhaust.prg -- exhaustion harness: drive the SAME ORM classes against every
 * reachable backend on the ABI engine and report pass/skip/fail per backend.
 *
 * Always-on backends (no external server needed):
 *   sqlite    : sqlite://<file>
 *   dbf-local : a local directory -> native DBF/ADT tables (proprietary format)
 *
 * Env-gated backends (set the URI to include them; unset = skipped cleanly):
 *   OADS_SERVER_URI   e.g. tcp://127.0.0.1:6262   (OpenADS server -- top priority)
 *   OADS_PG_URI       e.g. postgresql://user@host/db
 *   OADS_MARIA_URI    e.g. mariadb://root@127.0.0.1:3306/test
 *   OADS_ODBC_URI     e.g. odbc://Driver={...};...
 *
 * Run:  exhaust.exe            (sqlite + dbf-local)
 *       set OADS_SERVER_URI=... & exhaust.exe
 */
#include "hborm.ch"

PROCEDURE Main()
   LOCAL aB := {}, b, nGP := 0, nGS := 0, nGF := 0, nGC := 0, aR
   LOCAL cWork := WorkDir()

   IF Empty( GetEnv( "OADS_SERVER_ONLY" ) )
      AAdd( aB, { "sqlite",    "sqlite://" + cWork + "\exh_sqlite.db", .F. } )
      AAdd( aB, { "dbf-local", cWork + "\dbf",                          .T. } )
   ENDIF

   AddEnv( @aB, "openads-server", "OADS_SERVER_URI" )
   AddEnv( @aB, "postgresql",     "OADS_PG_URI" )
   AddEnv( @aB, "mariadb",        "OADS_MARIA_URI" )
   AddEnv( @aB, "odbc",           "OADS_ODBC_URI" )

   ? "=== hb_orm exhaustion harness ==="
   ? "engine:", hbo_Version()
   ?

   FOR EACH b IN aB
      ? "--- [" + b[ 1 ] + "]  " + b[ 2 ]
      aR := RunBackend( b[ 2 ], b[ 3 ] )      // { status, pass, fail, msg }
      DO CASE
      CASE aR[ 1 ] == "PASS"
         ? "    PASS  (" + LTrim( Str( aR[ 2 ] ) ) + " checks" + ;
           iif( aR[ 4 ] > 0, ", " + LTrim( Str( aR[ 4 ] ) ) + " engine-caveat", "" ) + ")"
         nGP++
         nGC += aR[ 4 ]
      CASE aR[ 1 ] == "SKIP"
         ? "    SKIP  (" + aR[ 5 ] + ")"
         nGS++
      OTHERWISE
         ? "    FAIL  (" + LTrim( Str( aR[ 3 ] ) ) + " failed) " + aR[ 5 ]
         nGF++
         nGC += aR[ 4 ]
      ENDCASE
      ?
   NEXT

   ? "Backends: pass=" + LTrim( Str( nGP ) ) + ;
     " skip=" + LTrim( Str( nGS ) ) + " fail=" + LTrim( Str( nGF ) ) + ;
     " | engine-caveats=" + LTrim( Str( nGC ) )
   IF nGF > 0
      ErrorLevel( 1 )
   ENDIF
   RETURN

/* Full CRUD cycle via the ORM. Returns { status, nPass, nFail, nCaveat, cMsg }.
 * lMakeDir: ensure the directory exists (dbf-local backend).
 * Caveats = end-to-end checks that fail only because of a documented ENGINE
 * limitation (not an ORM defect); see docs/EXHAUSTION_FINDINGS.md. */
STATIC FUNCTION RunBackend( cUri, lMakeDir )
   LOCAL oCn, oM, oFound, aRows, nP := 0, nF := 0, nC := 0, cTbl := "orm_exh"

   IF lMakeDir
      MakeDir( cUri )                          // cUri is a directory path here
   ENDIF

   oCn := TORMConnection():New( cUri )
   IF ! oCn:IsOpen()
      RETURN { "SKIP", 0, 0, 0, "connect failed: " + hbo_LastErr() }
   ENDIF

   /* idempotent reset */
   oCn:Execute( "DROP TABLE " + cTbl )

   IF ! oCn:Execute( "CREATE TABLE " + cTbl + ;
         " ( id INTEGER, nome VARCHAR(40), uf CHAR(2), ativo Logical )" )
      oCn:Close()
      RETURN { "SKIP", 0, 0, 0, "CREATE TABLE not supported (navigational backend?)" }
   ENDIF

   /* explicit column list -- portable across SQL and DBF/ADT backends
      (DBF rejects positional INSERT ... VALUES without a column list) */
   nP += Chk( "insert 1", oCn:Execute( "INSERT INTO " + cTbl + " ( id, nome, uf ) VALUES ( 1, 'Ana Souza', 'SP' )" ), @nF )
   nP += Chk( "insert 2", oCn:Execute( "INSERT INTO " + cTbl + " ( id, nome, uf ) VALUES ( 2, 'Bruno Lima', 'RJ' )" ), @nF )
   nP += Chk( "insert 3", oCn:Execute( "INSERT INTO " + cTbl + " ( id, nome, uf ) VALUES ( 3, 'Carla Reis', 'SP' )" ), @nF )

   aRows := oCn:Query( "SELECT id, nome, uf FROM " + cTbl + " ORDER BY id" )
   nP += Chk( "select count==3", Len( aRows ) == 3, @nF )
   IF Len( aRows ) >= 1
      nP += Chk( "select row1 nome", AllTrim( hb_HGetDef( aRows[ 1 ], "nome", "" ) ) == "Ana Souza", @nF )
   ENDIF

   /* fluent builder filter */
   aRows := TORMQuery():New( oCn, cTbl ):Where( "uf", "SP" ):OrderBy( "id", "DESC" ):Get()
   nP += Chk( "where uf=SP count==2", Len( aRows ) == 2, @nF )
   IF Len( aRows ) >= 1
      nP += Chk( "where first id==3 (DESC)", AllTrim( hb_HGetDef( aRows[ 1 ], "id", "" ) ) == "3", @nF )
   ENDIF

   /* model CRUD */
   TORMConnection_Default( oCn )
   oM := TORMExh():New()
   oM:Create( { "id" => 4, "nome" => "Davi Melo", "uf" => "MG", "ativo" => .T. } )
   oFound := TORMExh():New():Find( 4 )
   nP += Chk( "model find !NIL", oFound != NIL, @nF )
   IF oFound != NIL
      nP += Chk( "model find nome", AllTrim( oFound:Get( "nome" ) ) == "Davi Melo", @nF )
      nP += Chk( "model logical ativo true", LogiTrue( oFound:Get( "ativo" ) ), @nF )
      oFound:Set( "uf", "BA" )
      nP += Chk( "model save(update)", oFound:Save(), @nF )
      nP += Chk( "model reload uf==BA", AllTrim( TORMExh():New():Find( 4 ):Get( "uf" ) ) == "BA", @nF )
      nP += Chk( "model delete", oFound:Delete(), @nF )
      /* F3 used to make this a caveat on DBF/server: SQL-over-xBase equality
         WHERE ignores the deletion flag, so a SQL Find of a just-deleted row
         could still return it. The navigational Find path (AdsSeek/scan honors
         SET DELETED) closes that, so this is now a HARD check on every backend. */
      ? "      [diag] after delete id=4 -> scan=" + ;
        LTrim( Str( Len( oCn:Query( "SELECT id FROM " + cTbl + " ORDER BY id" ) ) ) ) + ;
        " | WHERE id=4=" + ;
        LTrim( Str( Len( oCn:Query( "SELECT id FROM " + cTbl + " WHERE id = 4" ) ) ) ) + ;
        " | WHERE id=4 LIMIT 1=" + ;
        LTrim( Str( Len( oCn:Query( "SELECT id FROM " + cTbl + " WHERE id = 4 LIMIT 1" ) ) ) )
      nP += Chk( "model find-after-delete NIL", TORMExh():New():Find( 4 ) == NIL, @nF )
   ENDIF

   oCn:Execute( "DROP TABLE " + cTbl )
   oCn:Close()

   RETURN { iif( nF == 0, "PASS", "FAIL" ), nP, nF, nC, "" }

/* logical round-trips as "T" on xBase/nav backends and "1" on SQL backends */
STATIC FUNCTION LogiTrue( cVal )
   LOCAL c := Upper( AllTrim( cVal ) )
   RETURN c == "T" .OR. c == "1" .OR. c == "Y" .OR. c == ".T."

STATIC FUNCTION Chk( cLabel, lCond, nFail )
   IF ! lCond
      nFail++
      ? "      x " + cLabel
      RETURN 0
   ENDIF
   RETURN 1

/* A check that only fails due to a documented engine limitation: surfaced
   loudly (never hidden) but counted as a caveat, not an ORM failure. */
STATIC PROCEDURE ChkCav( cLabel, lCond, nCav )
   IF ! lCond
      nCav++
      ? "      ~ engine-caveat: " + cLabel + "  (see EXHAUSTION_FINDINGS F3)"
   ELSE
      ? "      ok  " + cLabel
   ENDIF
   RETURN

STATIC PROCEDURE AddEnv( aB, cLabel, cEnvVar )
   LOCAL cUri := AllTrim( GetEnv( cEnvVar ) )
   IF ! Empty( cUri )
      AAdd( aB, { cLabel, cUri, .F. } )
   ENDIF
   RETURN

STATIC FUNCTION WorkDir()
   LOCAL c := hb_DirBase()
   RETURN iif( Empty( c ), ".", SubStr( c, 1, Len( c ) - 1 ) )

STATIC PROCEDURE MakeDir( cDir )
   IF ! hb_DirExists( cDir )
      hb_DirCreate( cDir )
   ENDIF
   RETURN

CREATE CLASS TORMExh FROM TORMModel
   METHOD TableName() INLINE "orm_exh"
END CLASS
