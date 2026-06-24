/* model.prg -- ActiveRecord. Subclasse sobrescreve TableName().
   Persistencia rastreada por ::lExists (nao "PK vazia"): linha criada com PK
   explicita ainda e um INSERT. CRUD 100% parametrizado via Grammar+Connection. */
#include "hborm.ch"
#include "error.ch"

CREATE CLASS TORMModel
   DATA oConn
   DATA hAttrs  INIT NIL
   DATA lExists INIT .F.
   DATA hRelDefs INIT NIL
   DATA hRelCache
   DATA aEager   INIT {}
   DATA aWithCount INIT {}
   DATA nTrashMode INIT 0
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
   METHOD PersistRow()
   METHOD Delete()
   METHOD All()
   METHOD Onde( cCol, xOpOrVal, xVal )
   METHOD Todos()             INLINE ::All()
   METHOD Buscar( xId )       INLINE ::Find( xId )
   METHOD Salvar()            INLINE ::Save()
   METHOD Apagar()            INLINE ::Delete()
   METHOD Criar( hAttrs )     INLINE ::Create( hAttrs )
   METHOD FromRow( hRow )
   METHOD FireEvent( cEvent, lCancelable )
   METHOD Relations()                INLINE {=>}
   METHOD RelDefs()
   METHOD DefineRelation( cName, hDef )
   METHOD Rel( cName )
   METHOD Relacao( cName )           INLINE ::Rel( cName )
   METHOD SetRelation( cName, xVal ) INLINE ( ::hRelCache[ cName ] := xVal, Self )
   METHOD Com( xNames )
   METHOD With( xNames )             INLINE ::Com( xNames )
   METHOD WithCount( xNames )
   METHOD ComContagem( xNames )      INLINE ::WithCount( xNames )
   METHOD ConConteo( xNames )        INLINE ::WithCount( xNames )
   METHOD InsertMany( aRows )
   METHOD InserirVarios( aRows )     INLINE ::InsertMany( aRows )
   METHOD Upsert( aRows, aConflict, aUpdate )
   METHOD InserirOuAtualizar( aRows, aConflict, aUpdate ) INLINE ::Upsert( aRows, aConflict, aUpdate )
   /* aliases ES (espanhol) -- casca fina, mesmo metodo real */
   METHOD Guardar()                  INLINE ::Save()
   METHOD Eliminar()                 INLINE ::Delete()
   METHOD Crear( hAttrs )            INLINE ::Create( hAttrs )
   METHOD Relacion( cName )          INLINE ::Rel( cName )
   METHOD Con( xNames )              INLINE ::Com( xNames )
   METHOD InsertarVarios( aRows )    INLINE ::InsertMany( aRows )
   METHOD InsertarOActualizar( aRows, aConflict, aUpdate ) INLINE ::Upsert( aRows, aConflict, aUpdate )
   METHOD Donde( cCol, xOpOrVal, xVal )   // alias ES de Onde (real: PCount nao confiavel em INLINE)
   /* auto-timestamps opt-in */
   METHOD Timestamps()      INLINE .F.
   /* soft-delete opt-in */
   METHOD SoftDeletes()     INLINE .F.
   METHOD DeletedAtColumn() INLINE "deleted_at"
   METHOD HardDelete()
   METHOD RemoveRow()
   METHOD ForceDelete()
   METHOD Restore()
   METHOD Trashed()
   METHOD WithTrashed()
   METHOD OnlyTrashed()
   /* PT */
   METHOD ApagarDeVez()  INLINE ::ForceDelete()
   METHOD Restaurar()    INLINE ::Restore()
   METHOD Apagado()      INLINE ::Trashed()
   METHOD ComApagados()  INLINE ::WithTrashed()
   METHOD SoApagados()   INLINE ::OnlyTrashed()
   /* ES */
   METHOD EliminarDefinitivo() INLINE ::ForceDelete()
   METHOD ConEliminados()      INLINE ::WithTrashed()
   METHOD SoloEliminados()     INLINE ::OnlyTrashed()
   METHOD Eliminado()          INLINE ::Trashed()
END CLASS

METHOD New( oConn ) CLASS TORMModel
   ::oConn     := iif( oConn == NIL, TORMConnection_Default(), oConn )
   ::hAttrs    := hb_Hash()
   ::hRelCache := hb_Hash()
   RETURN Self

