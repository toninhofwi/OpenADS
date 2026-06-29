/*
 * colonias_console.prg — headless smoke: remote TCP + AdsCreateIndex.
 */
#include "ads.ch"

REQUEST ADSCDX

PROCEDURE Main()

   IF ! InicializarOpenADS( .T. )  /* verbose console log */
      ? "FALLA: InicializarOpenADS"
      ErrorLevel( 1 )
   ELSE
      ErrorLevel( 0 )
   ENDIF

   RETURN