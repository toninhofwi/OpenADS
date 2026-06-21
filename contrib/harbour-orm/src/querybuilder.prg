/* querybuilder.prg -- fluent builder producing the query AST. */
#include "hborm.ch"

CREATE CLASS TORMQuery
   DATA oConn
   DATA cTable   INIT ""
   DATA aColumns INIT NIL
   DATA aWheres  INIT {}
   DATA aOrders  INIT {}
   DATA nLimit   INIT NIL
   METHOD New( oConn, cTable ) CONSTRUCTOR
   METHOD Select( aCols )  INLINE ( ::aColumns := aCols, Self )
   METHOD Where( cCol, xOpOrVal, xVal )
   METHOD OrderBy( cCol, cDir )
   METHOD Limit( n )       INLINE ( ::nLimit := n, Self )
   METHOD ToAst()
   METHOD ToSql()
   METHOD Get()
END CLASS

METHOD New( oConn, cTable ) CLASS TORMQuery
   ::oConn  := oConn
   ::cTable := cTable
   RETURN Self

METHOD Where( cCol, xOpOrVal, xVal ) CLASS TORMQuery
   IF PCount() == 2          // Where( col, val ) -> "="
      AAdd( ::aWheres, { cCol, "=", xOpOrVal } )
   ELSE
      AAdd( ::aWheres, { cCol, xOpOrVal, xVal } )
   ENDIF
   RETURN Self

METHOD OrderBy( cCol, cDir ) CLASS TORMQuery
   AAdd( ::aOrders, { cCol, iif( cDir == NIL, "ASC", Upper( cDir ) ) } )
   RETURN Self

METHOD ToAst() CLASS TORMQuery
   RETURN { "type"    => "select", ;
            "table"   => ::cTable, ;
            "columns" => ::aColumns, ;
            "wheres"  => ::aWheres, ;
            "orders"  => ::aOrders, ;
            "limit"   => ::nLimit }

METHOD ToSql() CLASS TORMQuery
   RETURN TORMGrammar():New():Compile( ::ToAst() )

METHOD Get() CLASS TORMQuery
   RETURN ::oConn:Query( ::ToSql() )