METHOD Query() CLASS TORMModel
   LOCAL oQ := TORMQuery():New( ::oConn, ::TableName() ):BindModel( Self )
   IF ::SoftDeletes()
      oQ:SoftScope( ::DeletedAtColumn(), ::nTrashMode )
   ENDIF
   RETURN oQ

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
   ::FireEvent( "retrieved", .F. )
   RETURN Self

METHOD FireEvent( cEvent, lCancelable ) CLASS TORMModel
   LOCAL cName, xRet
   FOR EACH cName IN EventHookNames( cEvent )
      IF __objHasMsg( Self, cName )
         xRet := __objSendMsg( Self, cName )
         IF lCancelable == .T. .AND. ValType( xRet ) == "L" .AND. xRet == .F.
            RETURN .F.
         ENDIF
      ENDIF
   NEXT
   RETURN .T.

/* Mapa evento canonico (EN) -> nomes de metodo {EN, PT[, ES]}.
   restoring: PT==ES (AntesDeRestaurar) -> 2 entradas, evita dispatch duplo.
   restored:  PT (DepoisDeRestaurar) != ES (DespuesDeRestaurar) -> 3 entradas.
   Evento desconhecido -> {} (FireEvent no-op). */
STATIC FUNCTION EventHookNames( cEvent )
   STATIC s_hMap
   IF s_hMap == NIL
      s_hMap := { ;
         "saving"        => { "BeforeSave",        "AntesDeSalvar",      "AntesDeGuardar" }, ;
         "saved"         => { "AfterSave",         "DepoisDeSalvar",     "DespuesDeGuardar" }, ;
         "creating"      => { "BeforeCreate",      "AntesDeCriar",       "AntesDeCrear" }, ;
         "created"       => { "AfterCreate",       "DepoisDeCriar",      "DespuesDeCrear" }, ;
         "updating"      => { "BeforeUpdate",      "AntesDeAtualizar",   "AntesDeActualizar" }, ;
         "updated"       => { "AfterUpdate",       "DepoisDeAtualizar",  "DespuesDeActualizar" }, ;
         "deleting"      => { "BeforeDelete",      "AntesDeApagar",      "AntesDeEliminar" }, ;
         "deleted"       => { "AfterDelete",       "DepoisDeApagar",     "DespuesDeEliminar" }, ;
         "restoring"     => { "BeforeRestore",     "AntesDeRestaurar" }, ;
         "restored"      => { "AfterRestore",      "DepoisDeRestaurar",  "DespuesDeRestaurar" }, ;
         "forceDeleting" => { "BeforeForceDelete", "AntesDeApagarDeVez", "AntesDeEliminarDefinitivo" }, ;
         "forceDeleted"  => { "AfterForceDelete",  "DepoisDeApagarDeVez","DespuesDeEliminarDefinitivo" }, ;
         "retrieved"     => { "AfterRetrieve",     "DepoisDeRecuperar",  "DespuesDeRecuperar" } }
   ENDIF
   RETURN hb_HGetDef( s_hMap, cEvent, {} )

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
   IF Len( ::aWithCount ) > 0
      ORM_ApplyWithCount( { Self }, ::aWithCount, ::RelDefs(), ::oConn )
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
   IF Len( ::aWithCount ) > 0
      ORM_ApplyWithCount( { Self }, ::aWithCount, ::RelDefs(), ::oConn )
   ENDIF
   RETURN Self

METHOD Save() CLASS TORMModel
   LOCAL lNovo := ! ::lExists
   IF ! ::FireEvent( "saving", .T. )
      RETURN .F.
   ENDIF
   IF ! ::FireEvent( iif( lNovo, "creating", "updating" ), .T. )
      RETURN .F.
   ENDIF
   IF ! ::PersistRow()
      RETURN .F.
   ENDIF
   ::FireEvent( iif( lNovo, "created", "updated" ), .F. )
   ::FireEvent( "saved", .F. )
   RETURN .T.

METHOD PersistRow() CLASS TORMModel
   LOCAL oG, hAst, r, lOk, xPk
   IF ::Timestamps()
      IF ! ::lExists
         ::Set( "created_at", OrmNow() )
      ENDIF
      ::Set( "updated_at", OrmNow() )
   ENDIF
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
   IF ::SoftDeletes()
      IF ! ::FireEvent( "deleting", .T. )
         RETURN .F.
      ENDIF
      ::Set( ::DeletedAtColumn(), SoftStamp() )
      IF ! ::PersistRow()
         RETURN .F.
      ENDIF
      ::FireEvent( "deleted", .F. )
      RETURN .T.
   ENDIF
   RETURN ::HardDelete()

