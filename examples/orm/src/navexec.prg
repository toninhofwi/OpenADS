/* navexec.prg -- executor NAVEGACIONAL: dirige um cursor de tabela (open/scan/
   seek/append/write/delete) quando a conexao nao fala SQL. Mesma AST da Fatia 4
   entra; rows (hb_Hash) saem -- a forma identica a Connection:Query, p/ Model
   nao mudar. Backend de prova: dbf://. */
#include "hborm.ch"
#include "error.ch"

/* contador de instrumentacao do seek -- prova que o fast-path dispara */
STATIC s_nSeeks := 0

/* ---- DDL: cria DBF a partir do AST do Blueprint -------------------------- */
FUNCTION NavCreateTable( oConn, hAst )
   LOCAL cTab := TORMGrammar():New():QuoteIdent( hAst[ "table" ] )
   LOCAL cFields := NavFieldDef( hAst[ "columns" ] ), nTbl, hIdx, lOk := .T.
   LOCAL cCol, cExpr, aCols
   nTbl := hbo_CreateTable( oConn:Handle(), cTab, cFields )
   IF nTbl == 0
      RETURN .F.
   ENDIF
   /* indices declarados -> tags CDX (uma coluna por tag; chave = nome do campo) */
   FOR EACH hIdx IN hAst[ "indexes" ]
      aCols := hIdx[ "columns" ]
      IF aCols != NIL .AND. Len( aCols ) > 0
         cCol  := TORMGrammar():New():QuoteIdent( aCols[ 1 ] )
         cExpr := Upper( cCol )                       // DBF guarda campo em CAIXA ALTA
         IF ! hbo_CreateIndex( nTbl, Left( "X" + cCol, 10 ), cExpr )
            lOk := .F.
         ENDIF
      ENDIF
   NEXT
   hbo_TableClose( nTbl )
   RETURN lOk

/* AST de colunas -> string ACE "NOME,TIPO,LARG,DEC;...". Tipos xBase seguros:
   integer/id -> N,10,0 (PK assinada pelo ORM); decimal -> N,prec,scale;
   string -> C,len; text -> M; boolean -> L; date -> D; datetime -> C,19 (ISO). */
STATIC FUNCTION NavFieldDef( aColumns )
   LOCAL c := "", hCol, cName, cType, nLen, nDec, cPart
   FOR EACH hCol IN aColumns
      cName := Upper( hCol[ "name" ] )
      cType := hCol[ "type" ]
      DO CASE
      CASE cType == "integer"  ; cPart := cName + ",N,10,0"
      CASE cType == "decimal"
         nLen := hb_HGetDef( hCol, "prec", 12 )
         nDec := hb_HGetDef( hCol, "scale", 2 )
         cPart := cName + ",N," + LTrim( Str( nLen ) ) + "," + LTrim( Str( nDec ) )
      CASE cType == "string"
         nLen := hb_HGetDef( hCol, "len", 100 )
         cPart := cName + ",C," + LTrim( Str( nLen ) )
      CASE cType == "text"     ; cPart := cName + ",M,10"
      CASE cType == "boolean"  ; cPart := cName + ",L,1"
      CASE cType == "date"     ; cPart := cName + ",D,8"
      CASE cType == "datetime" ; cPart := cName + ",C,19"
      CASE cType == "json"     ; cPart := cName + ",M,10"
      OTHERWISE                ; cPart := cName + ",C,100"
      ENDCASE
      c += iif( Empty( c ), "", ";" ) + cPart
   NEXT
   RETURN c

STATIC PROCEDURE NavRaise( cOp, cDesc )
   LOCAL oErr := ErrorNew()
   oErr:Subsystem( "hb_orm" )
   oErr:SubCode( 1200 )
   oErr:Severity( ES_ERROR )
   oErr:Description( cDesc )                          // sem SQL/path
   oErr:Operation( cOp )
   oErr:CanRetry( .F. )
   Eval( ErrorBlock(), oErr )
   RETURN

/* thin PUBLIC wrapper so external callers (model.prg) can raise a nav error
   without exposing the STATIC NavRaise directly */
