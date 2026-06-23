/*
 * all_backends_crud_bench.prg
 * ==================================================================
 * COOKBOOK / ORM track -- the "complete" example.
 *
 * Level: ADVANCED (but every step is commented for newcomers).
 *
 * One program, every back-end OpenADS reaches. It:
 *
 *   1. Discovers which back-ends are CONFIGURED (sqlite + dbf are
 *      built in; PostgreSQL / MariaDB / ODBC / a remote tcp:// server
 *      switch on only if you point an environment variable at them).
 *   2. Runs the SAME ORM CRUD cycle on each one -- the Harbour code is
 *      identical; only the connection string changes.
 *   3. Produces an AUDITABLE BENCHMARK: it times each operation, counts
 *      the rows, and prints a content CHECKSUM for every back-end. The
 *      seed data is identical everywhere, so the checksums MUST match;
 *      if a back-end disagrees you have caught a real correctness bug,
 *      not just a slow number. The timings are emitted as CSV so you
 *      can paste them into a spreadsheet and re-run to confirm.
 *
 * THE HEADLINE -- "seek vs scan":
 *   The very same `Model:Find( id )` call takes two different paths:
 *     * SQL back-ends (SQLite/PostgreSQL/MariaDB/ODBC): the engine
 *       resolves the primary key with an INDEXED lookup -- the cost
 *       barely moves as the table grows.
 *     * Navigational back-end (DBF): there is no SQL server, so the ORM
 *       WALKS the records (it honours the deletion flag row by row).
 *       That is a full SCAN: the cost grows with the table size.
 *   This benchmark makes that difference visible and measurable. (The
 *   engine itself now supports an O(log n) indexed seek on navigational
 *   tables too; a future ORM revision can adopt it. The stable ORM used
 *   here scans on purpose, to guarantee correct deletion semantics.)
 *
 * Data is invented (Ana / Bruno / Carla ...). No real records.
 *
 * Tunables (environment):
 *   BENCH_N        rows to insert per back-end   (default 500)
 *   BENCH_K        number of keyed lookups/edits (default 100)
 *   DEMO_PG_URI    e.g. postgresql://user:pass@host:5432/db
 *   DEMO_MARIA_URI e.g. mariadb://user:pass@host:3306/db
 *   DEMO_ODBC_URI  e.g. odbc://Driver={...};Server=...;Database=...
 *   DEMO_REMOTE_URI e.g. tcp://host:6262/  (an OpenADS server's data dir)
 *   BENCH_SQLITE_URI / BENCH_DBF_DIR  override the two built-in ones.
 *
 * Build & run: see README.md in this folder (needs the companion ORM
 * sources + an OpenADS DLL that routes sqlite://). The dbf back-end
 * needs no special build flag.
 * ==================================================================
 */

#include "hborm.ch"

/* A Model is just a class that names its table. The same class is used
 * against every back-end -- nothing in it is database-specific. */
CREATE CLASS Person FROM TORMModel
   METHOD TableName() INLINE "people"
END CLASS

/* ------------------------------------------------------------------ */
PROCEDURE Main()

   LOCAL aBackends, hB, aResults := {}, hR

   ? "OpenADS cookbook -- ORM CRUD + auditable benchmark (all back-ends)"
   ? "engine:", hbo_Version()
   ? "rows per back-end (BENCH_N):", LTrim( Str( BenchN() ) ), ;
     "  keyed ops (BENCH_K):", LTrim( Str( BenchK() ) )
   ?

   aBackends := BackendList()

   FOR EACH hB IN aBackends
      hR := RunBackend( hB )            // NIL if the back-end is absent
      IF hR != NIL
         AAdd( aResults, hR )
      ENDIF
   NEXT

   /* ---- the auditable report -------------------------------------- */
   ?
   ? "================ BENCHMARK (CSV) ================"
   EmitCsv( aResults )
   ?
   ? "================ SUMMARY ========================"
   EmitSummary( aResults )

   ? ""
   ? "Done."

   /* exit 0 only if at least one back-end ran AND every back-end that
    * ran agreed on the content checksum (the auditable invariant). */
   IF Len( aResults ) == 0 .OR. ! ChecksumsAgree( aResults ) .OR. ! DeletesOk( aResults )
      ErrorLevel( 1 )
   ENDIF
   RETURN

/* ================================================================== *
 *  Per-back-end CRUD + timing
 * ================================================================== */
