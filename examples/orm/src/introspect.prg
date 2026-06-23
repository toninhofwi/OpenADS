/* introspect.prg -- le o catalogo de uma tabela e deriva tokens de cast.
   Despacha por capacidade da conexao: SQL (sqlite/PRAGMA) ou navegacional. */
#include "hborm.ch"
#include "error.ch"

FUNCTION ORM_Introspect( oConn, cTable )
   IF oConn == NIL
      oConn := TORMConnection_Default()
   ENDIF
   IF oConn == NIL .OR. ! oConn:IsOpen()
      IntrospectRaise( "conexao indisponivel" )
   ENDIF
   IF oConn:IsNavigational()
      RETURN IntrospectNav( oConn, cTable )       // estrategia real na Task 4
   ENDIF
   RETURN IntrospectSql( oConn, cTable )

/* ---- estrategia navegacional: dicionario de campos via ACE --------------- */
STATIC FUNCTION IntrospectNav( oConn, cTable )
   LOCAL nTbl, aCols := {}, n, i, cName, cTok, lHasId := .F., c
   nTbl := hbo_OpenTable( oConn:Handle(), cTable )
   IF nTbl == 0
      IntrospectRaise( "tabela nao abre (navegacional)" )
      RETURN {}
   ENDIF
   n := hbo_NumFields( nTbl )
   FOR i := 1 TO n
      cName := AllTrim( hbo_FieldName( nTbl, i ) )
      cTok  := NavTypeToken( hbo_FieldType( nTbl, cName ), ;
                             hbo_FieldDecimals( nTbl, cName ) )
      AAdd( aCols, { "nome" => cName, "cast" => cTok, "pk" => .F. } )
      IF Lower( cName ) == "id"
         lHasId := .T.
      ENDIF
   NEXT
   hbo_TableClose( nTbl )
   /* PK: 'id' se existir, senao a 1a coluna */
   IF Len( aCols ) > 0
      IF lHasId
         FOR EACH c IN aCols
            IF Lower( c[ "nome" ] ) == "id"
               c[ "pk" ] := .T.
               EXIT
            ENDIF
         NEXT
      ELSE
         aCols[ 1 ][ "pk" ] := .T.
      ENDIF
   ENDIF
   RETURN aCols

/* tag ACE (+ decimais) -> token de cast */
STATIC FUNCTION NavTypeToken( cTag, nDec )
   DO CASE
   CASE cTag == "N" ; RETURN iif( nDec > 0, "decimal:" + LTrim( Str( Int( nDec ) ) ), "integer" )
   CASE cTag == "L" ; RETURN "boolean"
   CASE cTag == "D" ; RETURN "date"
   CASE cTag == "T" ; RETURN "datetime"
   ENDCASE
   RETURN "string"

FUNCTION ORM_CastsFromCols( aCols )
   LOCAL h := hb_Hash(), c
   FOR EACH c IN aCols
      h[ c[ "nome" ] ] := c[ "cast" ]
   NEXT
   RETURN h

FUNCTION ORM_PkFromCols( aCols )
   LOCAL c
   FOR EACH c IN aCols
      IF c[ "pk" ]
         RETURN c[ "nome" ]
      ENDIF
   NEXT
   RETURN "id"

/* ---- estrategia SQL: catalogo declarado via PRAGMA table_info ------------ */
STATIC FUNCTION IntrospectSql( oConn, cTable )
   LOCAL aCols := {}, aRows, hR, cIdent
   cIdent := TORMGrammar():New():QuoteIdent( cTable )    // valida; aborta se junk
   /* preferir o parametro vinculado; cair p/ a forma de comando com
      identificador JA validado (sem valor cru) se o passthrough nao popular */
   aRows := oConn:Query( "SELECT name, type, pk FROM pragma_table_info(:p1)", ;
                         { { "p1", cTable } } )
   IF Len( aRows ) == 0
      aRows := oConn:Query( "PRAGMA table_info(" + cIdent + ")" )
   ENDIF
   IF Len( aRows ) == 0
      IntrospectRaise( "tabela sem catalogo" )
   ENDIF
   FOR EACH hR IN aRows
      AAdd( aCols, { ;
         "nome" => AllTrim( hb_HGetDef( hR, "name", "" ) ), ;
         "cast" => SqlTypeToken( hb_HGetDef( hR, "type", "" ) ), ;
         "pk"   => ( Val( hb_CStr( hb_HGetDef( hR, "pk", "0" ) ) ) > 0 ) } )
   NEXT
   RETURN aCols

/* tipo declarado (texto) -> token de cast. Ordem: DATETIME antes de DATE. */
STATIC FUNCTION SqlTypeToken( cDecl )
   LOCAL cU := Upper( AllTrim( hb_CStr( cDecl ) ) )
   DO CASE
   CASE "INT" $ cU                                  ; RETURN "integer"
   CASE "BOOL" $ cU                                 ; RETURN "boolean"
   CASE "DATETIME" $ cU .OR. "TIMESTAMP" $ cU       ; RETURN "datetime"
   CASE "DATE" $ cU                                 ; RETURN "date"
   CASE "DEC" $ cU .OR. "NUMERIC" $ cU              ; RETURN DecToken( cU )
   CASE "REAL" $ cU .OR. "FLOA" $ cU .OR. "DOUB" $ cU ; RETURN "decimal"
   ENDCASE
   RETURN "string"

/* "DECIMAL(12,2)" -> "decimal:2"; sem escala util -> "decimal" */
STATIC FUNCTION DecToken( cU )
   LOCAL nOpen := At( "(", cU ), cInner, aPS, nScale
   IF nOpen == 0
      RETURN "decimal"
   ENDIF
   cInner := StrTran( SubStr( cU, nOpen + 1 ), ")", "" )
   aPS    := hb_ATokens( cInner, "," )
   IF Len( aPS ) >= 2
      nScale := Int( Val( aPS[ 2 ] ) )
      RETURN iif( nScale > 0, "decimal:" + LTrim( Str( nScale ) ), "decimal" )
   ENDIF
   RETURN "decimal"

STATIC PROCEDURE IntrospectRaise( cWhy )
   LOCAL oErr := ErrorNew()
   oErr:Subsystem( "hb_orm" )
   oErr:SubCode( 1010 )
   oErr:Severity( ES_ERROR )
   oErr:Description( "introspeccao falhou: " + cWhy )   // sem SQL/path
   oErr:Operation( "ORM_Introspect" )
   oErr:CanRetry( .F. )
   Eval( ErrorBlock(), oErr )
   RETURN
