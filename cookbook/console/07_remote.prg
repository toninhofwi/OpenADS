/*
 * 07_remote.prg
 * ------------------------------------------------------------------
 * COOKBOOK / console track -- pure Harbour against OpenADS, NO ORM.
 *
 * Level: INTERMEDIATE.
 *
 * What it shows, end to end:
 *   1. Switch from the in-process (LOCAL) engine to a REMOTE server.
 *   2. Connect over TCP with AdsConnect60 + a `tcp://host:port/dir` URI.
 *   3. Open a table that lives on the SERVER and read it -- the exact
 *      same xBase verbs as the local examples, just over the wire.
 *
 * Examples 01-06 ran the engine inside this process against a local
 * folder. The very same engine can also run as a standalone daemon
 * (`openads_serverd`) that several clients reach over TCP. The ONLY
 * things that change on the client are:
 *      AdsSetServerType( ADS_REMOTE_SERVER )         // not LOCAL
 *      AdsConnect60( "tcp://host:port/datadir", ... ) // a URI, not a path
 * Once connected, USE / dbSkip / dbSeek behave identically.
 *
 * ---- Running it -------------------------------------------------
 * This example is a CLIENT, so a server must be up first. From the
 * built tools:
 *      openads_serverd --port 6262 --data <some-data-dir>
 * Put a table (e.g. people.dbf) in <some-data-dir>, then point this
 * client at it:
 *      set OADS_REMOTE_URI=tcp://127.0.0.1:6262/<some-data-dir>
 *      set OADS_REMOTE_TABLE=people
 *      07_remote.exe
 * With no env vars it tries the documented localhost default and, if
 * nothing answers, prints how to start the server and exits cleanly
 * (so it never hangs in an unattended build).
 *
 * Data is 100% invented. No real-world data.
 * ------------------------------------------------------------------
 */

#include "ads.ch"
#include "rddsys.ch"

REQUEST ADS, ADSCDX, ADSNTX

PROCEDURE Main()

   LOCAL cUri   := GetEnv( "OADS_REMOTE_URI" )
   LOCAL cTable := GetEnv( "OADS_REMOTE_TABLE" )
   LOCAL hConn  := 0
   LOCAL n      := 0

   IF Empty( cUri )
      cUri := "tcp://127.0.0.1:6262/"      // documented localhost default
   ENDIF
   IF Empty( cTable )
      cTable := "people"
   ENDIF

   ? "OpenADS cookbook -- 07 remote server (pure Harbour, over TCP)"
   ? "Engine version reported by the DLL:", AdsVersion()
   ? "Connecting to:", cUri
   ?

   /* ---- 1. ask for the REMOTE engine --------------------------- */
   AdsSetServerType( ADS_REMOTE_SERVER )
   AdsSetFileType( ADS_CDX )
   RddSetDefault( "ADSCDX" )

   /* ---- 2. connect over TCP ------------------------------------ */
   /* AdsConnect60( path, serverTypes, user, pass, options, @hConn ).
    * Returns .F. (rather than hanging) when no server is listening. */
   IF ! AdsConnect60( cUri, ADS_REMOTE_SERVER, "", "", , @hConn ) .OR. hConn == 0
      ? "Could not reach a server at", cUri
      ?
      ? "Start one first, e.g.:"
      ? "    openads_serverd --port 6262 --data <data-dir>"
      ? "put a '" + cTable + "' table in that folder, then re-run with:"
      ? "    set OADS_REMOTE_URI=tcp://127.0.0.1:6262/<data-dir>"
      ErrorLevel( 1 )
      QUIT
   ENDIF

   ? "Connected. Default connection handle:", AdsConnection()
   ?

   /* ---- 3. open a SERVER-side table and read it ---------------- */
   /* No path games on the client: the table name resolves inside the
    * server's data directory. Otherwise it is an ordinary work area. */
   USE ( cTable ) VIA "ADSCDX" NEW SHARED
   IF ! Used()
      ? "Could not open table '" + cTable + "' on the server."
      AdsDisconnect()
      ErrorLevel( 1 )
      QUIT
   ENDIF

   ? "Rows in server table '" + cTable + "':", LastRec()
   ? "First rows (streamed from the server):"
   dbGoTop()
   DO WHILE ! Eof() .AND. n < 10
      n++
      ? "  " + Str( n, 2 ) + "  " + ;
        AllTrim( hb_CStr( FieldGet( 1 ) ) ) + "  " + ;
        AllTrim( hb_CStr( FieldGet( 2 ) ) )
      dbSkip()
   ENDDO

   /* ---- 4. clean up -------------------------------------------- */
   dbCloseArea()
   AdsDisconnect()

   ? "Done."
   RETURN
