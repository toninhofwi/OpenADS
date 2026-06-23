/* hbo_ace.prg -- Harbour-callable glue over the ABI ACE entry points.
 *
 * The RDD ships Harbour wrappers for connect / SQL-statement / record-count,
 * but they report success while dropping the OUT statement and cursor handles
 * (they come back zero), so we call the ABI entry points directly here. Each
 * function is one ABI hop; cursor field reads use a single reusable buffer. */

#pragma BEGINDUMP

#include "hbapi.h"
#include "hbapiitm.h"
#include "openads/ace.h"
#include <string.h>

HB_FUNC( HBO_CONNECT )       /* ( cUri ) -> connection handle, 0 on failure */
{
   ADSHANDLE    hConn = 0;
   const char * szUri = hb_parc( 1 );
   UNSIGNED32   ulRc  = 1;
   /* Server type follows the URI scheme: tcp:// or tls:// are remote, every
      other form (sqlite://, a local directory, native files) is local. */
   UNSIGNED16   usType = 1;   /* ADS_LOCAL_SERVER */
   if( szUri &&
       ( strncmp( szUri, "tcp://", 6 ) == 0 || strncmp( szUri, "tls://", 6 ) == 0 ) )
      usType = 2;             /* ADS_REMOTE_SERVER */
   if( szUri )
      ulRc = AdsConnect60( ( UNSIGNED8 * ) szUri, usType,
                           NULL, NULL, 0, &hConn );
   hb_retnint( ulRc == 0 ? ( HB_MAXINT ) hConn : 0 );
}

HB_FUNC( HBO_DISCONNECT )    /* ( nConn ) -> .T./.F. */
{
   hb_retl( AdsDisconnect( ( ADSHANDLE ) hb_parnint( 1 ) ) == 0 );
}

HB_FUNC( HBO_SHOWDELETED )   /* ( lShow ) -> rc; .F. hides deleted rows (SET DELETED ON) */
{
   hb_retnl( ( long ) AdsShowDeleted( ( UNSIGNED16 ) ( hb_parl( 1 ) ? 1 : 0 ) ) );
}

HB_FUNC( HBO_VERSION )       /* () -> "major.minorletter" */
{
   UNSIGNED32 ulMajor = 0, ulMinor = 0;
   UNSIGNED8  ucLetter = 0;
   char       szDesc[ 128 ];
   UNSIGNED16 usLen = ( UNSIGNED16 ) sizeof( szDesc );
   char       szVer[ 32 ];
   AdsGetVersion( &ulMajor, &ulMinor, &ucLetter, ( UNSIGNED8 * ) szDesc, &usLen );
   hb_snprintf( szVer, sizeof( szVer ), "%u.%u%c",
                ( unsigned ) ulMajor, ( unsigned ) ulMinor, ( char ) ucLetter );
   hb_retc( szVer );
}

HB_FUNC( HBO_LASTERR )       /* () -> last error text */
{
   UNSIGNED32 ulCode = 0;
   char       szBuf[ 512 ];
   UNSIGNED16 usLen  = ( UNSIGNED16 ) sizeof( szBuf );
   AdsGetLastError( &ulCode, ( UNSIGNED8 * ) szBuf, &usLen );
   hb_retclen( szBuf, usLen );
}

HB_FUNC( HBO_STMTNEW )       /* ( nConn ) -> statement handle, 0 on failure */
{
   ADSHANDLE  hStmt = 0;
   UNSIGNED32 ulRc  = AdsCreateSQLStatement( ( ADSHANDLE ) hb_parnint( 1 ), &hStmt );
   hb_retnint( ulRc == 0 ? ( HB_MAXINT ) hStmt : 0 );
}

HB_FUNC( HBO_EXECDIRECT )    /* ( nStmt, cSql, @nCursor ) -> .T./.F. */
{
   ADSHANDLE    hCur  = 0;
   const char * szSql = hb_parc( 2 );
   UNSIGNED32   ulRc  = 1;
   if( szSql )
      ulRc = AdsExecuteSQLDirect( ( ADSHANDLE ) hb_parnint( 1 ),
                                  ( UNSIGNED8 * ) szSql, &hCur );
   hb_stornint( ( HB_MAXINT ) hCur, 3 );
   hb_retl( ulRc == 0 );
}

HB_FUNC( HBO_STMTCLOSE )     /* ( nStmt ) -> rc */
{
   hb_retnl( ( long ) AdsCloseSQLStatement( ( ADSHANDLE ) hb_parnint( 1 ) ) );
}

HB_FUNC( HBO_TABLECLOSE )    /* ( nCursor ) -> rc */
{
   hb_retnl( ( long ) AdsCloseTable( ( ADSHANDLE ) hb_parnint( 1 ) ) );
}

HB_FUNC( HBO_GOTOP )         /* ( nCursor ) -> rc */
{
   hb_retnl( ( long ) AdsGotoTop( ( ADSHANDLE ) hb_parnint( 1 ) ) );
}

HB_FUNC( HBO_SKIP )          /* ( nCursor, nRecs ) -> rc */
{
   hb_retnl( ( long ) AdsSkip( ( ADSHANDLE ) hb_parnint( 1 ),
                               ( SIGNED32 ) hb_parnl( 2 ) ) );
}

HB_FUNC( HBO_EOF )           /* ( nCursor ) -> .T./.F. */
{
   UNSIGNED16 usEof = 0;
   AdsAtEOF( ( ADSHANDLE ) hb_parnint( 1 ), &usEof );
   hb_retl( usEof != 0 );
}

