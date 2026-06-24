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

/* N:N via tabela-pivo. cFkLocal/cFkRelated = colunas do pivo que apontam p/ o pai
   e p/ o relacionado; cKeyLocal/cKeyRelated default = PK do pai/relacionado. */
FUNCTION ORM_BelongsToMany( bFactory, cPivot, cFkLocal, cFkRelated, cKeyLocal, cKeyRelated )
   IF ! HB_ISBLOCK( bFactory )
      RelRaise( "ORM_BelongsToMany", "factory deve ser um codeblock" )
   ENDIF
   IF ! HB_ISSTRING( cPivot ) .OR. Empty( cPivot ) .OR. ;
      ! HB_ISSTRING( cFkLocal ) .OR. Empty( cFkLocal ) .OR. ;
      ! HB_ISSTRING( cFkRelated ) .OR. Empty( cFkRelated )
      RelRaise( "ORM_BelongsToMany", "pivot/fkLocal/fkRelated devem ser strings nao-vazias" )
   ENDIF
   RETURN { "type" => "belongsToMany", "factory" => bFactory, "pivot" => cPivot, ;
            "fkLocal" => cFkLocal, "fkRelated" => cFkRelated, ;
            "keyLocal"   => iif( HB_ISSTRING( cKeyLocal ) .AND. ! Empty( cKeyLocal ), cKeyLocal, NIL ), ;
            "keyRelated" => iif( HB_ISSTRING( cKeyRelated ) .AND. ! Empty( cKeyRelated ), cKeyRelated, NIL ) }

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

/* aplica eager sobre a colecao inteira. Aceita paths aninhados "a.b.c"
   (nested eager): cada segmento e carregado UMA vez por nivel; anti-N+1 em
   cada nivel. Nome sem ponto = comportamento de sempre (1 segmento). */
FUNCTION ORM_ApplyEager( aModels, aEager, hDefs, oConn )
   IF aModels == NIL .OR. Len( aModels ) == 0 .OR. aEager == NIL .OR. Len( aEager ) == 0
      RETURN aModels
   ENDIF
   ApplyEagerPaths( aModels, EagerParsePaths( aEager ), hDefs, oConn )
   RETURN aModels

/* parseia cada nome de eager em array-de-segmentos: "a.b" -> {"a","b"} */
STATIC FUNCTION EagerParsePaths( aEager )
   LOCAL aOut := {}, cName, aSeg
   FOR EACH cName IN aEager
      IF HB_ISSTRING( cName ) .AND. ! Empty( cName )
         aSeg := hb_ATokens( cName, "." )
         IF Len( aSeg ) > 0
            AAdd( aOut, aSeg )
         ENDIF
      ENDIF
   NEXT
   RETURN aOut

/* aplica uma lista de paths (cada path = array de segmentos) recursivamente:
   agrupa pelo 1o segmento (carrega 1x), depois recursa as caudas nos filhos. */
STATIC PROCEDURE ApplyEagerPaths( aModels, aPaths, hDefs, oConn )
   LOCAL hGroups, aPath, cFirst, aTail, i, cKey, aTails, aChildren
   IF aModels == NIL .OR. Len( aModels ) == 0 .OR. aPaths == NIL .OR. Len( aPaths ) == 0
      RETURN
   ENDIF
   hGroups := hb_Hash()                              // ordem de 1a aparicao preservada
   FOR EACH aPath IN aPaths
      cFirst := aPath[ 1 ]
      IF ! hb_HHasKey( hGroups, cFirst )
         hGroups[ cFirst ] := {}                     // garante o no, mesmo standalone
      ENDIF
      IF Len( aPath ) > 1                             // tem cauda -> guarda p/ recursao
         aTail := {}
         FOR i := 2 TO Len( aPath )
            AAdd( aTail, aPath[ i ] )
         NEXT
         AAdd( hGroups[ cFirst ], aTail )
      ENDIF
   NEXT
   FOR EACH cKey IN hb_HKeys( hGroups )
      IF ! hb_HHasKey( hDefs, cKey )
         RelRaise( "with", "relacao desconhecida: " + cKey )
      ENDIF
      ORM_EagerLoad( aModels, cKey, hDefs[ cKey ], oConn )    // carrega este nivel 1x
      aTails := hGroups[ cKey ]
      IF Len( aTails ) > 0
         aChildren := FlattenChildren( aModels, cKey )
         IF Len( aChildren ) > 0
            ApplyEagerPaths( aChildren, aTails, aChildren[ 1 ]:RelDefs(), oConn )
         ENDIF
      ENDIF
   NEXT
   RETURN

