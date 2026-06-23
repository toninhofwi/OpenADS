/* oa_ace.prg -- glue Harbour -> API ACE OpenADS (sem RDD).
 * Chama openace64.dll diretamente. Harbour e so hospedeiro/console. */

#pragma BEGINDUMP

#include "hbapi.h"
#include "hbapiitm.h"
#include "openads/ace.h"
#include <string.h>
#include <ctype.h>

#ifndef ADS_PROPRIETARY_LOCKING
#define ADS_PROPRIETARY_LOCKING 1
#endif

static int oa_open_struct_bag( ADSHANDLE hTable )
{
   char       szPath[ 1024 ];
   UNSIGNED16 usPLen = ( UNSIGNED16 ) ( sizeof( szPath ) - 5 );
   char *     pBase;
   char *     pDot;
   char *     pSep1;
   char *     pSep2;
   char *     pSep;
   ADSHANDLE  aHandles[ 64 ];
   UNSIGNED16 usLen = ( UNSIGNED16 ) ( sizeof( aHandles ) / sizeof( aHandles[ 0 ] ) );

   if( AdsGetTableFilename( hTable, ADS_FULLPATHNAME,
                            ( UNSIGNED8 * ) szPath, &usPLen ) != 0 || usPLen == 0 )
      return 0;
   pSep1 = strrchr( szPath, '\\' );
   pSep2 = strrchr( szPath, '/' );
   pSep  = pSep1 > pSep2 ? pSep1 : pSep2;
   pBase = ( pSep != NULL ) ? pSep + 1 : szPath;
   pDot  = strrchr( pBase, '.' );
   if( pDot != NULL )
      *pDot = '\0';
   strcat( pBase, ".cdx" );
   if( AdsOpenIndex( hTable, ( UNSIGNED8 * ) pBase, aHandles, &usLen ) != 0 )
      return 0;
   return ( int ) usLen;
}

HB_FUNC( OAA_CONNECT )
{
   ADSHANDLE    hConn = 0;
   const char * szUri = hb_parc( 1 );
   UNSIGNED32   ulRc  = 1;
   UNSIGNED16   usType = ADS_LOCAL_SERVER;

   if( szUri &&
       ( strncmp( szUri, "tcp://", 6 ) == 0 || strncmp( szUri, "tls://", 6 ) == 0 ) )
      usType = ADS_REMOTE_SERVER;
   if( szUri )
      ulRc = AdsConnect60( ( UNSIGNED8 * ) szUri, usType, NULL, NULL, 0, &hConn );
   hb_retnint( ulRc == 0 ? ( HB_MAXINT ) hConn : 0 );
}

HB_FUNC( OAA_DISCONNECT )
{
   hb_retl( AdsDisconnect( ( ADSHANDLE ) hb_parnint( 1 ) ) == 0 );
}

HB_FUNC( OAA_VERSION )
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

HB_FUNC( OAA_LASTERR )
{
   UNSIGNED32 ulCode = 0;
   char       szBuf[ 512 ];
   UNSIGNED16 usLen  = ( UNSIGNED16 ) sizeof( szBuf );

   AdsGetLastError( &ulCode, ( UNSIGNED8 * ) szBuf, &usLen );
   hb_retclen( szBuf, usLen );
}

HB_FUNC( OAA_CREATETABLE )
{
   ADSHANDLE    hTable = 0;
   const char * szName = hb_parc( 2 );
   const char * szDef  = hb_parc( 3 );
   UNSIGNED16   usType = ( UNSIGNED16 ) hb_parni( 4 );
   UNSIGNED32   ulRc   = 1;

   if( usType == 0 )
      usType = ADS_CDX;
   if( szName && szDef )
      ulRc = AdsCreateTable( ( ADSHANDLE ) hb_parnint( 1 ),
                             ( UNSIGNED8 * ) szName, NULL,
                             usType, ADS_ANSI, 0, 0, 64,
                             ( UNSIGNED8 * ) szDef, &hTable );
   hb_retnint( ulRc == 0 ? ( HB_MAXINT ) hTable : 0 );
}