METHOD HardDelete() CLASS TORMModel
   IF ! ::FireEvent( "deleting", .T. )
      RETURN .F.
   ENDIF
   IF ! ::RemoveRow()
      RETURN .F.
   ENDIF
   ::FireEvent( "deleted", .F. )
   RETURN .T.

METHOD ForceDelete() CLASS TORMModel
   IF ! ::FireEvent( "forceDeleting", .T. )
      RETURN .F.
   ENDIF
   IF ! ::RemoveRow()
      RETURN .F.
   ENDIF
   ::FireEvent( "forceDeleted", .F. )
   RETURN .T.

METHOD RemoveRow() CLASS TORMModel
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

METHOD Restore() CLASS TORMModel
   IF ! ::SoftDeletes()
      ModelRaise( "Restore", "Restore requer SoftDeletes() habilitado" )
   ENDIF
   IF ! ::FireEvent( "restoring", .T. )
      RETURN .F.
   ENDIF
   ::Set( ::DeletedAtColumn(), iif( ::oConn:IsNavigational(), "", NIL ) )
   IF ! ::PersistRow()
      RETURN .F.
   ENDIF
   ::FireEvent( "restored", .F. )
   RETURN .T.

METHOD Trashed() CLASS TORMModel
   RETURN ::SoftDeletes() .AND. ! Empty( ::Get( ::DeletedAtColumn() ) )

METHOD WithTrashed() CLASS TORMModel
   ::nTrashMode := 1
   RETURN Self

METHOD OnlyTrashed() CLASS TORMModel
   ::nTrashMode := 2
   RETURN Self

/* All records as models of the SAME class (clones Self, preserving oConn and
   the dynamic model's introspected schema, then hydrates each row). */
METHOD All() CLASS TORMModel
   LOCAL aRows := ::Query():Get(), aOut := {}, hRow
   FOR EACH hRow IN aRows
      AAdd( aOut, __objClone( Self ):FromRow( hRow ) )
   NEXT
   ORM_ApplyEager( aOut, ::aEager, ::RelDefs(), ::oConn )
   ORM_ApplyWithCount( aOut, ::aWithCount, ::RelDefs(), ::oConn )
   RETURN aOut

/* Bridge to the fluent query builder (Porta B); accepts Onde(col,val) or Onde(col,op,val). */
METHOD Onde( cCol, xOpOrVal, xVal ) CLASS TORMModel
   IF PCount() == 2
      RETURN ::Query():Where( cCol, xOpOrVal )
   ENDIF
   RETURN ::Query():Where( cCol, xOpOrVal, xVal )

/* alias ES de Onde (metodo real -- PCount nao e confiavel em INLINE) */
METHOD Donde( cCol, xOpOrVal, xVal ) CLASS TORMModel
   IF PCount() == 2
      RETURN ::Onde( cCol, xOpOrVal )
   ENDIF
   RETURN ::Onde( cCol, xOpOrVal, xVal )

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

METHOD WithCount( xNames ) CLASS TORMModel
   ORM_AddEager( ::aWithCount, xNames )
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

/* stamp ISO compacto (14 chars, <= C,19 do nav, nao-vazio): marca a linha como apagada */
STATIC FUNCTION SoftStamp()
   RETURN DToS( Date() ) + StrTran( Time(), ":", "" )

/* agora em string ISO "YYYY-MM-DD HH:MM:SS" -- round-trip com o cast datetime (ToStamp) */
STATIC FUNCTION OrmNow()
   RETURN hb_TToC( hb_DateTime(), "YYYY-MM-DD", "HH:MM:SS" )

STATIC PROCEDURE ModelRaise( cOp, cDesc )
   LOCAL oErr := ErrorNew()
   oErr:Subsystem( "hb_orm" )
   oErr:SubCode( 1200 )
   oErr:Severity( ES_ERROR )
   oErr:Description( cDesc )
   oErr:Operation( cOp )
   oErr:CanRetry( .F. )
   Eval( ErrorBlock(), oErr )
   RETURN
