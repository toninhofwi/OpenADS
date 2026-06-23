/* blueprint.prg -- builder fluente de colunas -> AST createTable (agnostica de banco).
   Modificadores encadeaveis mutam a ultima coluna adicionada (::oCurrent). */
#include "hborm.ch"

CREATE CLASS TORMBlueprint
   DATA cTable
   DATA aColumns
   DATA aIndexes
   DATA oCurrent                                 // ultima coluna (hash) p/ modificadores
   METHOD New( cTable ) CONSTRUCTOR
   METHOD Id( cName )
   METHOD Integer( cName )
   METHOD String( cName, nLen )
   METHOD Text( cName )
   METHOD Decimal( cName, nPrec, nScale )
   METHOD Boolean( cName )
   METHOD Date( cName )
   METHOD DateTime( cName )
   METHOD Json( cName )
   METHOD Nullable( lBool )
   METHOD Default( xVal )
   METHOD Unique()
   METHOD Primary()
   METHOD Timestamps()
   METHOD Index( aCols, lUnique, cName )
   METHOD ToAst()
   /* aliases PT (casca fina, mesmo metodo real) */
   METHOD Inteiro( cName )                 INLINE ::Integer( cName )
   METHOD Texto( cName )                   INLINE ::Text( cName )
   METHOD Booleano( cName )                INLINE ::Boolean( cName )
   METHOD Nulo( lBool )                    INLINE ::Nullable( lBool )
   METHOD Padrao( xVal )                   INLINE ::Default( xVal )
   METHOD Unico()                          INLINE ::Unique()
   METHOD Indice( aCols, lUnique, cName )  INLINE ::Index( aCols, lUnique, cName )
END CLASS

METHOD New( cTable ) CLASS TORMBlueprint
   ::cTable   := cTable
   ::aColumns := {}
   ::aIndexes := {}
   RETURN Self

STATIC FUNCTION NewCol( cName, cType )
   RETURN hb_Hash( "name", cName, "type", cType, ;
                   "nullable", .T., "unique", .F., "pk", .F., "autoinc", .F. )

METHOD Id( cName ) CLASS TORMBlueprint
   ::oCurrent := NewCol( iif( cName == NIL, "id", cName ), "integer" )
   ::oCurrent[ "pk" ]       := .T.
   ::oCurrent[ "autoinc" ]  := .T.
   ::oCurrent[ "nullable" ] := .F.
   AAdd( ::aColumns, ::oCurrent )
   RETURN Self

METHOD Integer( cName ) CLASS TORMBlueprint
   ::oCurrent := NewCol( cName, "integer" )
   AAdd( ::aColumns, ::oCurrent )
   RETURN Self

METHOD String( cName, nLen ) CLASS TORMBlueprint
   ::oCurrent := NewCol( cName, "string" )
   IF nLen != NIL
      ::oCurrent[ "len" ] := nLen
   ENDIF
   AAdd( ::aColumns, ::oCurrent )
   RETURN Self

METHOD Text( cName ) CLASS TORMBlueprint
   ::oCurrent := NewCol( cName, "text" )
   AAdd( ::aColumns, ::oCurrent )
   RETURN Self

METHOD Decimal( cName, nPrec, nScale ) CLASS TORMBlueprint
   ::oCurrent := NewCol( cName, "decimal" )
   IF nPrec != NIL
      ::oCurrent[ "prec" ]  := nPrec
      ::oCurrent[ "scale" ] := iif( nScale == NIL, 0, nScale )
   ENDIF
   AAdd( ::aColumns, ::oCurrent )
   RETURN Self

METHOD Boolean( cName ) CLASS TORMBlueprint
   ::oCurrent := NewCol( cName, "boolean" )
   AAdd( ::aColumns, ::oCurrent )
   RETURN Self

METHOD Date( cName ) CLASS TORMBlueprint
   ::oCurrent := NewCol( cName, "date" )
   AAdd( ::aColumns, ::oCurrent )
   RETURN Self

METHOD DateTime( cName ) CLASS TORMBlueprint
   ::oCurrent := NewCol( cName, "datetime" )
   AAdd( ::aColumns, ::oCurrent )
   RETURN Self

METHOD Json( cName ) CLASS TORMBlueprint
   ::oCurrent := NewCol( cName, "json" )
   AAdd( ::aColumns, ::oCurrent )
   RETURN Self

METHOD Nullable( lBool ) CLASS TORMBlueprint
   IF ::oCurrent != NIL
      ::oCurrent[ "nullable" ] := iif( lBool == NIL, .T., lBool )
   ENDIF
   RETURN Self

METHOD Default( xVal ) CLASS TORMBlueprint
   IF ::oCurrent != NIL
      ::oCurrent[ "default" ] := xVal           // chave presente mesmo se NIL (-> NULL)
   ENDIF
   RETURN Self

METHOD Unique() CLASS TORMBlueprint
   IF ::oCurrent != NIL
      ::oCurrent[ "unique" ] := .T.
   ENDIF
   RETURN Self

METHOD Primary() CLASS TORMBlueprint
   IF ::oCurrent != NIL
      ::oCurrent[ "pk" ]       := .T.
      ::oCurrent[ "nullable" ] := .F.
   ENDIF
   RETURN Self

METHOD Timestamps() CLASS TORMBlueprint
   ::DateTime( "created_at" )
   ::DateTime( "updated_at" )
   RETURN Self

METHOD Index( aCols, lUnique, cName ) CLASS TORMBlueprint
   AAdd( ::aIndexes, hb_Hash( "columns", aCols, ;
         "unique", ( lUnique == .T. ), "name", cName ) )
   RETURN Self

METHOD ToAst() CLASS TORMBlueprint
   RETURN hb_Hash( "type", "createTable", "table", ::cTable, ;
                   "columns", ::aColumns, "indexes", ::aIndexes )