HB_FUNC( OAA_OPENTABLE )
{
   ADSHANDLE    hTable = 0;
   const char * szName = hb_parc( 2 );
   UNSIGNED16   usType = ( UNSIGNED16 ) hb_parni( 3 );
   UNSIGNED32   ulRc   = 1;

   if( usType == 0 )
      usType = ADS_CDX;
   if( szName )
      ulRc = AdsOpenTable( ( ADSHANDLE ) hb_parnint( 1 ),
                           ( UNSIGNED8 * ) szName, NULL,
                           usType, ADS_ANSI, ADS_PROPRIETARY_LOCKING,
                           ADS_IGNORERIGHTS, ADS_EXCLUSIVE, &hTable );
   hb_retnint( ulRc == 0 ? ( HB_MAXINT ) hTable : 0 );
}

HB_FUNC( OAA_TABLECLOSE )
{
   hb_retl( AdsCloseTable( ( ADSHANDLE ) hb_parnint( 1 ) ) == 0 );
}

HB_FUNC( OAA_CREATEINDEX )
{
   ADSHANDLE    hIndex   = 0;
   const char * szTag    = hb_parc( 2 );
   const char * szExpr   = hb_parc( 3 );
   const char * szBag    = hb_parc( 4 );
   UNSIGNED8    szEmpty[] = { 0 };
   UNSIGNED8 *  pBag     = szEmpty;
   UNSIGNED32   ulRc     = 1;

   if( szBag && szBag[ 0 ] )
      pBag = ( UNSIGNED8 * ) szBag;
   if( szTag && szExpr )
      ulRc = AdsCreateIndex61( ( ADSHANDLE ) hb_parnint( 1 ),
                               pBag, ( UNSIGNED8 * ) szTag,
                               ( UNSIGNED8 * ) szExpr, NULL, NULL, 0, 0, &hIndex );
   hb_retl( ulRc == 0 );
}

HB_FUNC( OAA_OPENINDEX )
{
   ADSHANDLE    hTable = ( ADSHANDLE ) hb_parnint( 1 );
   const char * szBag  = hb_parc( 2 );
   ADSHANDLE    aHandles[ 64 ];
   UNSIGNED16   usLen  = ( UNSIGNED16 ) ( sizeof( aHandles ) / sizeof( aHandles[ 0 ] ) );
   UNSIGNED32   ulRc   = 1;

   if( szBag )
      ulRc = AdsOpenIndex( hTable, ( UNSIGNED8 * ) szBag, aHandles, &usLen );
   hb_retni( ulRc == 0 ? ( int ) usLen : 0 );
}

HB_FUNC( OAA_OPENIDXBAG )
{
   hb_retni( oa_open_struct_bag( ( ADSHANDLE ) hb_parnint( 1 ) ) );
}

HB_FUNC( OAA_GETINDEX )
{
   ADSHANDLE    hTable = ( ADSHANDLE ) hb_parnint( 1 );
   ADSHANDLE    hIndex = 0;
   const char * szTag  = hb_parc( 2 );
   UNSIGNED32   ulRc   = 1;

   if( szTag )
   {
      ulRc = AdsGetIndexHandle( hTable, ( UNSIGNED8 * ) szTag, &hIndex );
      if( ulRc != 0 )
      {
         oa_open_struct_bag( hTable );
         hIndex = 0;
         ulRc = AdsGetIndexHandle( hTable, ( UNSIGNED8 * ) szTag, &hIndex );
      }
   }
   hb_retnint( ulRc == 0 ? ( HB_MAXINT ) hIndex : 0 );
}

HB_FUNC( OAA_APPEND )
{
   hb_retl( AdsAppendRecord( ( ADSHANDLE ) hb_parnint( 1 ) ) == 0 );
}

HB_FUNC( OAA_WRITEREC )
{
   hb_retl( AdsWriteRecord( ( ADSHANDLE ) hb_parnint( 1 ) ) == 0 );
}

HB_FUNC( OAA_SETSTR )
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

HB_FUNC( OAA_SETNUM )
{
   const char * szField = hb_parc( 2 );
   UNSIGNED32   ulRc    = 1;

   if( szField )
      ulRc = AdsSetDouble( ( ADSHANDLE ) hb_parnint( 1 ),
                           ( UNSIGNED8 * ) szField, hb_parnd( 3 ) );
   hb_retl( ulRc == 0 );
}