/* achata os filhos de uma relacao ja carregada (array->itens; model->o item; NIL->pula) */
STATIC FUNCTION FlattenChildren( aModels, cName )
   LOCAL aOut := {}, m, xV, ch
   FOR EACH m IN aModels
      xV := m:Rel( cName )                            // ja em cache pos-eager (sem query)
      IF HB_ISARRAY( xV )
         FOR EACH ch IN xV
            AAdd( aOut, ch )
         NEXT
      ELSEIF HB_ISOBJECT( xV )
         AAdd( aOut, xV )
      ENDIF
   NEXT
   RETURN aOut

/* ---- withCount: anexa <relacao>_count em cada pai, sem carregar os filhos ---- */
FUNCTION ORM_ApplyWithCount( aModels, aNames, hDefs, oConn )
   LOCAL cName
   IF aModels == NIL .OR. Len( aModels ) == 0 .OR. aNames == NIL .OR. Len( aNames ) == 0
      RETURN aModels
   ENDIF
   FOR EACH cName IN aNames
      IF ! hb_HHasKey( hDefs, cName )
         RelRaise( "withCount", "relacao desconhecida: " + cName )
      ENDIF
      WithCountOne( aModels, cName, hDefs[ cName ], oConn )
   NEXT
   RETURN aModels

/* conta 1 relacao agrupando por FK (1 query agrupada); seta <cName>_count em cada pai.
   hasMany/hasOne contam o filho (fk no filho); belongsToMany conta linhas do pivo;
   belongsTo (to-one) levanta. */
STATIC PROCEDURE WithCountOne( aModels, cName, hRel, oConn )
   LOCAL cType := hRel[ "type" ], cAttr := cName + "_count"
   LOCAL cFk, cKey, aKeys, hCounts, m

   IF cType == "belongsTo"
      RelRaise( "withCount", "withCount nao suporta belongsTo (relacao to-one): " + cName )
   ENDIF
   FOR EACH m IN aModels
      m:Set( cAttr, 0 )                              // default = 0 (pai sem filho)
   NEXT
   IF cType == "belongsToMany"
      cFk   := hRel[ "fkLocal" ]
      cKey  := iif( hRel[ "keyLocal" ] == NIL, aModels[ 1 ]:PrimaryKey(), hRel[ "keyLocal" ] )
      aKeys := DistinctKeys( aModels, cKey )
      IF Len( aKeys ) == 0
         RETURN
      ENDIF
      hCounts := TORMQuery():New( oConn, hRel[ "pivot" ] ):WhereIn( cFk, aKeys ):CountBy( cFk )
   ELSE                                              // hasMany / hasOne: fk no filho
      cFk   := hRel[ "fk" ]
      cKey  := iif( hRel[ "key" ] == NIL, aModels[ 1 ]:PrimaryKey(), hRel[ "key" ] )
      aKeys := DistinctKeys( aModels, cKey )
      IF Len( aKeys ) == 0
         RETURN
      ENDIF
      hCounts := Eval( hRel[ "factory" ], oConn ):Query():WhereIn( cFk, aKeys ):CountBy( cFk )
   ENDIF
   FOR EACH m IN aModels
      m:Set( cAttr, hb_HGetDef( hCounts, hb_CStr( m:Get( cKey ) ), 0 ) )
   NEXT
   RETURN

/* carrega 1 relacao para um conjunto de models ja hidratados;
   grava no cache de cada model via SetRelation. */
