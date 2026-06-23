/* querybuilder.prg -- builder fluente -> AST parametrizada. */
#include "hborm.ch"
#include "error.ch"

CREATE CLASS TORMQuery
   DATA oConn
   DATA cTable   INIT ""
   DATA aColumns INIT NIL
   DATA aWheres  INIT {}
   DATA aOrders  INIT {}
   DATA nLimit   INIT NIL
   DATA aJoins   INIT {}
   DATA aGroups  INIT {}
   DATA aHavings INIT {}
   DATA nOffset  INIT NIL
   DATA hAgg     INIT NIL
   DATA oProto   INIT NIL
   DATA aEager   INIT {}
   METHOD New( oConn, cTable ) CONSTRUCTOR
   METHOD Select( aCols )  INLINE ( ::aColumns := aCols, Self )
   METHOD Where( cCol, xOpOrVal, xVal )
   METHOD OrWhere( cCol, xOpOrVal, xVal )
   METHOD WhereIn( cCol, aVals )    INLINE ( AAdd( ::aWheres, { "kind" => "in", "bool" => "AND", "col" => cCol, "vals" => aVals } ), Self )
   METHOD OrWhereIn( cCol, aVals )  INLINE ( AAdd( ::aWheres, { "kind" => "in", "bool" => "OR",  "col" => cCol, "vals" => aVals } ), Self )
   METHOD WhereRaw( cFrag, aVals )  INLINE ( AAdd( ::aWheres, { "kind" => "raw", "bool" => "AND", "frag" => cFrag, "params" => iif( aVals == NIL, {}, aVals ) } ), Self )
   METHOD Onde( cCol, xOpOrVal, xVal )
   METHOD OuOnde( cCol, xOpOrVal, xVal )
   METHOD OndeEm( cCol, aVals )     INLINE ::WhereIn( cCol, aVals )
   METHOD OuOndeEm( cCol, aVals )   INLINE ::OrWhereIn( cCol, aVals )
   METHOD OndeCru( cFrag, aVals )   INLINE ::WhereRaw( cFrag, aVals )
   METHOD Join( cTab, c1, cOp, c2 )
   METHOD LeftJoin( cTab, c1, cOp, c2 )
   METHOD GroupBy( xCols )
   METHOD Having( cCol, xOpOrVal, xVal )
   METHOD Offset( n )                    INLINE ( ::nOffset := n, Self )
   METHOD Juntar( cTab, c1, cOp, c2 )    INLINE ::Join( cTab, c1, cOp, c2 )
   METHOD JuntarEsq( cTab, c1, cOp, c2 ) INLINE ::LeftJoin( cTab, c1, cOp, c2 )
   METHOD Agrupar( xCols )               INLINE ::GroupBy( xCols )
   METHOD Tendo( cCol, xOpOrVal, xVal )
   METHOD Pular( n )                     INLINE ::Offset( n )
   METHOD RunAggregate( cFn, cCol )
   METHOD Count()         INLINE ::RunAggregate( "COUNT", NIL )
   METHOD Sum( cCol )     INLINE ::RunAggregate( "SUM", cCol )
   METHOD Avg( cCol )     INLINE ::RunAggregate( "AVG", cCol )
   METHOD Max( cCol )     INLINE ::RunAggregate( "MAX", cCol )
   METHOD Min( cCol )     INLINE ::RunAggregate( "MIN", cCol )
   METHOD Contar()        INLINE ::Count()
   METHOD Somar( cCol )   INLINE ::Sum( cCol )
   METHOD Media( cCol )   INLINE ::Avg( cCol )
   METHOD Maximo( cCol )  INLINE ::Max( cCol )
   METHOD Minimo( cCol )  INLINE ::Min( cCol )
   METHOD Paginate( nPage, nPer )
   METHOD Paginar( nPage, nPer )   INLINE ::Paginate( nPage, nPer )
   METHOD OrderBy( cCol, cDir )
   METHOD Limit( n )       INLINE ( ::nLimit := n, Self )
   METHOD ToAst()
   METHOD Compiled()
   METHOD Get()
   METHOD BindModel( oProto )  INLINE ( ::oProto := oProto, Self )
   METHOD Com( xNames )
   METHOD With( xNames )       INLINE ::Com( xNames )
   METHOD Obter()
   METHOD ObterModelos()       INLINE ::Obter()