HB_FUNC( OAA_GETSTR )
{
   ADSHANDLE  hTable = ( ADSHANDLE ) hb_parnint( 1 );
   UNSIGNED32 ulLen  = 8192;
   char *     pBuf   = ( char * ) hb_xgrab( ulLen + 1 );
   UNSIGNED32 ulRc   = AdsGetField( hTable, ( UNSIGNED8 * ) hb_parc( 2 ),
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

HB_FUNC( OAA_GETNUM )
{
   double       dVal    = 0;
   const char * szField = hb_parc( 2 );

   if( szField )
      AdsGetDouble( ( ADSHANDLE ) hb_parnint( 1 ),
                    ( UNSIGNED8 * ) szField, &dVal );
   hb_retnd( dVal );
}

HB_FUNC( OAA_RECORDCOUNT )
{
   UNSIGNED32   ulCnt = 0;
   AdsGetRecordCount( ( ADSHANDLE ) hb_parnint( 1 ), ADS_RESPECTSCOPES, &ulCnt );
   hb_retnl( ( long ) ulCnt );
}

HB_FUNC( OAA_GOTOP )
{
   hb_retl( AdsGotoTop( ( ADSHANDLE ) hb_parnint( 1 ) ) == 0 );
}

HB_FUNC( OAA_GOBOTTOM )
{
   hb_retl( AdsGotoBottom( ( ADSHANDLE ) hb_parnint( 1 ) ) == 0 );
}

HB_FUNC( OAA_SKIP )
{
   hb_retl( AdsSkip( ( ADSHANDLE ) hb_parnint( 1 ),
                     ( SIGNED32 ) hb_parnl( 2 ) ) == 0 );
}

HB_FUNC( OAA_EOF )
{
   UNSIGNED16 usEof = 0;
   AdsAtEOF( ( ADSHANDLE ) hb_parnint( 1 ), &usEof );
   hb_retl( usEof != 0 );
}

HB_FUNC( OAA_SEEKSTR )
{
   const char * szKey  = hb_parc( 2 );
   UNSIGNED16   bFound = 0;
   UNSIGNED32   ulRc   = 1;

   if( szKey )
      ulRc = AdsSeek( ( ADSHANDLE ) hb_parnint( 1 ),
                      ( UNSIGNED8 * ) szKey, ( UNSIGNED16 ) hb_parclen( 2 ),
                      ADS_STRINGKEY, ADS_HARDSEEK, &bFound );
   hb_retl( ulRc == 0 && bFound != 0 );
}

HB_FUNC( OAA_SEEKNUM )
{
   double     dKey   = hb_parnd( 2 );
   UNSIGNED16 bFound = 0;
   UNSIGNED32 ulRc   = AdsSeek( ( ADSHANDLE ) hb_parnint( 1 ),
                                ( UNSIGNED8 * ) &dKey, ( UNSIGNED16 ) sizeof( dKey ),
                                ADS_DOUBLEKEY, ADS_HARDSEEK, &bFound );
   hb_retl( ulRc == 0 && bFound != 0 );
}

HB_FUNC( OAA_ISFOUND )
{
   UNSIGNED16 bFound = 0;
   AdsIsFound( ( ADSHANDLE ) hb_parnint( 1 ), &bFound );
   hb_retl( bFound != 0 );
}

HB_FUNC( OAA_FLUSH )
{
   hb_retl( AdsFlushFileBuffers( ( ADSHANDLE ) hb_parnint( 1 ) ) == 0 );
}

HB_FUNC( OAA_GOTO )
{
   hb_retl( AdsGotoRecord( ( ADSHANDLE ) hb_parnint( 1 ),
                           ( UNSIGNED32 ) hb_parnl( 2 ) ) == 0 );
}

HB_FUNC( OAA_RECNO )
{
   UNSIGNED32 ulRec = 0;
   AdsGetRecordNum( ( ADSHANDLE ) hb_parnint( 1 ), 0, &ulRec );
   hb_retnl( ( long ) ulRec );
}

#pragma ENDDUMP