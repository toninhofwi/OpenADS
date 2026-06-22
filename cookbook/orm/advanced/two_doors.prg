/*
 * two_doors.prg
 * ------------------------------------------------------------------
 * COOKBOOK / ORM track -- the companion Harbour ORM, NEXT revision.
 * Back-end: SQLite (sqlite://...), so the output is identical on any
 * machine and independent of any file-table specifics.
 *
 * Level: ADVANCED.
 *
 * The stable ORM (see ../sqlite, ../dbf, ...) covers everyday CRUD plus
 * a fluent builder. The next revision adds two "doors" into the same
 * engine -- both shown here, end to end:
 *
 *   DOOR A -- DB-FIRST ("connect and use"): point the ORM at an
 *     EXISTING table; it introspects the columns and their types and
 *     hands you a working model WITHOUT writing a class. It can also
 *     SCAFFOLD a model .prg for you from the live schema.
 *
 *   DOOR B -- FULL POWER: a broad fluent builder (joins, aggregates,
 *     ...) whose SQL you can INSPECT before it runs, plus model
 *     RELATIONS with EAGER loading that avoids the classic N+1 query
 *     explosion -- proven here with a live query counter.
 *
 * Verbs come in English and Portuguese (Find/Buscar, All/Todos,
 * With/Com, Where/Onde); this example mixes them on purpose.
 *
 * Data is invented (users + posts). No real records.
 * Build & run: see ./README.md (needs the next-revision ORM sources).
 * ------------------------------------------------------------------
 */

#include "hborm.ch"

/* ---- models with RELATIONS (used by Door B) ----------------------- */
CREATE CLASS TUser FROM TORMModel
   METHOD TableName() INLINE "users"
   METHOD Relations() INLINE { ;
      "posts" => ORM_HasMany( {| c | TPost():New( c ) }, "user_id" ) }
END CLASS

CREATE CLASS TPost FROM TORMModel
   METHOD TableName() INLINE "posts"
   METHOD Relations() INLINE { ;
      "autor" => ORM_BelongsTo( {| c | TUser():New( c ) }, "user_id" ) }
END CLASS

PROCEDURE Main()

   LOCAL oCn, oQ, r, aUsers, oU, cSrc, hCol

   ? "OpenADS cookbook -- ORM next revision: the two doors (SQLite)"
   ? "engine:", hbo_Version()
   ?

   oCn := TORMConnection():New( cUri() )
   IF ! oCn:IsOpen()
      ? "connect failed:", hbo_LastErr()
      ErrorLevel( 1 )
      QUIT
   ENDIF
   TORMConnection_Default( oCn )
   Seed( oCn )

   /* ===== DOOR A -- DB-FIRST ======================================== */
   ? "== Door A: DB-first (introspect / open / scaffold) =="

   /* 1. introspect an existing table -> { column => cast-token } */
   ? "introspect 'users':"
   FOR EACH hCol IN ORM_Introspect( oCn, "users" )
      ? "   " + PadR( hCol[ "nome" ], 10 ) + " -> " + hCol[ "cast" ]
   NEXT

   /* 2. a working model with NO class declaration (Buscar = Find) */
   oU := ORM_Abrir( "users" ):Buscar( 1 )
   ? "ORM_Abrir('users'):Buscar(1) ->", AllTrim( oU:Get( "nome" ) )

   /* 3. scaffold a model .prg straight from the live schema */
   cSrc := ORM_Scaffold( "users", oCn )
   ? "ORM_Scaffold('users') generated:"
   ? Indent( cSrc )
   ?

   /* ===== DOOR B -- FULL POWER ====================================== */
   ? "== Door B: full-power builder + relations =="

   /* 1. a broad query -- SEE the SQL before you run it */
   oQ := TORMQuery():New( oCn, "posts" ) ;
        :Join( "users", "posts.user_id", "=", "users.id" ) ;
        :Where( "uf", "SP" ):OrderBy( "posts.id", "ASC" ):Limit( 10 )
   r := oQ:Compiled()
   ? "Compiled SQL:", r[ "sql" ]
   ? "rows (posts by SP authors):", LTrim( Str( Len( oQ:Get() ) ) )

   /* 2. aggregates straight off the builder */
   ? "total posts:", LTrim( Str( TORMQuery():New( oCn, "posts" ):Count() ) )
   ? "posts by user 1:", ;
     LTrim( Str( TORMQuery():New( oCn, "posts" ):Where( "user_id", "=", 1 ):Count() ) )
   ?

   /* 3. relations + EAGER loading: anti-N+1, proven by the counter.
    *    Naive lazy loading would cost 1 query for the users + 1 per user
    *    (N+1). Eager 'Com' batches all posts into a single extra query. */
   oCn:ResetQueryCount()
   aUsers := TUser():New( oCn ):Com( "posts" ):Todos()    // Com = With (eager)
   ? "eager-loaded " + LTrim( Str( Len( aUsers ) ) ) + " users with their posts"
   ? "queries spent (1 base + 1 for ALL posts = 2, not N+1):", ;
     LTrim( Str( oCn:QueryCount() ) )
   FOR EACH oU IN aUsers
      ? "   " + PadR( AllTrim( oU:Get( "nome" ) ), 10 ) + " has " + ;
        LTrim( Str( Len( oU:Rel( "posts" ) ) ) ) + " post(s)"
   NEXT
   ? "queries after touching the relations (already cached):", ;
     LTrim( Str( oCn:QueryCount() ) )

   oCn:Close()
   ? ""
   ? "Done."
   RETURN

/* Connection URI. Override with DEMO_DB; default is a local SQLite file. */
STATIC FUNCTION cUri()
   LOCAL c := GetEnv( "DEMO_DB" )
   RETURN iif( Empty( c ), "sqlite://./demo_advanced.db", c )

STATIC PROCEDURE Seed( oCn )
   LOCAL c
   FOR EACH c IN { "DROP TABLE posts", "DROP TABLE users" }
      oCn:Execute( c )                 // may fail on first run -- harmless
   NEXT
   oCn:Execute( "CREATE TABLE users ( id INTEGER, nome VARCHAR(40), uf CHAR(2) )" )
   oCn:Execute( "CREATE TABLE posts ( id INTEGER, user_id INTEGER, titulo VARCHAR(60) )" )
   oCn:Execute( "INSERT INTO users ( id, nome, uf ) VALUES ( 1, 'Ana Souza',  'SP' )" )
   oCn:Execute( "INSERT INTO users ( id, nome, uf ) VALUES ( 2, 'Bruno Lima', 'RJ' )" )
   oCn:Execute( "INSERT INTO users ( id, nome, uf ) VALUES ( 3, 'Carla Reis', 'SP' )" )
   oCn:Execute( "INSERT INTO posts ( id, user_id, titulo ) VALUES ( 10, 1, 'hello' )" )
   oCn:Execute( "INSERT INTO posts ( id, user_id, titulo ) VALUES ( 11, 1, 'world' )" )
   oCn:Execute( "INSERT INTO posts ( id, user_id, titulo ) VALUES ( 12, 3, 'sql ftw' )" )
   RETURN

/* indent a multi-line block for tidy console output */
STATIC FUNCTION Indent( cSrc )
   RETURN "      " + StrTran( cSrc, Chr( 10 ), Chr( 10 ) + "      " )
