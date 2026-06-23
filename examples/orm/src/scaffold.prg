/* scaffold.prg -- Porta A (avancado): gera a string de um .prg com CREATE CLASS
   ... FROM TORMModel e Casts() preenchido a partir do catalogo. */
#include "hborm.ch"

FUNCTION ORM_Scaffold( cTable, oConn, hOpts )
   LOCAL aCols, cClass, cSrc, cNl := hb_eol()
   IF oConn == NIL
      oConn := TORMConnection_Default()
   ENDIF
   aCols  := ORM_Introspect( oConn, cTable )
   cClass := ScaffoldClassName( cTable )
   cSrc := "/* Gerado por ORM_Scaffold a partir da tabela '" + cTable + "'." + cNl
   cSrc += "   Edite a vontade: e um ponto de partida, nao um arquivo gerenciado. */" + cNl
   cSrc += '#include "hborm.ch"' + cNl + cNl
   cSrc += "CREATE CLASS " + cClass + " FROM TORMModel" + cNl
   cSrc += '   METHOD TableName()  INLINE "' + cTable + '"' + cNl
   cSrc += '   METHOD PrimaryKey() INLINE "' + ORM_PkFromCols( aCols ) + '"' + cNl
   cSrc += "   METHOD Casts()      INLINE " + CastsLiteral( aCols ) + cNl
   cSrc += "END CLASS" + cNl
   IF hOpts != NIL .AND. hb_HHasKey( hOpts, "path" )
      hb_MemoWrit( hOpts[ "path" ], cSrc )
   ENDIF
   RETURN cSrc

/* { "id" => "integer", "saldo" => "decimal:2", ... } */
STATIC FUNCTION CastsLiteral( aCols )
   LOCAL c, a := {}
   FOR EACH c IN aCols
      AAdd( a, '"' + c[ "nome" ] + '" => "' + c[ "cast" ] + '"' )
   NEXT
   RETURN "{ " + JoinComma( a ) + " }"

STATIC FUNCTION JoinComma( a )
   LOCAL c := "", i
   FOR i := 1 TO Len( a )
      c += iif( i == 1, "", ", " ) + a[ i ]
   NEXT
   RETURN c

/* nome de classe sanitizado: "T" + tabela validada e capitalizada */
STATIC FUNCTION ScaffoldClassName( cTable )
   LOCAL cName
   TORMGrammar():New():QuoteIdent( cTable )   // valida; aborta se identificador invalido
   cName := Lower( cTable )
   RETURN "T" + Upper( Left( cName, 1 ) ) + SubStr( cName, 2 )