STATIC FUNCTION RunBackend( hB )

   LOCAL oCn, nN := BenchN(), nK := BenchK()
   LOCAL nT0, hRes, aRows, nFound, aKeys, i, oP, nSel

   ? "---- " + hB[ "label" ] + "  (" + hB[ "uri" ] + ")"

   /* the dbf back-end connects to a folder we must prepare first */
   IF hB[ "kind" ] == "dbf"
      PrepareDir( hB[ "uri" ] )
   ELSEIF hB[ "kind" ] == "sqlite"
      PrepareFile( UriPath( hB[ "uri" ] ) )
   ENDIF

   oCn := TORMConnection():New( hB[ "uri" ] )
   IF ! oCn:IsOpen()
      ? "   SKIP -- connect failed:", hbo_LastErr()
      RETURN NIL
   ENDIF
   TORMConnection_Default( oCn )        // models use this connection

   /* fresh schema (DROP may fail if absent -- that is fine) */
   oCn:Execute( "DROP TABLE people" )
   IF ! oCn:Execute( "CREATE TABLE people ( id INTEGER, name VARCHAR(40), uf CHAR(2), active Logical )" )
      ? "   SKIP -- cannot create table:", hbo_LastErr()
      oCn:Close()
      RETURN NIL
   ENDIF

   hRes := { "label" => hB[ "label" ], "nav" => oCn:IsNavigational(), "rows" => nN }

   /* ---- 1. INSERT N (timed) --------------------------------------- *
    * Same ORM call on every back-end: Model:Create. On a SQL back-end
    * it emits an INSERT; on the navigational back-end it appends a
    * record through the cursor API. */
   nT0 := hb_MilliSeconds()
   FOR i := 1 TO nN
      Person():New():Create( SeedRow( i ) )
   NEXT
   hRes[ "insert_ms" ] := hb_MilliSeconds() - nT0

   /* ---- 2. SELECT all (timed) + content checksum ------------------ *
    * A plain SELECT with no WHERE/LIMIT returns every row on both the
    * SQL and navigational paths. We checksum the result so identical
    * seed data yields an identical, re-runnable number. */
   nT0 := hb_MilliSeconds()
   aRows := oCn:Query( "SELECT id, name, uf, active FROM people" )
   hRes[ "select_ms" ] := hb_MilliSeconds() - nT0
   nSel := Len( aRows )
   hRes[ "select_rows" ] := nSel
   hRes[ "checksum" ] := RowsChecksum( aRows )

   /* ---- 3. KEYED Find x K (timed) -- THE seek-vs-scan number ------ *
    * The identical Model:Find( id ) is an indexed lookup on SQL
    * back-ends and a full scan on the navigational one. We probe K
    * primary keys spread across the whole table so a scan pays its
    * average (~N/2 comparisons) cost. */
   aKeys := SpreadKeys( nN, nK )
   nFound := 0
   nT0 := hb_MilliSeconds()
   FOR EACH i IN aKeys
      oP := Person():New():Find( i )
      IF oP != NIL
         nFound++
      ENDIF
   NEXT
   hRes[ "find_ms" ]    := hb_MilliSeconds() - nT0
   hRes[ "find_ops" ]   := Len( aKeys )
   hRes[ "find_found" ] := nFound

   /* ---- 4. UPDATE x K (timed) ------------------------------------- */
   nT0 := hb_MilliSeconds()
   FOR EACH i IN aKeys
      oP := Person():New():Find( i )
      IF oP != NIL
         oP:Set( "uf", "ZZ" )
         oP:Save()
      ENDIF
   NEXT
   hRes[ "update_ms" ]  := hb_MilliSeconds() - nT0
   hRes[ "update_ops" ] := Len( aKeys )

   /* ---- 5. DELETE x K (timed) ------------------------------------- */
   nT0 := hb_MilliSeconds()
   FOR EACH i IN aKeys
      oP := Person():New():Find( i )
      IF oP != NIL
         oP:Delete()
      ENDIF
   NEXT
   hRes[ "delete_ms" ]  := hb_MilliSeconds() - nT0
   hRes[ "delete_ops" ] := Len( aKeys )

   /* ---- 6. verify the deletes the CORRECT way --------------------- *
    * The auditable invariant is "a deleted key is no longer findable".
    * Model:Find honours deletion on BOTH paths (SQL WHERE / nav scan),
    * so this check is uniform and reliable. (We deliberately do NOT
    * trust a raw `SELECT COUNT` over the navigational table: SQL-over-
    * DBF does not honour the deletion flag, so it would still see the
    * logically-deleted rows -- a documented engine quirk, not an ORM
    * error.) */
   nFound := 0
   FOR EACH i IN aKeys
      IF Person():New():Find( i ) == NIL
         nFound++                       // correctly gone
      ENDIF
   NEXT
   hRes[ "gone" ]        := nFound
   hRes[ "gone_expect" ] := DistinctCount( aKeys )

   /* ---- cleanup --------------------------------------------------- */
   oCn:Execute( "DROP TABLE people" )
   oCn:Close()

   ? "   inserted", LTrim( Str( nN ) ), "| selected", LTrim( Str( nSel ) ), ;
     "| found", LTrim( Str( nFound ) ), "/", LTrim( Str( Len( aKeys ) ) ), ;
     "| checksum", LTrim( Str( hRes[ "checksum" ] ) )
   RETURN hRes

