/* relations.prg -- relacoes entre models + eager loading anti-N+1.
   Descritor normalizado: { "type", "factory"(codeblock), "fk", "key"(NIL=default PK) }.
   factory recebe a conexao do model pai: {|oConn| TFilho():New(oConn)}. */
#include "hborm.ch"
#include "error.ch"

FUNCTION ORM_HasMany( bFactory, cFk, cKey )
   RETURN RelDef( "hasMany", bFactory, cFk, cKey )

FUNCTION ORM_HasOne( bFactory, cFk, cKey )
   RETURN RelDef( "hasOne", bFactory, cFk, cKey )

FUNCTION ORM_BelongsTo( bFactory, cFk, cKey )
   RETURN RelDef( "belongsTo", bFactory, cFk, cKey )

STATIC FUNCTION RelDef( cType, bFactory, cFk, cKey )
   IF ! HB_ISBLOCK( bFactory )
      RelRaise( "RelDef", "factory deve ser um codeblock" )
   ENDIF
   IF ! HB_ISSTRING( cFk ) .OR. Empty( cFk )
      RelRaise( "RelDef", "fk deve ser string nao-vazia" )
   ENDIF
   RETURN { "type" => cType, "factory" => bFactory, "fk" => cFk, ;
            "key" => iif( HB_ISSTRING( cKey ) .AND. ! Empty( cKey ), cKey, NIL ) }

PROCEDURE RelRaise( cOp, cDesc )
   LOCAL oErr := ErrorNew()
   oErr:Subsystem( "hb_orm" )
   oErr:SubCode( 1200 )
   oErr:Severity( ES_ERROR )
   oErr:Description( cDesc )
   oErr:Operation( cOp )
   oErr:CanRetry( .F. )
   Eval( ErrorBlock(), oErr )
   RETURN

/* ---- eager loading ------------------------------------------------------ */

/* aRows -> array de models. bSpawn devolve um model fresco a cada Eval
   (filhos de relacao: clone de 1 prototipo; nunca chama factory por linha). */
FUNCTION ORM_HydrateModels( aRows, bSpawn )
   LOCAL aOut := {}, hRow
   FOR EACH hRow IN aRows
      AAdd( aOut, Eval( bSpawn ):FromRow( hRow ) )
   NEXT
   RETURN aOut

/* acumula nomes de eager (string ou array de strings) */
FUNCTION ORM_AddEager( aEager, xNames )
   LOCAL c
   IF HB_ISSTRING( xNames )
      AAdd( aEager, xNames )
   ELSEIF HB_ISARRAY( xNames )
      FOR EACH c IN xNames
         IF HB_ISSTRING( c )
            AAdd( aEager, c )
         ENDIF
      NEXT
   ENDIF
   RETURN aEager

/* aplica cada relacao eager sobre a colecao inteira (1 WhereIn por relacao) */
FUNCTION ORM_ApplyEager( aModels, aEager, hDefs, oConn )
   LOCAL cName
   IF aModels == NIL .OR. Len( aModels ) == 0 .OR. aEager == NIL
      RETURN aModels
   ENDIF
   FOR EACH cName IN aEager
      IF ! hb_HHasKey( hDefs, cName )
         RelRaise( "with", "relacao desconhecida: " + cName )
      ENDIF
      ORM_EagerLoad( aModels, cName, hDefs[ cName ], oConn )
   NEXT
   RETURN aModels

/* carrega 1 relacao para um conjunto de models ja hidratados;
   grava no cache de cada model via SetRelation. */
FUNCTION ORM_EagerLoad( aModels, cName, hRel, oConn )
   IF aModels == NIL .OR. Len( aModels ) == 0
      RETURN NIL
   ENDIF
   IF hRel[ "type" ] == "belongsTo"
      EagerBelongsTo( aModels, cName, hRel, oConn )
   ELSE
      EagerHasManyOne( aModels, cName, hRel, oConn )
   ENDIF
   RETURN NIL

/* hasMany / hasOne: filho.fk = pai.key (key default = PK do pai) */
STATIC PROCEDURE EagerHasManyOne( aModels, cName, hRel, oConn )
   LOCAL cFk := hRel[ "fk" ], bFac := hRel[ "factory" ], lOne := ( hRel[ "type" ] == "hasOne" )
   LOCAL cKey, aKeys, aRows, aChildren, hByFk, m, ch, cG, oProto
   cKey := iif( hRel[ "key" ] == NIL, aModels[ 1 ]:PrimaryKey(), hRel[ "key" ] )
   FOR EACH m IN aModels
      m:SetRelation( cName, iif( lOne, NIL, {} ) )
   NEXT
   aKeys := DistinctKeys( aModels, cKey )
   IF Len( aKeys ) == 0
      RETURN
   ENDIF
   oProto    := Eval( bFac, oConn )
   aRows     := oProto:Query():WhereIn( cFk, aKeys ):Get()
   aChildren := ORM_HydrateModels( aRows, {|| __objClone( oProto ) } )
   hByFk := hb_Hash()
   FOR EACH ch IN aChildren
      cG := hb_CStr( ch:Get( cFk ) )
      IF ! hb_HHasKey( hByFk, cG )
         hByFk[ cG ] := {}
      ENDIF
      AAdd( hByFk[ cG ], ch )
   NEXT
   FOR EACH m IN aModels
      cG := hb_CStr( m:Get( cKey ) )
      IF hb_HHasKey( hByFk, cG )
         m:SetRelation( cName, iif( lOne, hByFk[ cG ][ 1 ], hByFk[ cG ] ) )
      ENDIF
   NEXT
   RETURN

/* belongsTo: este.fk -> pai.key (key default = PK do pai relacionado) */
STATIC PROCEDURE EagerBelongsTo( aModels, cName, hRel, oConn )
   LOCAL cFk := hRel[ "fk" ], bFac := hRel[ "factory" ]
   LOCAL cKey, aKeys, aRows, aOwners, hById, m, ow, cG, oProto
   oProto := Eval( bFac, oConn )
   cKey   := iif( hRel[ "key" ] == NIL, oProto:PrimaryKey(), hRel[ "key" ] )
   FOR EACH m IN aModels
      m:SetRelation( cName, NIL )
   NEXT
   aKeys := DistinctKeys( aModels, cFk )
   IF Len( aKeys ) == 0
      RETURN
   ENDIF
   aRows   := oProto:Query():WhereIn( cKey, aKeys ):Get()
   aOwners := ORM_HydrateModels( aRows, {|| __objClone( oProto ) } )
   hById := hb_Hash()
   FOR EACH ow IN aOwners
      hById[ hb_CStr( ow:Get( cKey ) ) ] := ow
   NEXT
   FOR EACH m IN aModels
      cG := hb_CStr( m:Get( cFk ) )
      IF hb_HHasKey( hById, cG )
         m:SetRelation( cName, hById[ cG ] )
      ENDIF
   NEXT
   RETURN

/* valores distintos e nao-NIL de um campo, na ordem; chave de dedup = hb_CStr */
STATIC FUNCTION DistinctKeys( aModels, cField )
   LOCAL aOut := {}, hSeen := hb_Hash(), m, xV, cG
   FOR EACH m IN aModels
      xV := m:Get( cField )
      IF xV == NIL
         LOOP
      ENDIF
      cG := hb_CStr( xV )
      IF ! hb_HHasKey( hSeen, cG )
         hSeen[ cG ] := .T.
         AAdd( aOut, xV )
      ENDIF
   NEXT
   RETURN aOut