FUNCTION NavUnsupported( cOp, cDesc )
   NavRaise( cOp, cDesc )
   RETURN NIL

/* wrapper PUBLICO da hidratacao por-registro (a STATIC NavHydrateRow ja e
   provada na Fatia 9); permite ao TORMCursor reusar sem duplicar a logica. */
FUNCTION NavHydrateRowPub( nTbl, aNames )
   RETURN NavHydrateRow( nTbl, aNames )

/* InsertMany navegacional: executa NavInsert por linha (sem ON CONFLICT).
   Devolve .T. se todas as linhas foram inseridas, .F. se alguma falhou ou
   a lista estava vazia. */
FUNCTION NavInsertMany( oConn, cTable, aRows )
   LOCAL hRow, lOk := .T.
   IF aRows == NIL .OR. Len( aRows ) == 0
      RETURN .F.
   ENDIF
   FOR EACH hRow IN aRows
      lOk := ( NavInsert( oConn, cTable, hRow, "id" ) != NIL ) .AND. lOk
   NEXT
   RETURN lOk

/* ---- INSERT navegacional: append + set por tipo + write ------------------ */
FUNCTION NavInsert( oConn, cTable, hAttrs, cPk )
   LOCAL cTab := TORMGrammar():New():QuoteIdent( cTable )
   LOCAL nTbl, cK, xPk, lOk := .T.
   IF cPk == NIL ; cPk := "id" ; ENDIF
   /* PK: usa a fornecida; senao max+1 (autoinc deterministico, portavel).
      Resolvido ANTES de abrir o handle de escrita -- evita dois handles
      simultaneos na mesma tabela. */
   xPk := hb_HGetDef( hAttrs, cPk, NIL )
   IF xPk == NIL
      xPk := NavNextId( oConn, cTable, cPk )
   ENDIF
   nTbl := hbo_OpenTable( oConn:Handle(), cTab )
   IF nTbl == 0
      RETURN NIL
   ENDIF
   hbo_OpenIdxBag( nTbl )                             // mantem o(s) indice(s) no append
   IF ! hbo_Append( nTbl )
      hbo_TableClose( nTbl ) ; RETURN NIL
   ENDIF
   NavSetField( nTbl, Upper( cPk ), xPk )
   FOR EACH cK IN hb_HKeys( hAttrs )
      IF ! ( Lower( cK ) == Lower( cPk ) )
         lOk := NavSetField( nTbl, Upper( cK ), hAttrs[ cK ] ) .AND. lOk
      ENDIF
   NEXT
   lOk := hbo_WriteRec( nTbl ) .AND. lOk
   hbo_TableClose( nTbl )
   RETURN iif( lOk, xPk, NIL )

/* proximo id = (maior id atual) + 1; 1 em tabela vazia. Scan simples. */
STATIC FUNCTION NavNextId( oConn, cTable, cPk )
   LOCAL nTbl, nMax := 0, xV
   nTbl := hbo_OpenTable( oConn:Handle(), TORMGrammar():New():QuoteIdent( cTable ) )
   IF nTbl == 0
      RETURN 1
   ENDIF
   hbo_GoTop( nTbl )
   DO WHILE ! hbo_Eof( nTbl )
      xV := hbo_GetNum( nTbl, Upper( cPk ) )
      IF xV > nMax ; nMax := xV ; ENDIF
      hbo_Skip( nTbl, 1 )
   ENDDO
   hbo_TableClose( nTbl )
   RETURN nMax + 1

/* set despachado pelo TIPO HARBOUR do valor (numero/logico/data/string) */
STATIC FUNCTION NavSetField( nTbl, cField, xVal )
   DO CASE
   CASE xVal == NIL          ; RETURN .T.            // deixa default/vazio
   CASE HB_ISNUMERIC( xVal ) ; RETURN hbo_SetNum( nTbl, cField, xVal )
   CASE HB_ISLOGICAL( xVal ) ; RETURN hbo_SetLog( nTbl, cField, xVal )
   CASE HB_ISDATE( xVal )    ; RETURN hbo_SetStr( nTbl, cField, DToS( xVal ) )
   ENDCASE
   RETURN hbo_SetStr( nTbl, cField, hb_CStr( xVal ) )

