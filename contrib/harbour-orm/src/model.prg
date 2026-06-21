/* model.prg -- ActiveRecord base. Subclass and override TableName().
 *
 * Persisted state is tracked with ::lExists (set by Find/Create/Save), not by
 * "is the primary key empty" -- a row created with an explicit primary key is
 * still a fresh INSERT, so the empty-key heuristic would wrongly UPDATE it. */
#include "hborm.ch"

CREATE CLASS TORMModel
   DATA oConn
   DATA hAttrs  INIT NIL
   DATA lExists INIT .F.
   METHOD New( oConn ) CONSTRUCTOR
   METHOD TableName()  VIRTUAL          // subclass MUST override
   METHOD PrimaryKey() INLINE "id"
   METHOD Set( cField, xVal ) INLINE ( ::hAttrs[ cField ] := xVal, Self )
   METHOD Get( cField )       INLINE hb_HGetDef( ::hAttrs, cField, NIL )
   METHOD Query()
   METHOD Create( hAttrs )
   METHOD Find( xId )
   METHOD Save()
   METHOD Delete()
   METHOD FromRow( hRow )               // internal: hydrate from a query row
   METHOD HasMany( bFactory, cForeignKey )
   METHOD HasOne( bFactory, cForeignKey )
   METHOD BelongsTo( bFactory, cForeignKey )
END CLASS

METHOD New( oConn ) CLASS TORMModel
   ::oConn  := iif( oConn == NIL, TORMConnection_Default(), oConn )
   ::hAttrs := hb_Hash()
   RETURN Self

METHOD Query() CLASS TORMModel
   RETURN TORMQuery():New( ::oConn, ::TableName() )

/* Relations. bFactory is a codeblock that builds a fresh related model bound to
   a connection: {| oConn | TRelated():New( oConn ) }. Returns hydrated models. */
METHOD HasMany( bFactory, cForeignKey ) CLASS TORMModel
   LOCAL oRel := Eval( bFactory, ::oConn )
   LOCAL aRows := oRel:Query():Where( cForeignKey, ::Get( ::PrimaryKey() ) ):Get()
   LOCAL aOut := {}, hRow
   FOR EACH hRow IN aRows
      AAdd( aOut, Eval( bFactory, ::oConn ):FromRow( hRow ) )
   NEXT
   RETURN aOut

METHOD HasOne( bFactory, cForeignKey ) CLASS TORMModel
   LOCAL aAll := ::HasMany( bFactory, cForeignKey )
   RETURN iif( Len( aAll ) == 0, NIL, aAll[ 1 ] )

METHOD BelongsTo( bFactory, cForeignKey ) CLASS TORMModel
   LOCAL oRel := Eval( bFactory, ::oConn )
   LOCAL aRows := oRel:Query():Where( oRel:PrimaryKey(), ::Get( cForeignKey ) ):Get()
   IF Len( aRows ) == 0
      RETURN NIL
   ENDIF
   RETURN Eval( bFactory, ::oConn ):FromRow( aRows[ 1 ] )

METHOD FromRow( hRow ) CLASS TORMModel
   ::hAttrs  := hb_HClone( hRow )
   ::lExists := .T.
   RETURN Self

METHOD Create( hAttrs ) CLASS TORMModel
   LOCAL cK
   FOR EACH cK IN hb_HKeys( hAttrs )
      ::hAttrs[ cK ] := hAttrs[ cK ]
   NEXT
   ::lExists := .F.                     // force the INSERT path
   ::Save()
   RETURN Self

METHOD Find( xId ) CLASS TORMModel
   LOCAL aRows, hRow
   IF ::oConn:IsNavigational()
      hRow := ::oConn:NavFind( ::TableName(), ::PrimaryKey(), xId )
      RETURN iif( hRow == NIL, NIL, ::FromRow( hRow ) )
   ENDIF
   aRows := ::Query():Where( ::PrimaryKey(), xId ):Limit( 1 ):Get()
   IF Len( aRows ) == 0
      RETURN NIL
   ENDIF
   RETURN ::FromRow( aRows[ 1 ] )

METHOD Save() CLASS TORMModel
   LOCAL oG, xId, hAst, cSql, lOk
   IF ::oConn:IsNavigational()
      IF ::lExists
         lOk := ::oConn:NavUpdate( ::TableName(), ::PrimaryKey(), ;
                                   ::Get( ::PrimaryKey() ), ::hAttrs )
      ELSE
         lOk := ::oConn:NavInsert( ::TableName(), ::hAttrs )
      ENDIF
      IF lOk
         ::lExists := .T.
      ENDIF
      RETURN lOk
   ENDIF
   oG := TORMGrammar():New()
   IF ::lExists
      xId  := ::Get( ::PrimaryKey() )
      hAst := { "type" => "update", "table" => ::TableName(), ;
                "values" => ::hAttrs, ;
                "wheres" => { { ::PrimaryKey(), "=", xId } } }
   ELSE
      hAst := { "type" => "insert", "table" => ::TableName(), "values" => ::hAttrs }
   ENDIF
   cSql := oG:Compile( hAst )
   lOk  := ::oConn:Execute( cSql )
   IF lOk
      ::lExists := .T.
   ENDIF
   RETURN lOk

METHOD Delete() CLASS TORMModel
   LOCAL oG, xId := ::Get( ::PrimaryKey() ), cSql, lOk
   IF Empty( xId )
      RETURN .F.
   ENDIF
   IF ::oConn:IsNavigational()
      lOk := ::oConn:NavDelete( ::TableName(), ::PrimaryKey(), xId )
      IF lOk
         ::lExists := .F.
      ENDIF
      RETURN lOk
   ENDIF
   oG := TORMGrammar():New()
   cSql := oG:Compile( { "type" => "delete", "table" => ::TableName(), ;
                         "wheres" => { { ::PrimaryKey(), "=", xId } } } )
   lOk := ::oConn:Execute( cSql )
   IF lOk
      ::lExists := .F.
   ENDIF
   RETURN lOk
