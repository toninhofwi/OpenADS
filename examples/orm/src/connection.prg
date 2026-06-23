/* connection.prg -- conexao parametrizada na ABI, com cache de prepared. */
#include "hborm.ch"
#include "error.ch"

CREATE CLASS TORMConnection
   DATA nConn   INIT 0
   DATA cUri    INIT ""
   DATA hCache                                   // SQL -> nStmt
   DATA lNav      INIT .F.
   DATA nQueries  INIT 0
   DATA nTxDepth        INIT 0
   DATA lTxRollbackOnly INIT .F.
   METHOD New( cUri ) CONSTRUCTOR
   METHOD IsOpen()         INLINE ::nConn != 0
   METHOD IsNavigational() INLINE ::lNav
   METHOD Handle()         INLINE ::nConn
   METHOD Execute( cSql, aParams )
   METHOD Query( cSql, aParams )
   METHOD Raw( cSql, aParams )   INLINE ::Query( cSql, aParams )
   METHOD Close()
   METHOD PrepCached( cSql )                     // -> nStmt (0 em falha)
   METHOD Bind( nStmt, aParams )                 // aplica os binds
   METHOD QueryCount()      INLINE ::nQueries
   METHOD ResetQueryCount() INLINE ( ::nQueries := 0, Self )
   METHOD CreateIndex( cTable, aCols, lUnique, cName )
   METHOD DropIndex( cName )
   METHOD Begin()
   METHOD Commit()
   METHOD Rollback()
   METHOD Transaction( bBlk )
   METHOD InTransaction() INLINE ::nTxDepth > 0
   METHOD Iniciar()         INLINE ::Begin()
   METHOD Confirmar()       INLINE ::Commit()
   METHOD Desfazer()        INLINE ::Rollback()
   METHOD Transacao( bBlk ) INLINE ::Transaction( bBlk )
   METHOD EmTransacao()     INLINE ::nTxDepth > 0
END CLASS

METHOD New( cUri ) CLASS TORMConnection
   LOCAL cArg := AllTrim( cUri )
   ::cUri   := cUri
   ::hCache := hb_Hash()
   /* Capacidade pelo scheme: sqlite:// fala SQL; qualquer outro backend
      (DBF nativo, tcp:// servidor, pg/maria/odbc) expoe o cursor de tabela
      -> caminho navegacional. */
   ::lNav   := ! ( Lower( Left( cArg, 9 ) ) == "sqlite://" )
   /* dbf://<dir>: o engine conecta no diretorio; tiramos o prefixo do scheme. */
   IF Lower( Left( cArg, 6 ) ) == "dbf://"
      cArg := SubStr( cArg, 7 )
   ENDIF
   ::nConn  := hbo_Connect( cArg )
   IF ::nConn != 0 .AND. ::lNav
      /* ORM policy: deleted rows are gone. NOTE: AdsShowDeleted is a PROCESS-GLOBAL
         engine setting (not per-connection) and is intentionally NOT restored on
         Close() -- every navigational connection in the process shares this policy.
         A per-table save/restore is deferred to the navigational-CRUD/dialects slice. */
      hbo_ShowDeleted( .F. )
   ENDIF
   RETURN Self

METHOD PrepCached( cSql ) CLASS TORMConnection
   LOCAL nStmt
   IF hb_HHasKey( ::hCache, cSql )
      RETURN ::hCache[ cSql ]
   ENDIF
   nStmt := hbo_Prepare( ::nConn, cSql )
   IF nStmt != 0
      ::hCache[ cSql ] := nStmt
   ENDIF
   RETURN nStmt

METHOD Bind( nStmt, aParams ) CLASS TORMConnection
   LOCAL p, xV, lOk := .T.
   IF aParams == NIL
      RETURN .T.
   ENDIF
   FOR EACH p IN aParams
      xV := p[ 2 ]
      DO CASE
      CASE xV == NIL           ; lOk := hbo_BindNull( nStmt, p[ 1 ] ) .AND. lOk
      CASE HB_ISNUMERIC( xV )  ; lOk := hbo_BindNum( nStmt, p[ 1 ], xV ) .AND. lOk
      CASE HB_ISLOGICAL( xV )  ; lOk := hbo_BindLog( nStmt, p[ 1 ], xV ) .AND. lOk
      CASE HB_ISDATE( xV )     ; lOk := hbo_BindStr( nStmt, p[ 1 ], DToS( xV ) ) .AND. lOk
      OTHERWISE                ; lOk := hbo_BindStr( nStmt, p[ 1 ], hb_CStr( xV ) ) .AND. lOk
      ENDCASE
   NEXT
   RETURN lOk