END CLASS

METHOD New( oConn, cTable ) CLASS TORMQuery
   ::oConn  := oConn
   ::cTable := cTable
   RETURN Self

METHOD Where( cCol, xOpOrVal, xVal ) CLASS TORMQuery
   IF PCount() == 2
      AAdd( ::aWheres, { "kind" => "simple", "bool" => "AND", "col" => cCol, "op" => "=", "val" => xOpOrVal } )
   ELSE
      AAdd( ::aWheres, { "kind" => "simple", "bool" => "AND", "col" => cCol, "op" => xOpOrVal, "val" => xVal } )
   ENDIF
   RETURN Self

METHOD OrWhere( cCol, xOpOrVal, xVal ) CLASS TORMQuery
   IF PCount() == 2
      AAdd( ::aWheres, { "kind" => "simple", "bool" => "OR", "col" => cCol, "op" => "=", "val" => xOpOrVal } )
   ELSE
      AAdd( ::aWheres, { "kind" => "simple", "bool" => "OR", "col" => cCol, "op" => xOpOrVal, "val" => xVal } )
   ENDIF
   RETURN Self

/* aliases PT que dependem de PCount(): metodos reais (PCount nao e confiavel em INLINE) */
METHOD Onde( cCol, xOpOrVal, xVal ) CLASS TORMQuery
   IF PCount() == 2
      RETURN ::Where( cCol, xOpOrVal )
   ENDIF
   RETURN ::Where( cCol, xOpOrVal, xVal )

METHOD OuOnde( cCol, xOpOrVal, xVal ) CLASS TORMQuery
   IF PCount() == 2
      RETURN ::OrWhere( cCol, xOpOrVal )
   ENDIF
   RETURN ::OrWhere( cCol, xOpOrVal, xVal )

METHOD Join( cTab, c1, cOp, c2 ) CLASS TORMQuery
   AAdd( ::aJoins, { "type" => "INNER", "table" => cTab, "c1" => c1, "op" => cOp, "c2" => c2 } )
   RETURN Self

METHOD LeftJoin( cTab, c1, cOp, c2 ) CLASS TORMQuery
   AAdd( ::aJoins, { "type" => "LEFT", "table" => cTab, "c1" => c1, "op" => cOp, "c2" => c2 } )
   RETURN Self

METHOD GroupBy( xCols ) CLASS TORMQuery
   LOCAL c
   IF HB_ISSTRING( xCols )
      AAdd( ::aGroups, xCols )
   ELSE
      FOR EACH c IN xCols
         AAdd( ::aGroups, c )
      NEXT
   ENDIF
   RETURN Self

METHOD Having( cCol, xOpOrVal, xVal ) CLASS TORMQuery
   IF PCount() == 2
      AAdd( ::aHavings, { "kind" => "simple", "bool" => "AND", "col" => cCol, "op" => "=", "val" => xOpOrVal } )
   ELSE
      AAdd( ::aHavings, { "kind" => "simple", "bool" => "AND", "col" => cCol, "op" => xOpOrVal, "val" => xVal } )
   ENDIF
   RETURN Self

/* alias PT real (PCount nao e confiavel em INLINE) */
METHOD Tendo( cCol, xOpOrVal, xVal ) CLASS TORMQuery
   IF PCount() == 2
      RETURN ::Having( cCol, xOpOrVal )
   ENDIF
   RETURN ::Having( cCol, xOpOrVal, xVal )