/* ---- UPDATE / DELETE navegacional: localiza a PK e grava/apaga ----------- */
FUNCTION NavUpdate( oConn, cTable, cPk, xPk, hAttrs )
   LOCAL cTab := TORMGrammar():New():QuoteIdent( cTable )
   LOCAL nTbl, cK, lOk := .T.
   IF cPk == NIL ; cPk := "id" ; ENDIF
   nTbl := hbo_OpenTable( oConn:Handle(), cTab )
   IF nTbl == 0 ; RETURN .F. ; ENDIF
   hbo_OpenIdxBag( nTbl )                             // mantem indice(s) na regravacao
   IF ! NavLocate( nTbl, cPk, xPk )
      hbo_TableClose( nTbl ) ; RETURN .F.
   ENDIF
   FOR EACH cK IN hb_HKeys( hAttrs )
      IF ! ( Lower( cK ) == Lower( cPk ) )            // nao reescreve a PK
         lOk := NavSetField( nTbl, Upper( cK ), hAttrs[ cK ] ) .AND. lOk
      ENDIF
   NEXT
   lOk := hbo_WriteRec( nTbl ) .AND. lOk
   hbo_TableClose( nTbl )
   RETURN lOk

FUNCTION NavDelete( oConn, cTable, cPk, xPk )
   LOCAL cTab := TORMGrammar():New():QuoteIdent( cTable )
   LOCAL nTbl, lOk
   IF cPk == NIL ; cPk := "id" ; ENDIF
   nTbl := hbo_OpenTable( oConn:Handle(), cTab )
   IF nTbl == 0 ; RETURN .F. ; ENDIF
   hbo_OpenIdxBag( nTbl )                             // mantem indice(s) na exclusao
   IF ! NavLocate( nTbl, cPk, xPk )
      hbo_TableClose( nTbl ) ; RETURN .F.
   ENDIF
   lOk := hbo_DeleteRec( nTbl )
   hbo_TableClose( nTbl )
   RETURN lOk

/* posiciona o cursor no registro cuja PK == xPk (scan; seek vem na Task 6) */
STATIC FUNCTION NavLocate( nTbl, cPk, xPk )
   LOCAL cField := Upper( cPk )
   hbo_GoTop( nTbl )
   DO WHILE ! hbo_Eof( nTbl )
      IF hbo_GetNum( nTbl, cField ) == xPk
         RETURN .T.
      ENDIF
      hbo_Skip( nTbl, 1 )
   ENDDO
   RETURN .F.

/* ---- SELECT navegacional: seek (fast path) | scan -> filtra -> ordena -> fatia
   O resultado e IDENTICO com ou sem seek -- o seek e so otimizacao O(log n);
   o scan e a garantia de correcao quando nao ha indice na coluna do filtro. */
FUNCTION NavSelect( oConn, hAst )
   LOCAL aRows, aWheres
   NavGuardUnsupported( hAst )                        // join/group/having/agg/raw -> levanta
   aWheres := hb_HGetDef( hAst, "wheres", {} )
   /* fast path: 1 termo simples "=" + indice na coluna -> seek O(log n) */
   aRows := NavTrySeek( oConn, hAst[ "table" ], aWheres )
   IF aRows == NIL                                    // nao aplicavel / sem indice -> scan
      aRows := NavScanRows( oConn, hAst[ "table" ] )
      aRows := NavFilter( aRows, aWheres )
   ENDIF
   aRows := NavApplyOrder( aRows, hb_HGetDef( hAst, "orders", {} ) )
   aRows := NavApplyLimit( aRows, hb_HGetDef( hAst, "limit", NIL ), ;
                                  hb_HGetDef( hAst, "offset", NIL ) )
   RETURN aRows

/* ---- seek por indice: prova de disparo via contador --------------------- *
   s_nSeeks (declarado no topo do modulo) conta SO os seeks que de fato
   dispararam (indice achado + AdsSeek emitido) -- nao conta o caminho "nao
   aplicavel/sem indice" (que devolve NIL e cai p/ scan). Permite ao teste
   distinguir seek real de fallback. */