HB_FUNC( HBO_NUMFIELDS )     /* ( nCursor ) -> field count */
{
   UNSIGNED16 usFields = 0;
   AdsGetNumFields( ( ADSHANDLE ) hb_parnint( 1 ), &usFields );
   hb_retni( ( int ) usFields );
}

HB_FUNC( HBO_FIELDNAME )     /* ( nCursor, nPos ) -> column name */
{
   char       szName[ 256 ];
   UNSIGNED16 usLen = ( UNSIGNED16 ) sizeof( szName );
   if( AdsGetFieldName( ( ADSHANDLE ) hb_parnint( 1 ),
                        ( UNSIGNED16 ) hb_parni( 2 ),
                        ( UNSIGNED8 * ) szName, &usLen ) == 0 )
      hb_retclen( szName, usLen );
   else
      hb_retc( "" );
}

HB_FUNC( HBO_FIELD )         /* ( nCursor, cName ) -> value (trimmed string) */
{
   ADSHANDLE  hCursor = ( ADSHANDLE ) hb_parnint( 1 );
   UNSIGNED32 ulLen   = 8192;
   char *     pBuf    = ( char * ) hb_xgrab( ulLen + 1 );
   UNSIGNED32 ulRc    = AdsGetField( hCursor, ( UNSIGNED8 * ) hb_parc( 2 ),
                                     ( UNSIGNED8 * ) pBuf, &ulLen, 0 );
   if( ulRc == 0 )
   {
      while( ulLen > 0 && pBuf[ ulLen - 1 ] == ' ' )
         ulLen--;
      hb_retclen( pBuf, ulLen );
   }
   else
      hb_retc( "" );
   hb_xfree( pBuf );
}

/* ---- navigational path (USE/APPEND/SEEK over native tables) -------------- *
 * For DBF/ADS and the navigational-only backends (pg/maria/odbc) the ORM
 * drives a real table cursor instead of emitting SQL: a scan honors SET
 * DELETED (which the SQL-over-xBase WHERE does not -- see findings F3), and
 * these are the only entry points reachable on the navigational-only
 * backends.  Numeric values must go through AdsSetDouble (the string encoder
 * left-justifies into a numeric field); logicals through AdsSetLogical. */

HB_FUNC( HBO_OPENTABLE )     /* ( nConn, cName ) -> table handle, 0 on failure */
{
   ADSHANDLE    hTable = 0;
   const char * szName = hb_parc( 2 );
   UNSIGNED32   ulRc   = 1;
   if( szName )
      ulRc = AdsOpenTable( ( ADSHANDLE ) hb_parnint( 1 ),
                           ( UNSIGNED8 * ) szName, NULL,
                           ADS_CDX, ADS_ANSI, ADS_PROPRIETARY_LOCKING,
                           ADS_IGNORERIGHTS, ADS_SHARED, &hTable );
   hb_retnint( ulRc == 0 ? ( HB_MAXINT ) hTable : 0 );
}

HB_FUNC( HBO_APPEND )        /* ( nTable ) -> .T./.F. */
{
   hb_retl( AdsAppendRecord( ( ADSHANDLE ) hb_parnint( 1 ) ) == 0 );
}

HB_FUNC( HBO_WRITEREC )      /* ( nTable ) -> .T./.F. */
{
   hb_retl( AdsWriteRecord( ( ADSHANDLE ) hb_parnint( 1 ) ) == 0 );
}

HB_FUNC( HBO_DELETEREC )     /* ( nTable ) -> .T./.F. */
{
   hb_retl( AdsDeleteRecord( ( ADSHANDLE ) hb_parnint( 1 ) ) == 0 );
}

HB_FUNC( HBO_ISDELETED )     /* ( nTable ) -> .T. if current record is deleted */
{
   UNSIGNED16 usDel = 0;
   AdsIsRecordDeleted( ( ADSHANDLE ) hb_parnint( 1 ), &usDel );
   hb_retl( usDel != 0 );
}

HB_FUNC( HBO_SETSTR )        /* ( nTable, cField, cVal ) -> .T./.F. */
{
   const char * szField = hb_parc( 2 );
   UNSIGNED32   ulRc    = 1;
   if( szField )
      ulRc = AdsSetString( ( ADSHANDLE ) hb_parnint( 1 ),
                           ( UNSIGNED8 * ) szField,
                           ( UNSIGNED8 * ) hb_parc( 3 ),
                           ( UNSIGNED32 ) hb_parclen( 3 ) );
   hb_retl( ulRc == 0 );
}

HB_FUNC( HBO_SETNUM )        /* ( nTable, cField, nVal ) -> .T./.F. */
{
   const char * szField = hb_parc( 2 );
   UNSIGNED32   ulRc    = 1;
   if( szField )
      ulRc = AdsSetDouble( ( ADSHANDLE ) hb_parnint( 1 ),
                           ( UNSIGNED8 * ) szField, hb_parnd( 3 ) );
   hb_retl( ulRc == 0 );
}

HB_FUNC( HBO_SETLOG )        /* ( nTable, cField, lVal ) -> .T./.F. */
{
   const char * szField = hb_parc( 2 );
   UNSIGNED32   ulRc    = 1;
   if( szField )
      ulRc = AdsSetLogical( ( ADSHANDLE ) hb_parnint( 1 ),
                            ( UNSIGNED8 * ) szField,
                            ( UNSIGNED16 ) ( hb_parl( 3 ) ? 1 : 0 ) );
   hb_retl( ulRc == 0 );
}

#pragma ENDDUMP