FUNCTION ORM_EagerLoad( aModels, cName, hRel, oConn )
   IF aModels == NIL .OR. Len( aModels ) == 0
      RETURN NIL
   ENDIF
   DO CASE
   CASE hRel[ "type" ] == "belongsTo"
      EagerBelongsTo( aModels, cName, hRel, oConn )
   CASE hRel[ "type" ] == "belongsToMany"
      EagerBelongsToMany( aModels, cName, hRel, oConn )
   OTHERWISE
      EagerHasManyOne( aModels, cName, hRel, oConn )
   ENDCASE
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

/* belongsToMany: pai.keyLocal -> pivot.fkLocal ; pivot.fkRelated -> relacionado.keyRelated.
   Exatamente 2 queries (pivo + relacionados), independente do nº de pais. */
STATIC PROCEDURE EagerBelongsToMany( aModels, cName, hRel, oConn )
   LOCAL bFac := hRel[ "factory" ], cPivot := hRel[ "pivot" ]
   LOCAL cFkLocal := hRel[ "fkLocal" ], cFkRelated := hRel[ "fkRelated" ]
   LOCAL oProto, cKeyLocal, cKeyRelated, aLocalKeys, aPivot, hRow
   LOCAL hRelByLocal, hSeen, aRelatedKeys, aRows, aRelated, ow
   LOCAL hRelById, m, cL, cR, aList, xR

   oProto      := Eval( bFac, oConn )
   cKeyLocal   := iif( hRel[ "keyLocal" ]   == NIL, aModels[ 1 ]:PrimaryKey(), hRel[ "keyLocal" ] )
   cKeyRelated := iif( hRel[ "keyRelated" ] == NIL, oProto:PrimaryKey(),       hRel[ "keyRelated" ] )

   FOR EACH m IN aModels
      m:SetRelation( cName, {} )                     // default = vazio (pai sem pivo)
   NEXT
   aLocalKeys := DistinctKeys( aModels, cKeyLocal )
   IF Len( aLocalKeys ) == 0
      RETURN
   ENDIF

   /* Query #1: linhas do pivo p/ os pais */
   aPivot := TORMQuery():New( oConn, cPivot ):WhereIn( cFkLocal, aLocalKeys ):Get()

   /* mapa localKey -> {relatedKey...} (ordem do pivo) + conjunto distinto de relatedKey */
   hRelByLocal  := hb_Hash()
   hSeen        := hb_Hash()
   aRelatedKeys := {}
   FOR EACH hRow IN aPivot
      xR := hb_HGetDef( hRow, cFkRelated, NIL )
      IF xR == NIL
         LOOP
      ENDIF
      cL := hb_CStr( hb_HGetDef( hRow, cFkLocal, NIL ) )
      IF ! hb_HHasKey( hRelByLocal, cL )
         hRelByLocal[ cL ] := {}
      ENDIF
      AAdd( hRelByLocal[ cL ], xR )
      cR := hb_CStr( xR )
      IF ! hb_HHasKey( hSeen, cR )
         hSeen[ cR ] := .T.
         AAdd( aRelatedKeys, xR )
      ENDIF
   NEXT
   IF Len( aRelatedKeys ) == 0
      RETURN
   ENDIF

   /* Query #2: os relacionados, indexados pela keyRelated */
   aRows    := oProto:Query():WhereIn( cKeyRelated, aRelatedKeys ):Get()
   aRelated := ORM_HydrateModels( aRows, {|| __objClone( oProto ) } )
   hRelById := hb_Hash()
   FOR EACH ow IN aRelated
      hRelById[ hb_CStr( ow:Get( cKeyRelated ) ) ] := ow
   NEXT

   /* monta a lista de cada pai na ordem do pivo (pulando orfaos) */
   FOR EACH m IN aModels
      cL := hb_CStr( m:Get( cKeyLocal ) )
      IF ! hb_HHasKey( hRelByLocal, cL )
         LOOP
      ENDIF
      aList := {}
      FOR EACH xR IN hRelByLocal[ cL ]
         cR := hb_CStr( xR )
         IF hb_HHasKey( hRelById, cR )
            AAdd( aList, hRelById[ cR ] )
         ENDIF
      NEXT
      m:SetRelation( cName, aList )
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
