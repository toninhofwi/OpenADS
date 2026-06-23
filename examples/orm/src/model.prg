/* model.prg -- ActiveRecord. Subclasse sobrescreve TableName().
   Persistencia rastreada por ::lExists (nao "PK vazia"): linha criada com PK
   explicita ainda e um INSERT. CRUD 100% parametrizado via Grammar+Connection. */
#include "hborm.ch"

CREATE CLASS TORMModel
   DATA oConn
   DATA hAttrs  INIT NIL
   DATA lExists INIT .F.
   DATA hRelDefs INIT NIL
   DATA hRelCache
   DATA aEager   INIT {}
   METHOD New( oConn ) CONSTRUCTOR
   METHOD TableName() VIRTUAL
   METHOD PrimaryKey() INLINE "id"
   METHOD Set( cField, xVal ) INLINE ( ::hAttrs[ cField ] := xVal, Self )
   METHOD Get( cField )       INLINE hb_HGetDef( ::hAttrs, cField, NIL )
   METHOD Casts()             INLINE {=>}    // hash vazio; subclasse sobrescreve
   METHOD Query()
   METHOD Create( hAttrs )
   METHOD Find( xId )
   METHOD FindBy( cCol, xVal )
   METHOD BuscarPor( cCol, xVal )    INLINE ::FindBy( cCol, xVal )
   METHOD Save()
   METHOD Delete()
   METHOD All()
   METHOD Onde( cCol, xOpOrVal, xVal )
   METHOD Todos()             INLINE ::All()
   METHOD Buscar( xId )       INLINE ::Find( xId )
   METHOD Salvar()            INLINE ::Save()
   METHOD Apagar()            INLINE ::Delete()
   METHOD Criar( hAttrs )     INLINE ::Create( hAttrs )
   METHOD FromRow( hRow )
   METHOD Relations()                INLINE {=>}
   METHOD RelDefs()
   METHOD DefineRelation( cName, hDef )
   METHOD Rel( cName )
   METHOD Relacao( cName )           INLINE ::Rel( cName )
   METHOD SetRelation( cName, xVal ) INLINE ( ::hRelCache[ cName ] := xVal, Self )
   METHOD Com( xNames )
   METHOD With( xNames )             INLINE ::Com( xNames )
   METHOD InsertMany( aRows )
   METHOD InserirVarios( aRows )     INLINE ::InsertMany( aRows )
   METHOD Upsert( aRows, aConflict, aUpdate )
   METHOD InserirOuAtualizar( aRows, aConflict, aUpdate ) INLINE ::Upsert( aRows, aConflict, aUpdate )
END CLASS

METHOD New( oConn ) CLASS TORMModel
   ::oConn     := iif( oConn == NIL, TORMConnection_Default(), oConn )
   ::hAttrs    := hb_Hash()
   ::hRelCache := hb_Hash()
   RETURN Self

METHOD Query() CLASS TORMModel
   RETURN TORMQuery():New( ::oConn, ::TableName() ):BindModel( Self )

METHOD FromRow( hRow ) CLASS TORMModel
   LOCAL hCast := ::Casts(), cK
   ::hAttrs := hb_HClone( hRow )
   IF hCast != NIL .AND. Len( hCast ) > 0
      FOR EACH cK IN hb_HKeys( hCast )
         IF hb_HHasKey( ::hAttrs, cK )
            ::hAttrs[ cK ] := ORM_Cast( ::hAttrs[ cK ], hCast[ cK ] )
         ENDIF
      NEXT
   ENDIF
   ::hRelCache := hb_Hash()
   ::lExists   := .T.
   RETURN Self

METHOD Create( hAttrs ) CLASS TORMModel
   LOCAL cK
   FOR EACH cK IN hb_HKeys( hAttrs )
      ::hAttrs[ cK ] := hAttrs[ cK ]
   NEXT
   ::lExists := .F.
   ::Save()
   RETURN Self

