/*
 * OpenADS Harbour extension — AdsCreateIndex() for contrib/rddads.
 *
 * Linked standalone until rddads-compat.patch is applied to Harbour's
 * contrib/rddads/adsfunc.c and rddads is rebuilt. Do not link this file
 * together with a patched rddads (duplicate HB_FUNC symbol).
 *
 * Harbour signature:
 *   AdsCreateIndex( cBagFile, cTag, cExpr, [cFor], [nOptions] ) -> lOk
 */
#include "hbapi.h"
#include "hbapirdd.h"
#include "rddads.h"

HB_FUNC( ADSCREATEINDEX )
{
   ADSAREAP     pArea    = hb_adsGetWorkAreaPointer();
   const char * szFile   = hb_parc( 1 );
   const char * szTag    = hb_parc( 2 );
   const char * szExpr   = hb_parc( 3 );
   const char * szFor    = hb_parc( 4 );
   UNSIGNED32   ulOpts   = ( UNSIGNED32 ) hb_parnint( 5 );
   ADSHANDLE    hIndex   = 0;
   UNSIGNED32   ulRetVal;

   if( ! pArea || pArea->hTable == 0 )
   {
      hb_retl( HB_FALSE );
      return;
   }

   if( szFile )
   {
      HB_SIZE nLen = strlen( szFile );
      if( nLen >= 4 &&
          szFile[ nLen - 4 ] == '.' &&
          ( szFile[ nLen - 3 ] == 'c' || szFile[ nLen - 3 ] == 'C' ) &&
          ( szFile[ nLen - 2 ] == 'd' || szFile[ nLen - 2 ] == 'D' ) &&
          ( szFile[ nLen - 1 ] == 'x' || szFile[ nLen - 1 ] == 'X' ) )
         ulOpts |= ADS_COMPOUND;
   }

#if ADS_LIB_VERSION >= 610
   ulRetVal = AdsCreateIndex61( pArea->hTable,
                                ( UNSIGNED8 * ) HB_UNCONST( szFile ),
                                ( UNSIGNED8 * ) HB_UNCONST( szTag ),
                                ( UNSIGNED8 * ) HB_UNCONST( szExpr ),
                                ( szFor && *szFor ) ?
                                   ( UNSIGNED8 * ) HB_UNCONST( szFor ) : NULL,
                                NULL,
                                ulOpts,
                                ADS_DEFAULT,
                                &hIndex );
#else
   ulRetVal = AdsCreateIndex( pArea->hTable,
                              ( UNSIGNED8 * ) HB_UNCONST( szFile ),
                              ( UNSIGNED8 * ) HB_UNCONST( szTag ),
                              ( UNSIGNED8 * ) HB_UNCONST( szExpr ),
                              ( szFor && *szFor ) ?
                                 ( UNSIGNED8 * ) HB_UNCONST( szFor ) : NULL,
                              ulOpts,
                              ADS_DEFAULT,
                              &hIndex );
#endif

   if( ulRetVal == AE_SUCCESS )
      pArea->hOrdCurrent = hIndex;

   hb_retl( ulRetVal == AE_SUCCESS );
}