FUNCTION NavSeekCount()      ; RETURN s_nSeeks
FUNCTION NavResetSeekCount() ; s_nSeeks := 0 ; RETURN NIL

/* devolve as rows casadas por seek, ou NIL se nao aplicavel (=> usa scan).
   Coleta todos os registros com a chave (soft-seek + walk enquanto igual);
   p/ PK unica e uma row. Chave numerica -> DOUBLEKEY; string -> STRINGKEY. */
STATIC FUNCTION NavTrySeek( oConn, cTable, aWheres )
   LOCAL cTab := TORMGrammar():New():QuoteIdent( cTable )
   LOCAL w, cCol, xVal, nTbl, nIdx, aNames := {}, n, i, aRows := {}, lHit, hRow
   IF Len( aWheres ) != 1
      RETURN NIL
   ENDIF
   w := aWheres[ 1 ]
   IF ! HB_ISHASH( w ) .OR. hb_HGetDef( w, "kind", "" ) != "simple" ;  RETURN NIL ; ENDIF
   IF hb_HGetDef( w, "op", "=" ) != "=" ;  RETURN NIL ; ENDIF
   cCol := w[ "col" ] ; xVal := w[ "val" ]
   nTbl := hbo_OpenTable( oConn:Handle(), cTab )
   IF nTbl == 0 ;  RETURN NIL ; ENDIF
   nIdx := NavSeekIndexFor( nTbl, cCol )              // 0 se nao ha tag p/ a coluna
   IF nIdx == 0
      hbo_TableClose( nTbl ) ;  RETURN NIL            // sem indice -> sinaliza scan
   ENDIF
   /* nomes p/ hidratar */
   n := hbo_NumFields( nTbl )
   FOR i := 1 TO n ; AAdd( aNames, AllTrim( hbo_FieldName( nTbl, i ) ) ) ; NEXT
   /* aqui o seek REALMENTE dispara: conta */
   s_nSeeks++
   lHit := iif( HB_ISNUMERIC( xVal ), hbo_SeekNum( nIdx, xVal ), ;
                                      hbo_SeekStr( nIdx, hb_CStr( xVal ) ) )
   IF lHit .AND. hbo_IsFound( nTbl )
      /* walk enquanto a chave continua igual; compara o valor HIDRATADO da
         coluna (serve p/ chave numerica E string, e respeita NULL/casts).
         Registros logicamente deletados sao ignorados (indice xBase nao
         remove a chave na exclusao logica; SET DELETED filtra em GoTop/Skip
         do scan mas nao pos-seek -- checar aqui mantem consistencia). */
      DO WHILE ! hbo_Eof( nTbl )
         hRow := NavHydrateRow( nTbl, aNames )
         IF ! NavEq( hb_HGetDef( hRow, Lower( cCol ), NIL ), xVal )
            EXIT
         ENDIF
         IF ! hbo_IsDeleted( nTbl )
            AAdd( aRows, hRow )
         ENDIF
         hbo_Skip( nTbl, 1 )
      ENDDO
   ENDIF
   hbo_TableClose( nTbl )
   RETURN aRows

/* handle do tag cujo nome casa a convencao do NavCreateTable ("X"+coluna) */
STATIC FUNCTION NavSeekIndexFor( nTbl, cCol )
   RETURN hbo_GetIndex( nTbl, Left( "X" + TORMGrammar():New():QuoteIdent( cCol ), 10 ) )