METHOD Find( xId ) CLASS TORMModel
   LOCAL aRows := ::Query():Where( ::PrimaryKey(), xId ):Limit( 1 ):Get()
   IF Len( aRows ) == 0
      RETURN NIL
   ENDIF
   ::FromRow( aRows[ 1 ] )
   IF Len( ::aEager ) > 0
      ORM_ApplyEager( { Self }, ::aEager, ::RelDefs(), ::oConn )
   ENDIF
   RETURN Self

METHOD FindBy( cCol, xVal ) CLASS TORMModel
   LOCAL aRows := ::Query():Where( cCol, xVal ):Limit( 1 ):Get()
   IF Len( aRows ) == 0
      RETURN NIL
   ENDIF
   ::FromRow( aRows[ 1 ] )
   IF Len( ::aEager ) > 0
      ORM_ApplyEager( { Self }, ::aEager, ::RelDefs(), ::oConn )
   ENDIF
   RETURN Self

METHOD Save() CLASS TORMModel
   LOCAL oG, hAst, r, lOk, xPk
   IF ::oConn:IsNavigational()
      IF ::lExists
         RETURN NavUpdate( ::oConn, ::TableName(), ::PrimaryKey(), ;
                           ::Get( ::PrimaryKey() ), ::hAttrs )
      ENDIF
      xPk := NavInsert( ::oConn, ::TableName(), ::hAttrs, ::PrimaryKey() )
      IF xPk != NIL
         ::Set( ::PrimaryKey(), xPk )
         ::lExists := .T.
      ENDIF
      RETURN ( xPk != NIL )
   ENDIF
   oG := TORMGrammar():New()
   IF ::lExists
      hAst := { "type" => "update", "table" => ::TableName(), "values" => ::hAttrs, ;
                "wheres" => { { ::PrimaryKey(), "=", ::Get( ::PrimaryKey() ) } } }
   ELSE
      hAst := { "type" => "insert", "table" => ::TableName(), "values" => ::hAttrs }
   ENDIF
   r   := oG:Compile( hAst )
   lOk := ::oConn:Execute( r[ "sql" ], r[ "params" ] )
   IF lOk
      ::lExists := .T.
   ENDIF
   RETURN lOk

METHOD Delete() CLASS TORMModel
   LOCAL oG, xId := ::Get( ::PrimaryKey() ), r, lOk
   IF Empty( xId )
      RETURN .F.
   ENDIF
   IF ::oConn:IsNavigational()
      lOk := NavDelete( ::oConn, ::TableName(), ::PrimaryKey(), xId )
      IF lOk ; ::lExists := .F. ; ENDIF
      RETURN lOk
   ENDIF
   oG := TORMGrammar():New()
   r := oG:Compile( { "type" => "delete", "table" => ::TableName(), ;
                      "wheres" => { { ::PrimaryKey(), "=", xId } } } )
   lOk := ::oConn:Execute( r[ "sql" ], r[ "params" ] )
   IF lOk
      ::lExists := .F.
   ENDIF
   RETURN lOk

/* All records as models of the SAME class (clones Self, preserving oConn and
   the dynamic model's introspected schema, then hydrates each row). */
METHOD All() CLASS TORMModel
   LOCAL aRows := ::Query():Get(), aOut := {}, hRow
   FOR EACH hRow IN aRows
      AAdd( aOut, __objClone( Self ):FromRow( hRow ) )
   NEXT
   ORM_ApplyEager( aOut, ::aEager, ::RelDefs(), ::oConn )
   RETURN aOut

/* Bridge to the fluent query builder (Porta B); accepts Onde(col,val) or Onde(col,op,val). */
METHOD Onde( cCol, xOpOrVal, xVal ) CLASS TORMModel
   IF PCount() == 2
      RETURN ::Query():Where( cCol, xOpOrVal )
   ENDIF
   RETURN ::Query():Where( cCol, xOpOrVal, xVal )