METHOD Execute( cSql, aParams ) CLASS TORMConnection
   LOCAL nStmt, nCur := 0, lOk
   IF ::nConn == 0
      RETURN .F.
   ENDIF
   nStmt := ::PrepCached( cSql )
   IF nStmt == 0
      RETURN .F.
   ENDIF
   IF ! ::Bind( nStmt, aParams )
      RETURN .F.
   ENDIF
   lOk := hbo_ExecPrep( nStmt, @nCur )
   IF nCur != 0
      hbo_TableClose( nCur )                     // DDL/DML nao deve produzir cursor
   ENDIF
   RETURN lOk

METHOD Query( cSql, aParams ) CLASS TORMConnection
   LOCAL nStmt, nCur := 0, aRows := {}, aNames := {}, i, nFields, hRow
   IF ::nConn == 0
      RETURN aRows
   ENDIF
   ::nQueries++
   nStmt := ::PrepCached( cSql )
   IF nStmt == 0 .OR. ! ::Bind( nStmt, aParams ) .OR. ! hbo_ExecPrep( nStmt, @nCur ) .OR. nCur == 0
      RETURN aRows
   ENDIF
   nFields := hbo_NumFields( nCur )
   FOR i := 1 TO nFields
      AAdd( aNames, hbo_FieldName( nCur, i ) )
   NEXT
   hbo_GoTop( nCur )
   DO WHILE ! hbo_Eof( nCur )
      hRow := hb_Hash()
      FOR i := 1 TO nFields
         hRow[ aNames[ i ] ] := HydrateField( nCur, aNames[ i ] )
      NEXT
      AAdd( aRows, hRow )
      hbo_Skip( nCur, 1 )
   ENDDO
   hbo_TableClose( nCur )
   RETURN aRows

METHOD Close() CLASS TORMConnection
   LOCAL cK
   IF ::nConn != 0
      FOR EACH cK IN hb_HKeys( ::hCache )
         hbo_StmtClose( ::hCache[ cK ] )
      NEXT
      ::hCache := hb_Hash()
      hbo_Disconnect( ::nConn )
      ::nConn := 0
   ENDIF
   RETURN NIL

METHOD CreateIndex( cTable, aCols, lUnique, cName ) CLASS TORMConnection
   LOCAL oG, r, cIdx
   IF aCols == NIL .OR. Len( aCols ) == 0
      RETURN .F.
   ENDIF
   cIdx := iif( cName == NIL .OR. Empty( cName ), ;
               DefaultIdxName( cTable, aCols, lUnique == .T. ), cName )
   oG := TORMGrammar():New()
   r  := oG:Compile( { "type" => "createIndex", "index" => cIdx, "table" => cTable, ;
                       "columns" => aCols, "unique" => ( lUnique == .T. ) } )
   RETURN ::Execute( r[ "sql" ], r[ "params" ] )

METHOD DropIndex( cName ) CLASS TORMConnection
   LOCAL oG := TORMGrammar():New(), r
   r := oG:Compile( { "type" => "dropIndex", "index" => cName } )
   RETURN ::Execute( r[ "sql" ], r[ "params" ] )

/* nome de indice deterministico e valido (ix_/ux_ + tabela + colunas) */
STATIC FUNCTION DefaultIdxName( cTable, aCols, lUnique )
   LOCAL c := iif( lUnique, "ux_", "ix_" ) + cTable, cCol
   FOR EACH cCol IN aCols
      c += "_" + cCol
   NEXT
   RETURN c

/* Indirecao de provider: hoje uma conexao default thread-static (opt-in);
   amanha um pool entra AQUI sem tocar os call-sites. */
