/*
 * colonias_common.prg — shared remote-connect + AdsCreateIndex flow.
 */
#include "ads.ch"
#include "rddsys.ch"

FUNCTION InicializarOpenADS( lVerbose )

   LOCAL lRet      := .T.
   LOCAL hConn     := 0
   LOCAL cUri      := GetEnv( "OADS_REMOTE_URI" )
   LOCAL cUser     := ""
   LOCAL cPass     := ""
   LOCAL nOptions  := 0
   LOCAL nError    := 0
   LOCAL cError    := Space( 200 )
   LOCAL cTbl      := "CCOLONIA.DBF"
   LOCAL lClose    := .F.

   IF lVerbose == NIL
      lVerbose := .F.
   ENDIF

   IF Empty( cUri )
      cUri := "tcp://127.0.0.1:6262/"
   ENDIF

   IF lVerbose
      ? "OpenADS — InicializarOpenADS (remoto)"
      ? "ACE:", AdsVersion()
      ? "URI:", cUri
   ENDIF

   AdsSetFileType( ADS_CDX )
   AdsSetServerType( ADS_REMOTE_SERVER )
   RddSetDefault( "ADSCDX" )

   IF ! AdsConnect60( cUri, ADS_REMOTE_SERVER, cUser, cPass, nOptions, @hConn )

      AdsGetLastError( @nError, @cError )
      IF lVerbose
         ? "AdsConnect60 Remota fallo."
         ? "Codigo:", hb_ValToStr( nError ), "Detalle:", AllTrim( cError )
      ENDIF
      lRet := .F.
   ELSEIF lVerbose
      ? "Conectado. Handle:", hConn
   ENDIF

   IF lRet

      AdsConnection( hConn )

      USE ( cTbl ) ALIAS CCOLONIA NEW EXCLUSIVE VIA "ADSCDX"

      IF Select( "CCOLONIA" ) > 0

         IF CCOLONIA->( OrdCount() ) == 0
            IF lVerbose
               ? "Creando indices con AdsCreateIndex..."
            ENDIF
            IF ! AdsCreateIndex( "CCOLONIA.CDX", "COLONIA", "COLONIA", "", 0 )
               IF lVerbose
                  ? "No se pudo crear el tag COLONIA."
               ENDIF
               lRet := .F.
            ENDIF
         ENDIF

         IF lRet .AND. CCOLONIA->( OrdCount() ) < 2
            IF ! AdsCreateIndex( "CCOLONIA.CDX", "NOMBRE", "NOMBRE", "", 0 )
               IF lVerbose
                  ? "No se pudo crear el tag NOMBRE."
               ENDIF
               lRet := .F.
            ENDIF
         ENDIF

         IF lRet
            CCOLONIA->( OrdSetFocus( "COLONIA" ) )
            CCOLONIA->( DBGoTop() )

            IF lVerbose
               ? "Indices OK. Registros:", CCOLONIA->( LastRec() )
               ? "Tag activo:", CCOLONIA->( OrdName() )
               ListColonias( 20 )
               lClose := .T.
            ENDIF
         ENDIF

      ELSE
         IF lVerbose
            ? "El servidor no pudo asignar el area de trabajo."
         ENDIF
         lRet := .F.
      ENDIF

   ENDIF

   IF ! lRet .AND. hConn > 0
      AdsDisconnect( hConn )
   ENDIF

   IF lClose
      CCOLONIA->( DbCloseArea() )
      AdsDisconnect( hConn )
   ENDIF

RETURN lRet

FUNCTION ListColonias( nMax )

   LOCAL n := 0, cLine

   IF nMax == NIL
      nMax := 20
   ENDIF

   ?
   ? "=== Catalogo de Colonias ==="
   ?

   CCOLONIA->( DBGoTop() )
   DO WHILE ! CCOLONIA->( Eof() ) .AND. n < nMax
      n++
      cLine := "  " + Str( n, 3 ) + "  " + ;
               PadR( AllTrim( CCOLONIA->COLONIA ), 20 ) + ;
               PadR( AllTrim( CCOLONIA->NOMBRE ), 30 ) + ;
               CCOLONIA->CP
      ? cLine
      CCOLONIA->( DBSkip() )
   ENDDO

   ? "Total:", n
   ?

RETURN NIL