/* ================================================================== *
 *  Back-end discovery
 * ================================================================== */
STATIC FUNCTION BackendList()
   LOCAL a := {}, c

   /* always-on: a local SQLite file (needs a DLL with OPENADS_WITH_SQLITE) */
   AAdd( a, { "label" => "sqlite", "kind" => "sqlite", ;
              "uri" => DefEnv( "BENCH_SQLITE_URI", "sqlite://./_bench.db" ) } )

   /* always-on: a DBF folder (navigational; no build flag needed) */
   AAdd( a, { "label" => "dbf", "kind" => "dbf", ;
              "uri" => DefEnv( "BENCH_DBF_DIR", "./_bench_dbf" ) } )

   /* opt-in SQL servers -- only if you give them a URI */
   c := GetEnv( "DEMO_PG_URI" )
   IF ! Empty( c ) ; AAdd( a, { "label" => "postgresql", "kind" => "sql", "uri" => c } ) ; ENDIF
   c := GetEnv( "DEMO_MARIA_URI" )
   IF ! Empty( c ) ; AAdd( a, { "label" => "mariadb", "kind" => "sql", "uri" => c } ) ; ENDIF
   c := GetEnv( "DEMO_ODBC_URI" )
   IF ! Empty( c ) ; AAdd( a, { "label" => "odbc", "kind" => "sql", "uri" => c } ) ; ENDIF
   c := GetEnv( "DEMO_REMOTE_URI" )
   IF ! Empty( c ) ; AAdd( a, { "label" => "remote-tcp", "kind" => "dbf", "uri" => c } ) ; ENDIF

   RETURN a

/* ================================================================== *
 *  Reporting
 * ================================================================== */
STATIC PROCEDURE EmitCsv( aResults )
   LOCAL hR
   ? "backend,nav,rows,operation,total_ms,per_op_ms,ops"
   FOR EACH hR IN aResults
      CsvLine( hR, "insert", hR[ "insert_ms" ], hR[ "rows" ] )
      CsvLine( hR, "select_all", hR[ "select_ms" ], 1 )
      CsvLine( hR, "find_pk", hR[ "find_ms" ], hR[ "find_ops" ] )
      CsvLine( hR, "update_pk", hR[ "update_ms" ], hR[ "update_ops" ] )
      CsvLine( hR, "delete_pk", hR[ "delete_ms" ], hR[ "delete_ops" ] )
   NEXT
   RETURN

STATIC PROCEDURE CsvLine( hR, cOp, nMs, nOps )
   LOCAL cPer := iif( nOps > 0, Str3( nMs / nOps ), "" )
   ? hR[ "label" ] + "," + iif( hR[ "nav" ], "yes", "no" ) + "," + ;
     LTrim( Str( hR[ "rows" ] ) ) + "," + cOp + "," + ;
     Str3( nMs ) + "," + cPer + "," + LTrim( Str( nOps ) )
   RETURN

STATIC PROCEDURE EmitSummary( aResults )
   LOCAL hR, lAllOk := .T.
   ? PadR( "backend", 12 ) + PadR( "path", 6 ) + ;
     PadL( "find/op(ms)", 13 ) + PadL( "del ok", 9 )
   FOR EACH hR IN aResults
      ? PadR( hR[ "label" ], 12 ) + ;
        PadR( iif( hR[ "nav" ], "scan", "seek" ), 6 ) + ;
        PadL( Str3( hR[ "find_ms" ] / Max( 1, hR[ "find_ops" ] ) ), 13 ) + ;
        PadL( iif( hR[ "gone" ] == hR[ "gone_expect" ], "yes", "NO" ), 9 )
      IF hR[ "gone" ] != hR[ "gone_expect" ]
         lAllOk := .F.
      ENDIF
   NEXT
   ?
   ? "checksums (identical seed -> must match across back-ends):"
   FOR EACH hR IN aResults
      ? "   " + PadR( hR[ "label" ], 12 ) + LTrim( Str( hR[ "checksum" ] ) )
   NEXT
   ? iif( ChecksumsAgree( aResults ), "   -> all checksums AGREE (auditable: correct)", ;
                                       "   -> MISMATCH: a back-end returned different data <<<" )
   ?
   ? "seek vs scan: compare the find/op column. SQL back-ends (path=seek)"
   ? "stay roughly flat as rows grow; the DBF back-end (path=scan) rises"
   ? "with the row count -- that is the cost the headline talks about."
   IF ! lAllOk
      ? "WARNING: a delete check failed (see 'del ok' = NO above)."
   ENDIF
   RETURN

