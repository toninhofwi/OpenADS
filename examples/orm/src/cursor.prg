/* cursor.prg -- cursor navegacional PERSISTENTE: abre o handle uma vez e mantem
   posicao (GoTop/GoBottom/Skip/Bof/Eof/RecNo/Count/GetField). Lastro do modo
   "lazy" do grid e de qualquer varredura streaming. Honra deletado via
   hbo_IsDeleted() explicito (o flag global de ocultacao de registros apagados pode
   nao filtrar em todos os contextos deste backend; defensivo = checar sempre via
   hbo_IsDeleted).
   RecNo/Count sao LOGICOS sobre registros vivos (1-based), p/ casar com o modo array. */
#include "hborm.ch"
#include "error.ch"

CREATE CLASS TORMCursor
   DATA oConn
   DATA cTable  INIT ""
   DATA nTbl    INIT 0
   DATA aNames    INIT {}
   DATA aRawNames INIT {}   // nomes de campo sem Lower(); reutilizados em Row()
   DATA nPos    INIT 0      // ordinal logico corrente; 0 = bof/vazio
   DATA nCount  INIT NIL    // cache lazy da contagem viva; NIL = nao calculado
   METHOD New( oConn, cTable ) CONSTRUCTOR
   METHOD Open()
   METHOD GoTop()
   METHOD GoBottom()
   METHOD Skip( n )
   METHOD Bof()      INLINE iif( ::nTbl == 0, .T., hbo_Bof( ::nTbl ) )
   METHOD Eof()      INLINE iif( ::nTbl == 0, .T., hbo_Eof( ::nTbl ) )
   METHOD RecNo()    INLINE ::nPos
   METHOD Count()
   METHOD GetField( cName )
   METHOD FieldNames() INLINE ::aNames
   METHOD Row()
   METHOD Close()
   /* aliases PT */
   METHOD Abrir()           INLINE ::Open()
   METHOD IrTopo()          INLINE ::GoTop()
   METHOD IrFim()           INLINE ::GoBottom()
   METHOD Pular( n )        INLINE ::Skip( n )
   METHOD Topo()            INLINE ::Bof()
   METHOD Fim()             INLINE ::Eof()
   METHOD RegAtual()        INLINE ::RecNo()
   METHOD Contar()          INLINE ::Count()
   METHOD Campo( cName )    INLINE ::GetField( cName )
   METHOD Campos()          INLINE ::FieldNames()
   METHOD Linha()           INLINE ::Row()
   METHOD Fechar()          INLINE ::Close()
   /* aliases ES */
   METHOD IrInicio()        INLINE ::GoTop()
   METHOD IrFinal()         INLINE ::GoBottom()
   METHOD Saltar( n )       INLINE ::Skip( n )
   METHOD Inicio()          INLINE ::Bof()
   METHOD Final()           INLINE ::Eof()
   METHOD RegActual()       INLINE ::RecNo()
   METHOD Fila()            INLINE ::Row()
   METHOD Cerrar()          INLINE ::Close()
END CLASS

METHOD New( oConn, cTable ) CLASS TORMCursor
   ::oConn  := oConn
   ::cTable := cTable
   RETURN Self

METHOD Open() CLASS TORMCursor
   LOCAL n, i, cTab, cRaw
   IF ! ::oConn:IsNavigational()
      NavUnsupported( "TORMCursor:Open", "cursor lazy requer backend navegacional" )
   ENDIF
   cTab := TORMGrammar():New():QuoteIdent( ::cTable )
   ::nTbl := hbo_OpenTable( ::oConn:Handle(), cTab )
   IF ::nTbl == 0
      NavUnsupported( "TORMCursor:Open", "tabela nao abre (navegacional)" )
   ENDIF
   ::nCount    := NIL   // limpa cache ao (re)abrir
   ::aNames    := {}
   ::aRawNames := {}
   n := hbo_NumFields( ::nTbl )
   FOR i := 1 TO n
      cRaw := AllTrim( hbo_FieldName( ::nTbl, i ) )
      AAdd( ::aRawNames, cRaw )
      AAdd( ::aNames, Lower( cRaw ) )
   NEXT
   RETURN ::GoTop()

