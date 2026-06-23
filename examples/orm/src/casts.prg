/* casts.prg -- coercao pura de um valor para um token de tipo declarado.
   Sem dependencia de banco: testavel isolada. NIL passa reto em todo token.
   Aliases PT sao sinonimos finos do mesmo token. */
#include "hborm.ch"
#include "error.ch"

FUNCTION ORM_Cast( xVal, cToken )
   LOCAL cBase, nScale, aPart
   IF xVal == NIL
      RETURN NIL
   ENDIF
   cToken := Lower( AllTrim( hb_defaultValue( cToken, "string" ) ) )
   aPart  := hb_ATokens( cToken, ":" )
   cBase  := aPart[ 1 ]
   nScale := iif( Len( aPart ) >= 2, Val( aPart[ 2 ] ), -1 )

   DO CASE
   CASE cBase == "integer"  .OR. cBase == "inteiro"
      RETURN Int( ToNum( xVal ) )
   CASE cBase == "decimal"
      RETURN iif( nScale >= 0, Round( ToNum( xVal ), nScale ), ToNum( xVal ) )
   CASE cBase == "boolean"  .OR. cBase == "logico"
      RETURN ToBool( xVal )
   CASE cBase == "date"     .OR. cBase == "data"
      RETURN ToDate( xVal )
   CASE cBase == "datetime" .OR. cBase == "datahora"
      RETURN ToStamp( xVal )
   CASE cBase == "string"   .OR. cBase == "texto"
      RETURN iif( HB_ISSTRING( xVal ), xVal, AllTrim( hb_CStr( xVal ) ) )
   ENDCASE

   CastRaise( cToken )
   RETURN xVal   /* nao alcancado: CastRaise levanta */

STATIC FUNCTION ToNum( x )
   RETURN iif( HB_ISNUMERIC( x ), x, iif( HB_ISSTRING( x ), Val( x ), 0 ) )

STATIC FUNCTION ToBool( x )
   LOCAL c
   IF HB_ISLOGICAL( x ) ; RETURN x ; ENDIF
   IF HB_ISNUMERIC( x ) ; RETURN x != 0 ; ENDIF
   IF HB_ISSTRING( x )
      c := Upper( AllTrim( x ) )
      RETURN ! Empty( c ) .AND. ;
         ( c == "T" .OR. c == ".T." .OR. c == "1" .OR. c == "Y" .OR. ;
           c == "S" .OR. c == "TRUE" )
   ENDIF
   RETURN .F.

STATIC FUNCTION ToDate( x )
   IF HB_ISDATE( x )   ; RETURN x ; ENDIF
   IF HB_ISSTRING( x ) ; RETURN hb_SToD( StripDate( x ) ) ; ENDIF
   RETURN hb_SToD( "" )

STATIC FUNCTION StripDate( c )   /* "2026-06-21" ou "20260621" -> "20260621" */
   c := AllTrim( c )
   c := StrTran( StrTran( c, "-", "" ), "/", "" )
   RETURN Left( c, 8 )

STATIC FUNCTION ToStamp( x )
   IF HB_ISDATETIME( x ) ; RETURN x ; ENDIF
   IF HB_ISDATE( x )     ; RETURN x ; ENDIF
   IF HB_ISSTRING( x )   ; RETURN hb_CToT( AllTrim( x ), "YYYY-MM-DD HH:MM:SS" ) ; ENDIF
   RETURN hb_CToT( "", "YYYY-MM-DD HH:MM:SS" )

STATIC PROCEDURE CastRaise( cToken )
   LOCAL oErr := ErrorNew()
   oErr:Subsystem( "hb_orm" )
   oErr:SubCode( 1003 )
   oErr:Severity( ES_ERROR )
   oErr:Description( "cast token desconhecido: " + cToken )
   oErr:Operation( "ORM_Cast" )
   oErr:CanRetry( .F. )
   Eval( ErrorBlock(), oErr )
   RETURN
