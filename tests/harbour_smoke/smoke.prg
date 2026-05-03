/* OpenADS / Harbour rddads smoke test.
 *
 * Goal: prove the OpenADS-shipped ace64.dll resolves every symbol
 * Harbour's contrib/rddads expects and that the program at least
 * launches. Real DBF/CDX exercise lands in a follow-up.
 */
#include "ads.ch"

REQUEST ADS, ADSCDX, ADSNTX

PROCEDURE Main()
   ? "OpenADS smoke test"
   ? "rddads version probe..."

   /* AdsVersion() is rddads' Harbour-side wrapper — it calls
    * AdsGetVersion() in the ACE DLL on PATH and returns the assembled
    * string. Resolving this symbol proves the rddads.lib link found
    * every Ads* it expected (otherwise hbmk2 fails earlier). */
   ? "ACE DLL reports:", AdsVersion()

   RETURN
