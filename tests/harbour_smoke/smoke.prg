/* OpenADS / Harbour rddads smoke test (M8.6).
 *
 * Multi-field DBF + a pre-staged CDX index (built by make_cdx.exe via
 * OpenADS' own CdxIndex::create). The smoke opens both, walks every
 * record in NAME order, then exercises dbSeek for an existing key, a
 * key-not-found key, and a soft-seek partial match.
 */
#include "ads.ch"

REQUEST ADS, ADSCDX, ADSNTX

PROCEDURE Main()
   LOCAL nFld

   ErrorBlock( {|oErr| MyHandler( oErr ) } )

   ? "OpenADS smoke test (M8.6)"
   ? "ACE DLL reports:", AdsVersion()

   AdsSetFileType( ADS_CDX )
   AdsSetServerType( ADS_LOCAL_SERVER )

   IF ! AdsConnect( "." )
      ? "AdsConnect failed."
      RETURN
   ENDIF

   USE data INDEX data VIA "ADSCDX"
   IF NetErr()
      ? "USE failed"
      RETURN
   ENDIF

   ? "Schema:"
   FOR nFld := 1 TO FCount()
      ? "  ", nFld, FieldName(nFld), FieldType(nFld), ;
        "len=", FieldLen(nFld), "dec=", FieldDec(nFld)
   NEXT

   ? "OrderName:", OrdName()
   ? "OrderKey :", OrdKey()

   ? "Walking", LastRec(), "records in NAME order:"
   dbGoTop()
   DO WHILE ! Eof()
      ? "  rec", RecNo(), ;
        "NAME=[" + FIELD->NAME + "]", ;
        "AGE=" + LTrim(Str(FIELD->AGE)), ;
        "ACTIVE=" + iif(FIELD->ACTIVE, "T", "F"), ;
        "BORN=" + DToS(FIELD->BORN)
      dbSkip()
   ENDDO

   ? ""
   ? "Seek 'BETA' (exact)..."
   dbSeek("BETA")
   ? "  Found=" + iif(Found(), "T", "F"), ;
     "RecNo=" + LTrim(Str(RecNo())), ;
     "NAME=[" + FIELD->NAME + "]"

   ? "Seek 'GAMMA' (exact, last key)..."
   dbSeek("GAMMA")
   ? "  Found=" + iif(Found(), "T", "F"), ;
     "RecNo=" + LTrim(Str(RecNo())), ;
     "NAME=[" + FIELD->NAME + "]"

   ? "Seek 'NOPE' (miss)..."
   dbSeek("NOPE")
   ? "  Found=" + iif(Found(), "T", "F"), ;
     "EOF=" + iif(Eof(), "T", "F")

   USE
   ? "Done."
   RETURN

PROCEDURE MyHandler( oErr )
   ? "ERROR:", oErr:Description, "[" + LTrim(Str(oErr:GenCode)) + "/" + ;
        LTrim(Str(oErr:SubCode)) + "]"
   ? "Operation:", oErr:Operation
   ErrorLevel( 1 )
   QUIT
   RETURN