FUNCTION TORMConnection_Default( oConn )
   THREAD STATIC t_oDefault
   IF PCount() > 0
      t_oDefault := oConn
   ENDIF
   RETURN t_oDefault

/* Le um campo do cursor ja tipado pelo catalogo do engine.
   No passthrough SQL os getters nativos (AdsGetDouble/AdsIsNull) sao inertes,
   entao lemos a STRING e convertemos pela tag do campo em Harbour. NULL numerico
   chega como "" -> NIL (em coluna texto o engine nao distingue NULL de "").
   Logico/data/datetime ficam string aqui; a camada de cast do Model os coage. */
STATIC FUNCTION HydrateField( nCur, cName )
   LOCAL cTag := hbo_FieldType( nCur, cName )
   LOCAL cRaw := hbo_Field( nCur, cName )
   IF cTag != "C" .AND. Empty( cRaw )
      RETURN NIL
   ENDIF
   IF cTag == "N"
      RETURN Val( cRaw )
   ENDIF
   RETURN cRaw

/* Transação reentrante-plana sobre o backend SQL (sqlite passthrough): controle
   SQL-level BEGIN/COMMIT/ROLLBACK via Execute -- a ABI de transação do engine não
   está ligada a esse backend (devolve erro), mas o SQL cru é transacional (inclui
   DDL). Só o nível mais EXTERNO (nTxDepth 0->1) toca o engine; níveis internos só
   ajustam a profundidade. Qualquer Rollback marca a transação inteira rollback-only.
   Backend navegacional (tx via ABI) -> fatia navegacional/dialetos. */
METHOD Begin() CLASS TORMConnection
   IF ::nTxDepth == 0
      IF ! ::Execute( "BEGIN", {} )
         TxRaise( "falha ao iniciar transacao" )
      ENDIF
      ::lTxRollbackOnly := .F.
   ENDIF
   ::nTxDepth++
   RETURN Self

METHOD Commit() CLASS TORMConnection
   IF ::nTxDepth == 0
      TxRaise( "commit sem transacao ativa" )
   ENDIF
   ::nTxDepth--
   IF ::nTxDepth == 0
      IF ::lTxRollbackOnly
         ::Execute( "ROLLBACK", {} )
         TxRaise( "transacao desfeita (rollback-only)" )
      ELSEIF ! ::Execute( "COMMIT", {} )
         TxRaise( "falha ao confirmar transacao" )
      ENDIF
   ENDIF
   RETURN Self

METHOD Rollback() CLASS TORMConnection
   IF ::nTxDepth == 0
      TxRaise( "rollback sem transacao ativa" )
   ENDIF
   ::lTxRollbackOnly := .T.
   ::nTxDepth--
   IF ::nTxDepth == 0
      IF ! ::Execute( "ROLLBACK", {} )
         TxRaise( "falha ao desfazer transacao" )
      ENDIF
   ENDIF
   RETURN Self

/* Caminho idiomatico seguro: inicia, roda o bloco (recebe Self), confirma no
   sucesso; em QUALQUER excecao desfaz e RE-LEVANTA (nunca engole o erro do bloco). */
METHOD Transaction( bBlk ) CLASS TORMConnection
   LOCAL xRet, oErr
   ::Begin()
   BEGIN SEQUENCE WITH {| e | Break( e ) }
      xRet := Eval( bBlk, Self )
      ::Commit()
   RECOVER USING oErr
      ::Rollback()                                // marca rollback-only + desfaz no nivel externo
      Eval( ErrorBlock(), oErr )                  // re-levanta: nunca engole o erro do bloco
   END SEQUENCE
   RETURN xRet

STATIC PROCEDURE TxRaise( cWhy )
   LOCAL oErr := ErrorNew()
   oErr:Subsystem( "hb_orm" )
   oErr:SubCode( 1018 )
   oErr:Severity( ES_ERROR )
   oErr:Description( cWhy )                        // sem SQL/path
   oErr:Operation( "TORMConnection:Transaction" )
   oErr:CanRetry( .F. )
   Eval( ErrorBlock(), oErr )
   RETURN