/* operacoes que o cursor nao faz: erro honesto (sem stub fantasma) */
STATIC PROCEDURE NavGuardUnsupported( hAst )
   LOCAL w
   IF Len( hb_HGetDef( hAst, "joins",   {} ) ) > 0 ;  NavRaise( "NavSelect", "join nao suportado no backend navegacional" ) ; ENDIF
   IF Len( hb_HGetDef( hAst, "groups",  {} ) ) > 0 ;  NavRaise( "NavSelect", "groupBy nao suportado no backend navegacional" ) ; ENDIF
   IF Len( hb_HGetDef( hAst, "havings", {} ) ) > 0 ;  NavRaise( "NavSelect", "having nao suportado no backend navegacional" ) ; ENDIF
   IF hb_HGetDef( hAst, "aggregate", NIL ) != NIL  ;  NavRaise( "NavSelect", "agregado nao suportado no backend navegacional" ) ; ENDIF
   FOR EACH w IN hb_HGetDef( hAst, "wheres", {} )
      IF HB_ISHASH( w ) .AND. hb_HGetDef( w, "kind", "" ) == "raw"
         NavRaise( "NavSelect", "SQL cru nao suportado no backend navegacional" )
      ENDIF
   NEXT
   RETURN

/* abre a tabela, le todos os registros vivos como row-hash hidratada */
STATIC FUNCTION NavScanRows( oConn, cTable )
   LOCAL cTab := TORMGrammar():New():QuoteIdent( cTable )
   LOCAL nTbl, aNames := {}, n, i, aRows := {}
   nTbl := hbo_OpenTable( oConn:Handle(), cTab )
   IF nTbl == 0
      NavRaise( "NavSelect", "tabela nao abre (navegacional)" )
      RETURN {}
   ENDIF
   n := hbo_NumFields( nTbl )
   FOR i := 1 TO n
      AAdd( aNames, AllTrim( hbo_FieldName( nTbl, i ) ) )
   NEXT
   hbo_GoTop( nTbl )
   DO WHILE ! hbo_Eof( nTbl )                         // ShowDeleted(.F.) ja oculta deletados
      AAdd( aRows, NavHydrateRow( nTbl, aNames ) )
      hbo_Skip( nTbl, 1 )
   ENDDO
   hbo_TableClose( nTbl )
   RETURN aRows

/* hidratacao com getters NATIVOS vivos: N->double real, NULL->NIL via AdsIsNull;
   logico via "T"; C/D/T ficam string (a camada Casts do Model coage D/T, igual ao
   caminho SQL). Chave da row em minuscula p/ casar com as colunas do dominio. */
STATIC FUNCTION NavHydrateRow( nTbl, aNames )
   LOCAL hRow := hb_Hash(), cName, cKey, cTag, cRaw
   FOR EACH cName IN aNames
      cKey := Lower( cName )
      IF hbo_IsNull( nTbl, cName )
         hRow[ cKey ] := NIL
         LOOP
      ENDIF
      cTag := hbo_FieldType( nTbl, cName )
      DO CASE
      CASE cTag == "N"
         IF hbo_FieldDecimals( nTbl, cName ) == 0
            hRow[ cKey ] := Int( hbo_GetNum( nTbl, cName ) )
         ELSE
            hRow[ cKey ] := hbo_GetNum( nTbl, cName )
         ENDIF
      CASE cTag == "L" ; hRow[ cKey ] := ( Upper( Left( hbo_Field( nTbl, cName ), 1 ) ) $ "TY" )
      OTHERWISE
         cRaw := hbo_Field( nTbl, cName )
         hRow[ cKey ] := iif( cTag != "C" .AND. Empty( cRaw ), NIL, cRaw )
      ENDCASE
   NEXT
   RETURN hRow

/* ---- predicados em memoria sobre o envelope wheres da Fatia 4 ------------ */
STATIC FUNCTION NavFilter( aRows, aWheres )
   LOCAL aOut := {}, hRow
   IF aWheres == NIL .OR. Len( aWheres ) == 0
      RETURN aRows
   ENDIF
   FOR EACH hRow IN aRows
      IF NavMatch( hRow, aWheres )
         AAdd( aOut, hRow )
      ENDIF
   NEXT
   RETURN aOut