/* GoTop: posiciona no PRIMEIRO registro vivo (nao-deletado). */
METHOD GoTop() CLASS TORMCursor
   hbo_GoTop( ::nTbl )
   /* o flag global de ocultacao pode nao filtrar neste backend -> skip sobre deletados explicitamente */
   DO WHILE ! hbo_Eof( ::nTbl ) .AND. hbo_IsDeleted( ::nTbl )
      hbo_Skip( ::nTbl, 1 )
   ENDDO
   ::nPos := iif( hbo_Eof( ::nTbl ), 0, 1 )
   RETURN Self

/* GoBottom: posiciona no ULTIMO registro vivo. */
METHOD GoBottom() CLASS TORMCursor
   hbo_GoBottom( ::nTbl )
   /* skip sobre deletados no final (para tras) */
   DO WHILE ! hbo_Bof( ::nTbl ) .AND. hbo_IsDeleted( ::nTbl )
      hbo_Skip( ::nTbl, -1 )
   ENDDO
   IF hbo_Bof( ::nTbl )
      // skip retrocedeu ate BOF: todos os registros estavam deletados -> posicao invalida
      ::nPos := 0
   ELSE
      ::nPos := iif( hbo_Eof( ::nTbl ), 0, ::Count() )
   ENDIF
   RETURN Self

/* Skip(n): avanca/recua n registros vivos. Cada "passo" e sobre 1 registro vivo.
   Para frente (n>0): skip fisico; pula deletados ate achar vivo ou EOF.
   Para tras (n<0): idem em sentido inverso. */
METHOD Skip( n ) CLASS TORMCursor
   LOCAL nStep, nDir, i
   IF n == NIL ; n := 1 ; ENDIF
   nDir  := iif( n > 0, 1, -1 )
   nStep := iif( n > 0, n, -n )
   FOR i := 1 TO nStep
      hbo_Skip( ::nTbl, nDir )
      /* pula registros deletados no mesmo sentido */
      IF nDir > 0
         DO WHILE ! hbo_Eof( ::nTbl ) .AND. hbo_IsDeleted( ::nTbl )
            hbo_Skip( ::nTbl, 1 )
         ENDDO
      ELSE
         DO WHILE ! hbo_Bof( ::nTbl ) .AND. hbo_IsDeleted( ::nTbl )
            hbo_Skip( ::nTbl, -1 )
         ENDDO
      ENDIF
      IF hbo_Eof( ::nTbl ) .OR. hbo_Bof( ::nTbl )
         EXIT
      ENDIF
   NEXT
   DO CASE
   CASE hbo_Eof( ::nTbl )  ; ::nPos := ::Count() + 1
   CASE hbo_Bof( ::nTbl )  ; ::nPos := 0
   OTHERWISE
      ::nPos := iif( nDir > 0, ::nPos + nStep, ::nPos - nStep )
   ENDCASE
   RETURN Self

/* contagem viva: handle SEPARADO p/ nao perturbar o cursor corrente.
   Honra deletados via hbo_IsDeleted explicito (mesma politica do GoTop/Skip).
   Cacheada; invalida depois de Close()+Open(). */
METHOD Count() CLASS TORMCursor
   LOCAL nTbl, n := 0
   IF ::nCount != NIL
      RETURN ::nCount
   ENDIF
   nTbl := hbo_OpenTable( ::oConn:Handle(), TORMGrammar():New():QuoteIdent( ::cTable ) )
   IF nTbl == 0
      RETURN 0
   ENDIF
   hbo_GoTop( nTbl )
   DO WHILE ! hbo_Eof( nTbl )
      IF ! hbo_IsDeleted( nTbl )       // conta so registros vivos
         n++
      ENDIF
      hbo_Skip( nTbl, 1 )
   ENDDO
   hbo_TableClose( nTbl )
   ::nCount := n
   RETURN n

METHOD GetField( cName ) CLASS TORMCursor
   RETURN hb_HGetDef( ::Row(), Lower( cName ), NIL )

METHOD Row() CLASS TORMCursor
   IF ::nTbl == 0 .OR. hbo_Eof( ::nTbl ) .OR. hbo_Bof( ::nTbl )
      RETURN hb_Hash()
   ENDIF
   // aRawNames foi populado em Open() uma unica vez; reutilizar evita alocacao por chamada.
   // aNames (lowercase) fica disponivel via FieldNames().
   RETURN NavHydrateRowPub( ::nTbl, ::aRawNames )

METHOD Close() CLASS TORMCursor
   IF ::nTbl != 0
      hbo_TableClose( ::nTbl )
      ::nTbl := 0
   ENDIF
   ::nCount := NIL   // invalida cache; proximo Open() vai recalcular
   RETURN Self
