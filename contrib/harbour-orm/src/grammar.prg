/* grammar.prg -- renders a dialect-agnostic query AST into SQLite SQL. */
#include "hborm.ch"

CREATE CLASS TORMGrammar
   METHOD New() CONSTRUCTOR
   METHOD Compile( hAst )
   METHOD Quote( xVal )
   METHOD CompileWheres( aWheres )   // -> "" or " WHERE ..."
END CLASS

METHOD New() CLASS TORMGrammar
   RETURN Self

METHOD Quote( xVal ) CLASS TORMGrammar
   DO CASE
   CASE xVal == NIL
      RETURN "NULL"
   CASE HB_ISSTRING( xVal )
      RETURN "'" + StrTran( xVal, "'", "''" ) + "'"
   CASE HB_ISLOGICAL( xVal )
      RETURN iif( xVal, "1", "0" )
   CASE HB_ISDATE( xVal )
      RETURN "'" + iif( Empty( xVal ), "", DToC4( xVal ) ) + "'"
   CASE HB_ISNUMERIC( xVal )
      RETURN LTrim( Str( xVal ) )
   ENDCASE
   RETURN "'" + StrTran( hb_CStr( xVal ), "'", "''" ) + "'"

METHOD CompileWheres( aWheres ) CLASS TORMGrammar
   LOCAL c := "", a
   IF aWheres == NIL .OR. Len( aWheres ) == 0
      RETURN ""
   ENDIF
   FOR EACH a IN aWheres
      c += iif( Empty( c ), "", " AND " ) + ;
           a[ 1 ] + " " + a[ 2 ] + " " + ::Quote( a[ 3 ] )
   NEXT
   RETURN " WHERE " + c

METHOD Compile( hAst ) CLASS TORMGrammar
   LOCAL cType := hAst[ "type" ], cTable := hAst[ "table" ]
   LOCAL c, aCols, aVals, cK, a, cCols, cSel

   DO CASE
   CASE cType == "insert"
      aCols := {} ; aVals := {}
      FOR EACH cK IN hb_HKeys( hAst[ "values" ] )
         AAdd( aCols, cK )
         AAdd( aVals, ::Quote( hAst[ "values" ][ cK ] ) )
      NEXT
      RETURN "INSERT INTO " + cTable + " ( " + JoinStr( aCols, ", " ) + ;
             " ) VALUES ( " + JoinStr( aVals, ", " ) + " )"

   CASE cType == "select"
      cSel := iif( hAst[ "columns" ] == NIL .OR. Len( hAst[ "columns" ] ) == 0, ;
                   "*", JoinStr( hAst[ "columns" ], ", " ) )
      c := "SELECT " + cSel + " FROM " + cTable
      c += ::CompileWheres( hb_HGetDef( hAst, "wheres", NIL ) )
      IF hb_HGetDef( hAst, "orders", NIL ) != NIL .AND. Len( hAst[ "orders" ] ) > 0
         cCols := ""
         FOR EACH a IN hAst[ "orders" ]
            cCols += iif( Empty( cCols ), "", ", " ) + a[ 1 ] + " " + a[ 2 ]
         NEXT
         c += " ORDER BY " + cCols
      ENDIF
      IF hb_HGetDef( hAst, "limit", NIL ) != NIL
         c += " LIMIT " + LTrim( Str( hAst[ "limit" ] ) )
      ENDIF
      RETURN c

   CASE cType == "update"
      c := ""
      FOR EACH cK IN hb_HKeys( hAst[ "values" ] )
         c += iif( Empty( c ), "", ", " ) + cK + " = " + ::Quote( hAst[ "values" ][ cK ] )
      NEXT
      RETURN "UPDATE " + cTable + " SET " + c + ;
             ::CompileWheres( hb_HGetDef( hAst, "wheres", NIL ) )

   CASE cType == "delete"
      RETURN "DELETE FROM " + cTable + ::CompileWheres( hb_HGetDef( hAst, "wheres", NIL ) )
   ENDCASE
   RETURN ""

/* small helpers (file-local) */
STATIC FUNCTION JoinStr( a, cSep )
   LOCAL c := "", i
   FOR i := 1 TO Len( a )
      c += iif( i == 1, "", cSep ) + a[ i ]
   NEXT
   RETURN c

STATIC FUNCTION DToC4( d )
   RETURN StrZero( Year( d ), 4 ) + "-" + StrZero( Month( d ), 2 ) + "-" + StrZero( Day( d ), 2 )