/* combina os termos por bool (AND/OR), na ordem -- equivalente ao SQL plano */
STATIC FUNCTION NavMatch( hRow, aWheres )
   LOCAL lAcc := .T., lFirst := .T., w, lTerm, cBool, cKind
   FOR EACH w IN aWheres
      cKind := hb_HGetDef( w, "kind", "simple" )
      cBool := hb_HGetDef( w, "bool", "AND" )
      DO CASE
      CASE cKind == "simple"
         lTerm := NavCmp( hb_HGetDef( hRow, Lower( w[ "col" ] ), NIL ), w[ "op" ], w[ "val" ] )
      CASE cKind == "in"
         lTerm := ( AScan( w[ "vals" ], ;
                    {| x | NavEq( hb_HGetDef( hRow, Lower( w[ "col" ] ), NIL ), x ) } ) > 0 )
      OTHERWISE
         lTerm := .F.
      ENDCASE
      IF lFirst
         lAcc := lTerm ; lFirst := .F.
      ELSEIF cBool == "OR"
         lAcc := lAcc .OR. lTerm
      ELSE
         lAcc := lAcc .AND. lTerm
      ENDIF
   NEXT
   RETURN lAcc

STATIC FUNCTION NavEq( xA, xB )
   IF xA == NIL .OR. xB == NIL
      RETURN ( xA == NIL .AND. xB == NIL )
   ENDIF
   IF HB_ISNUMERIC( xA ) .AND. HB_ISNUMERIC( xB ) ; RETURN xA == xB ; ENDIF
   RETURN hb_CStr( xA ) == hb_CStr( xB )

STATIC FUNCTION NavCmp( xVal, cOp, xRef )
   DO CASE
   CASE cOp == "="  .OR. cOp == "=="  ; RETURN NavEq( xVal, xRef )
   CASE cOp == "!=" .OR. cOp == "<>"  ; RETURN ! NavEq( xVal, xRef )
   ENDCASE
   IF xVal == NIL .OR. xRef == NIL
      RETURN .F.
   ENDIF
   DO CASE
   CASE cOp == "<"  ; RETURN xVal <  xRef
   CASE cOp == "<=" ; RETURN xVal <= xRef
   CASE cOp == ">"  ; RETURN xVal >  xRef
   CASE cOp == ">=" ; RETURN xVal >= xRef
   CASE Upper( cOp ) == "LIKE"
      // % wildcards stripped; pattern becomes a case-insensitive contains match
      RETURN ( StrTran( Upper( hb_CStr( xRef ) ), "%", "" ) $ Upper( hb_CStr( xVal ) ) )
   ENDCASE
   RETURN .F.

/* ordena por colunas {col,dir}; estavel o suficiente p/ o smoke (sort em copia) */
STATIC FUNCTION NavApplyOrder( aRows, aOrders )
   LOCAL aOut, a, cCol, lDesc
   IF aOrders == NIL .OR. Len( aOrders ) == 0
      RETURN aRows
   ENDIF
   aOut := AClone( aRows )
   FOR EACH a IN aOrders DESCEND                       // estabiliza multi-coluna (ultima 1o)
      cCol  := Lower( a[ 1 ] )
      lDesc := ( Upper( a[ 2 ] ) == "DESC" )
      ASort( aOut,,, {| x, y | NavOrderBlk( x, y, cCol, lDesc ) } )
   NEXT
   RETURN aOut

STATIC FUNCTION NavOrderBlk( x, y, cCol, lDesc )
   LOCAL xa := hb_HGetDef( x, cCol, NIL ), xb := hb_HGetDef( y, cCol, NIL )
   IF xa == NIL ; RETURN ! lDesc ; ENDIF
   IF xb == NIL ; RETURN lDesc ; ENDIF
   RETURN iif( lDesc, xa > xb, xa < xb )

STATIC FUNCTION NavApplyLimit( aRows, nLimit, nOffset )
   LOCAL aOut := {}, i, nFrom, nTo
   IF nLimit == NIL .AND. nOffset == NIL
      RETURN aRows
   ENDIF
   nFrom := iif( nOffset == NIL, 1, nOffset + 1 )
   nTo   := iif( nLimit == NIL, Len( aRows ), nFrom + nLimit - 1 )
   FOR i := nFrom TO Min( nTo, Len( aRows ) )
      AAdd( aOut, aRows[ i ] )
   NEXT
   RETURN aOut