METHOD OrderBy( cCol, cDir ) CLASS TORMQuery
   AAdd( ::aOrders, { cCol, iif( cDir == NIL, "ASC", Upper( cDir ) ) } )
   RETURN Self

METHOD ToAst() CLASS TORMQuery
   RETURN { "type" => "select", "table" => ::cTable, "columns" => ::aColumns, ;
            "wheres" => ::aWheres, "joins" => ::aJoins, "groups" => ::aGroups, ;
            "havings" => ::aHavings, "orders" => ::aOrders, "limit" => ::nLimit, ;
            "offset" => ::nOffset, "aggregate" => ::hAgg }

METHOD RunAggregate( cFn, cCol ) CLASS TORMQuery
   LOCAL hSave := ::hAgg, r, aRows, aVals, xV
   IF ::oConn:IsNavigational()
      QRaise( "RunAggregate", "agregado nao suportado no backend navegacional" )
   ENDIF
   ::hAgg := { "fn" => cFn, "col" => cCol }
   r := ::Compiled()
   ::hAgg := hSave                                  // restaura: builder reutilizavel
   aRows := ::oConn:Query( r[ "sql" ], r[ "params" ] )
   IF Len( aRows ) == 0
      RETURN 0
   ENDIF
   aVals := hb_HValues( aRows[ 1 ] )
   IF Len( aVals ) == 0
      RETURN 0
   ENDIF
   xV := aVals[ 1 ]
   RETURN iif( HB_ISNUMERIC( xV ), xV, Val( hb_CStr( xV ) ) )

METHOD Compiled() CLASS TORMQuery
   RETURN TORMGrammar():New():Compile( ::ToAst() )

METHOD Get() CLASS TORMQuery
   LOCAL r
   IF ::oConn:IsNavigational()
      RETURN NavSelect( ::oConn, ::ToAst() )
   ENDIF
   r := ::Compiled()
   RETURN ::oConn:Query( r[ "sql" ], r[ "params" ] )

METHOD Paginate( nPage, nPer ) CLASS TORMQuery
   LOCAL nTotal, aData, nLast
   IF ! HB_ISNUMERIC( nPage ) .OR. ! HB_ISNUMERIC( nPer ) .OR. nPage < 1 .OR. nPer < 1
      QRaise( "Paginate", "pagina/por-pagina invalido" )
   ENDIF
   nTotal := ::Count()                              // ignora limit/offset/orders (caminho agregado)
   ::nLimit  := nPer
   ::nOffset := ( nPage - 1 ) * nPer
   aData  := ::Get()
   nLast  := Max( 1, Int( ( nTotal + nPer - 1 ) / nPer ) )      // ceil(total/per)
   RETURN { "data" => aData, "total" => nTotal, "page" => nPage, ;
            "per_page" => nPer, "last_page" => nLast }

METHOD Com( xNames ) CLASS TORMQuery
   ORM_AddEager( ::aEager, xNames )
   RETURN Self

/* terminal que devolve MODELS (nao hashes). Requer BindModel previo. */
METHOD Obter() CLASS TORMQuery
   LOCAL aRows, aModels, oProto := ::oProto
   IF oProto == NIL
      QRaise( "Obter", "Obter requer um model (use via Model:Onde/Com)" )
   ENDIF
   aRows   := ::Get()
   aModels := ORM_HydrateModels( aRows, {|| __objClone( oProto ) } )
   ORM_ApplyEager( aModels, ::aEager, oProto:RelDefs(), oProto:oConn )
   RETURN aModels

STATIC PROCEDURE QRaise( cOp, cDesc )
   LOCAL oErr := ErrorNew()
   oErr:Subsystem( "hb_orm" )
   oErr:SubCode( 1100 )
   oErr:Severity( ES_ERROR )
   oErr:Description( cDesc )
   oErr:Operation( cOp )
   oErr:CanRetry( .F. )
   Eval( ErrorBlock(), oErr )
   RETURN