STATIC FUNCTION DeletesOk( aResults )
   LOCAL hR
   FOR EACH hR IN aResults
      IF hR[ "gone" ] != hR[ "gone_expect" ]
         RETURN .F.
      ENDIF
   NEXT
   RETURN .T.

STATIC FUNCTION ChecksumsAgree( aResults )
   LOCAL nFirst, hR, lFirst := .T.
   FOR EACH hR IN aResults
      IF lFirst
         nFirst := hR[ "checksum" ] ; lFirst := .F.
      ELSEIF hR[ "checksum" ] != nFirst
         RETURN .F.
      ENDIF
   NEXT
   RETURN .T.

/* ================================================================== *
 *  Deterministic seed data + helpers
 * ================================================================== */
STATIC FUNCTION SeedRow( i )
   STATIC aNames := { "Ana", "Bruno", "Carla", "Davi", "Eva" }
   STATIC aUf    := { "SP", "RJ", "MG" }
   RETURN { "id"     => i, ;
            "name"   => aNames[ ( i % 5 ) + 1 ] + " #" + LTrim( Str( i ) ), ;
            "uf"     => aUf[ ( i % 3 ) + 1 ], ;
            "active" => ( i % 2 == 0 ) }

/* order-independent checksum: identical seed -> identical number on every
 * back-end, regardless of the order the SELECT happens to return rows in. */
STATIC FUNCTION RowsChecksum( aRows )
   LOCAL h, nSum := 0, cUf
   FOR EACH h IN aRows
      nSum += Val( hb_CStr( HGet( h, "id", "0" ) ) ) * 131
      nSum += Len( AllTrim( hb_CStr( HGet( h, "name", "" ) ) ) )
      cUf := AllTrim( hb_CStr( HGet( h, "uf", "" ) ) )
      IF ! Empty( cUf )
         nSum += Asc( cUf )
      ENDIF
   NEXT
   RETURN nSum

/* K primary keys evenly spread over 1..N (so a scan pays its average). */
STATIC FUNCTION SpreadKeys( nN, nK )
   LOCAL a := {}, i, nStep
   IF nK >= nN
      FOR i := 1 TO nN ; AAdd( a, i ) ; NEXT
      RETURN a
   ENDIF
   nStep := nN / nK
   FOR i := 1 TO nK
      AAdd( a, Int( ( i - 1 ) * nStep ) + 1 )
   NEXT
   RETURN a

STATIC FUNCTION DistinctCount( aKeys )
   LOCAL h := hb_Hash(), i
   FOR EACH i IN aKeys ; h[ i ] := .T. ; NEXT
   RETURN Len( h )

/* case-insensitive hash get: DBF field names may come back upper-cased */
STATIC FUNCTION HGet( h, cKey, xDef )
   IF hb_HHasKey( h, cKey )       ; RETURN h[ cKey ] ; ENDIF
   IF hb_HHasKey( h, Upper( cKey ) ) ; RETURN h[ Upper( cKey ) ] ; ENDIF
   IF hb_HHasKey( h, Lower( cKey ) ) ; RETURN h[ Lower( cKey ) ] ; ENDIF
   RETURN xDef

STATIC FUNCTION BenchN() ; RETURN Max( 1, Val( DefEnv( "BENCH_N", "500" ) ) )
STATIC FUNCTION BenchK() ; RETURN Max( 1, Val( DefEnv( "BENCH_K", "100" ) ) )

STATIC FUNCTION DefEnv( cVar, cDef )
   LOCAL c := GetEnv( cVar )
   RETURN iif( Empty( c ), cDef, c )

/* a 3-decimal numeric string, trimmed -- keeps the CSV tidy */
STATIC FUNCTION Str3( n ) ; RETURN LTrim( Str( n, 14, 3 ) )

/* strip a sqlite:// prefix to get the file path (for cleanup) */
STATIC FUNCTION UriPath( cUri )
   IF Lower( Left( cUri, 9 ) ) == "sqlite://"
      RETURN SubStr( cUri, 10 )
   ENDIF
   RETURN cUri

STATIC PROCEDURE PrepareFile( cFile )
   IF ! Empty( cFile ) .AND. File( cFile )
      FErase( cFile )
   ENDIF
   RETURN

STATIC PROCEDURE PrepareDir( cDir )
   IF ! hb_DirExists( cDir )
      hb_DirCreate( cDir )
   ENDIF
   /* remove any leftover table files from a previous run */
   AEval( hb_Directory( cDir + "/people.*" ), ;
          {| aF | FErase( cDir + "/" + aF[ 1 ] ) } )
   RETURN