METHOD RelDefs() CLASS TORMModel
   IF ::hRelDefs == NIL
      ::hRelDefs := hb_HClone( ::Relations() )
   ENDIF
   RETURN ::hRelDefs

METHOD DefineRelation( cName, hDef ) CLASS TORMModel
   ::RelDefs()[ cName ] := hDef
   RETURN Self

METHOD Rel( cName ) CLASS TORMModel
   LOCAL hDefs := ::RelDefs()
   IF hb_HHasKey( ::hRelCache, cName )
      RETURN ::hRelCache[ cName ]
   ENDIF
   IF ! hb_HHasKey( hDefs, cName )
      RelRaise( "Rel", "relacao desconhecida: " + cName )
   ENDIF
   ORM_EagerLoad( { Self }, cName, hDefs[ cName ], ::oConn )
   RETURN ::hRelCache[ cName ]

METHOD Com( xNames ) CLASS TORMModel
   ORM_AddEager( ::aEager, xNames )
   RETURN Self

/* 🔬 ACHADO DE ENGINE: o passthrough sqlite mal-mapeia os params nomeados de um
   INSERT VALUES multi-linha (:p7..:pN viram lixo perto do teto ~18) -- Execute
   devolve .T. mas grava dados EMBARALHADOS. Por isso InsertMany/Upsert executam
   POR-LINHA (3 params :p1..:pN por statement, caminho comprovado do Save),
   reusando o prepared cacheado. O VALUES multi-linha da Grammar fica correto p/
   quando o engine corrigir o binding (candidato a PR). */
METHOD InsertMany( aRows ) CLASS TORMModel
   LOCAL aCols, oG, r, lOk := .T., hRow, hOne, cCol
   IF aRows == NIL .OR. Len( aRows ) == 0
      RETURN .F.
   ENDIF
   IF ::oConn:IsNavigational()
      RETURN NavInsertMany( ::oConn, ::TableName(), aRows )
   ENDIF
   aCols := hb_HKeys( aRows[ 1 ] )
   oG    := TORMGrammar():New()
   FOR EACH hRow IN aRows
      hOne := hb_Hash()
      FOR EACH cCol IN aCols
         hOne[ cCol ] := hb_HGetDef( hRow, cCol, NIL )
      NEXT
      r   := oG:Compile( { "type" => "insert", "table" => ::TableName(), "values" => hOne } )
      lOk := ::oConn:Execute( r[ "sql" ], r[ "params" ] ) .AND. lOk
   NEXT
   RETURN lOk

METHOD Upsert( aRows, aConflict, aUpdate ) CLASS TORMModel
   LOCAL aCols, oG, r, lOk := .T., hRow, cCol, aVals
   IF aRows == NIL .OR. Len( aRows ) == 0
      RETURN .F.
   ENDIF
   IF ::oConn:IsNavigational()
      NavUnsupported( "Upsert", "upsert (ON CONFLICT) nao suportado no backend navegacional" )
   ENDIF
   aCols := hb_HKeys( aRows[ 1 ] )
   IF aUpdate == NIL                                  // default: colunas menos as de conflito
      aUpdate := {}
      FOR EACH cCol IN aCols
         IF aConflict == NIL .OR. AScan( aConflict, {|c| c == cCol } ) == 0
            AAdd( aUpdate, cCol )
         ENDIF
      NEXT
   ENDIF
   oG := TORMGrammar():New()
   FOR EACH hRow IN aRows
      aVals := {}
      FOR EACH cCol IN aCols
         AAdd( aVals, hb_HGetDef( hRow, cCol, NIL ) )
      NEXT
      r   := oG:Compile( { "type" => "upsert", "table" => ::TableName(), ;
                           "columns" => aCols, "rows" => { aVals }, ;
                           "conflict" => aConflict, "update" => aUpdate } )
      lOk := ::oConn:Execute( r[ "sql" ], r[ "params" ] ) .AND. lOk
   NEXT
   RETURN lOk
