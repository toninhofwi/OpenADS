/* browsesource.prg -- adapter de fonte de dados p/ grid. Uma interface de
   navegacao (GoTop/GoBottom/Skip/Bof/Eof/RecNo/Count/Value) sobre DOIS lastros:
   array em RAM (models ou row-hashes) e cursor lazy (TORMCursor). A fiacao do
   xBrowse (examples/) dirige SO esta interface -- identica nos dois modos. */
#include "hborm.ch"

CREATE CLASS TORMBrowseSource
   DATA lArray  INIT .T.
   DATA aItems  INIT {}
   DATA nIdx    INIT 0
   DATA oCursor INIT NIL
   DATA aCols   INIT NIL
   METHOD New() CONSTRUCTOR
   METHOD FromArray( aItems, aCols )
   METHOD FromCursor( oCursor, aCols )
   METHOD GoTop()
   METHOD GoBottom()
   METHOD Skip( n )
   METHOD Bof()
   METHOD Eof()
   METHOD RecNo()
   METHOD Count()
   METHOD Value( cCol )
   METHOD Columns()
   METHOD DeriveColsCursor()
   METHOD DeriveColsArray()
   /* aliases PT */
   METHOD DeArray( aItems, aCols ) INLINE ::FromArray( aItems, aCols )
   METHOD DeCursor( oCursor, aCols ) INLINE ::FromCursor( oCursor, aCols )
   METHOD IrTopo()    INLINE ::GoTop()
   METHOD IrFim()     INLINE ::GoBottom()
   METHOD Pular( n )  INLINE ::Skip( n )
   METHOD Topo()      INLINE ::Bof()
   METHOD Fim()       INLINE ::Eof()
   METHOD RegAtual()  INLINE ::RecNo()
   METHOD Contar()    INLINE ::Count()
   METHOD Valor( cCol ) INLINE ::Value( cCol )
   METHOD Colunas()   INLINE ::Columns()
   /* aliases ES */
   METHOD DesdeArray( aItems, aCols )   INLINE ::FromArray( aItems, aCols )
   METHOD DesdeCursor( oCursor, aCols ) INLINE ::FromCursor( oCursor, aCols )
   METHOD IrInicio()  INLINE ::GoTop()
   METHOD IrFinal()   INLINE ::GoBottom()
   METHOD Saltar( n ) INLINE ::Skip( n )
   METHOD Inicio()    INLINE ::Bof()
   METHOD Final()     INLINE ::Eof()
   METHOD RegActual() INLINE ::RecNo()
   METHOD Columnas()  INLINE ::Columns()
END CLASS

METHOD New() CLASS TORMBrowseSource
   RETURN Self

METHOD FromArray( aItems, aCols ) CLASS TORMBrowseSource
   ::lArray := .T.
   ::aItems := iif( aItems == NIL, {}, aItems )
   ::aCols  := aCols
   ::nIdx   := iif( Len( ::aItems ) == 0, 0, 1 )
   RETURN Self

METHOD FromCursor( oCursor, aCols ) CLASS TORMBrowseSource
   ::lArray  := .F.
   ::aItems  := {}
   ::oCursor := oCursor
   ::aCols   := aCols
   RETURN Self

METHOD GoTop() CLASS TORMBrowseSource
   IF ::lArray ; ::nIdx := iif( Len( ::aItems ) == 0, 0, 1 ) ; RETURN Self ; ENDIF
   ::oCursor:GoTop() ; RETURN Self

METHOD GoBottom() CLASS TORMBrowseSource
   IF ::lArray ; ::nIdx := Len( ::aItems ) ; RETURN Self ; ENDIF
   ::oCursor:GoBottom() ; RETURN Self

METHOD Skip( n ) CLASS TORMBrowseSource
   IF n == NIL ; n := 1 ; ENDIF
   IF ::lArray
      ::nIdx += n
      IF ::nIdx < 1            ; ::nIdx := 0 ; ENDIF                 // bof
      IF ::nIdx > Len(::aItems); ::nIdx := Len(::aItems) + 1 ; ENDIF // eof
      RETURN Self
   ENDIF
   ::oCursor:Skip( n ) ; RETURN Self

METHOD Bof() CLASS TORMBrowseSource
   IF ::lArray ; RETURN ( ::nIdx < 1 ) ; ENDIF
   RETURN ::oCursor:Bof()

METHOD Eof() CLASS TORMBrowseSource
   IF ::lArray ; RETURN ( Len( ::aItems ) == 0 .OR. ::nIdx > Len( ::aItems ) ) ; ENDIF
   RETURN ::oCursor:Eof()

METHOD RecNo() CLASS TORMBrowseSource
   IF ::lArray ; RETURN ::nIdx ; ENDIF
   RETURN ::oCursor:RecNo()

METHOD Count() CLASS TORMBrowseSource
   IF ::lArray ; RETURN Len( ::aItems ) ; ENDIF
   RETURN ::oCursor:Count()

METHOD Value( cCol ) CLASS TORMBrowseSource
   LOCAL xItem
   IF ! ::lArray
      RETURN ::oCursor:GetField( cCol )
   ENDIF
   IF ::nIdx < 1 .OR. ::nIdx > Len( ::aItems )
      RETURN NIL
   ENDIF
   xItem := ::aItems[ ::nIdx ]
   IF HB_ISHASH( xItem )
      RETURN hb_HGetDef( xItem, Lower( cCol ), NIL )
   ELSEIF HB_ISOBJECT( xItem )
      RETURN xItem:Get( Lower( cCol ) )
   ENDIF
   RETURN NIL

METHOD Columns() CLASS TORMBrowseSource
   IF ::aCols != NIL
      RETURN ::aCols
   ENDIF
   ::aCols := iif( ::lArray, ::DeriveColsArray(), ::DeriveColsCursor() )
   RETURN ::aCols

/* modo cursor: introspeccao da tabela (tem conn+table) -> ColMeta por coluna */
METHOD DeriveColsCursor() CLASS TORMBrowseSource
   LOCAL aOut := {}, hC
   FOR EACH hC IN ORM_Introspect( ::oCursor:oConn, ::oCursor:cTable )
      AAdd( aOut, ColMeta( Lower( hC[ "nome" ] ), hC[ "cast" ], NIL ) )
   NEXT
   RETURN aOut

/* modo array: sniff do tipo pelo valor da 1a linha (sem catalogo) */
METHOD DeriveColsArray() CLASS TORMBrowseSource
   LOCAL aOut := {}, xItem, cK, xV, hRow
   IF Len( ::aItems ) == 0 ; RETURN {} ; ENDIF
   xItem := ::aItems[ 1 ]
   hRow  := iif( HB_ISHASH( xItem ), xItem, iif( HB_ISOBJECT( xItem ), xItem:hAttrs, hb_Hash() ) )
   FOR EACH cK IN hb_HKeys( hRow )
      xV := hRow[ cK ]
      AAdd( aOut, ColMeta( Lower( cK ), SniffType( xV ), xV ) )
   NEXT
   RETURN aOut

STATIC FUNCTION SniffType( xV )
   DO CASE
   CASE HB_ISNUMERIC( xV ) ; RETURN iif( xV == Int( xV ), "integer", "decimal:2" )
   CASE HB_ISDATE( xV )    ; RETURN "date"
   CASE HB_ISLOGICAL( xV ) ; RETURN "boolean"
   ENDCASE
   RETURN "string"
