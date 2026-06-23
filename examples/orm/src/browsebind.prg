/* browsebind.prg -- conveniencias de UI p/ o grid: metadados de coluna por tipo
   (ColMeta) e a fabrica PURA de code blocks do xBrowse (ORM_BrowseBlocks, Task 6).
   Tudo Harbour puro/testavel headless; a fiacao do TXBrowse fica no exemplo. */
#include "hborm.ch"

/* tipo (vocabulario ORM_Cast) -> metadados de exibicao. cType ex.: "integer",
   "decimal:2", "string", "date", "datetime", "boolean", "text". */
FUNCTION ColMeta( cName, cType, xSample )
   LOCAL hMeta := hb_Hash(), nDec, cBase := cType, nW, cPic := NIL
   HB_SYMBOL_UNUSED( xSample )
   IF ":" $ cType
      cBase := SubStr( cType, 1, At( ":", cType ) - 1 )
      nDec  := Val( SubStr( cType, At( ":", cType ) + 1 ) )
   ENDIF
   DO CASE
   CASE cBase == "integer" ; nW := 8  ; cPic := "@Z 999999999"
   CASE cBase == "decimal"
      IF nDec == NIL ; nDec := 2 ; ENDIF
      nW  := 14
      cPic := "@Z 999,999,999." + Replicate( "9", nDec )
   CASE cBase == "date"     ; nW := 10 ; cPic := "@D"
   CASE cBase == "datetime" ; nW := 19
   CASE cBase == "boolean"  ; nW := 3
   CASE cBase == "text"     ; nW := 30
   OTHERWISE                ; nW := 20      // string e desconhecidos
   ENDCASE
   hMeta[ "name" ]    := cName
   hMeta[ "type" ]    := cType
   hMeta[ "heading" ] := MetaHeading( cName )
   hMeta[ "width" ]   := nW
   hMeta[ "picture" ] := cPic
   RETURN hMeta

/* fabrica PURA de code blocks p/ um TXBrowse: cada block dirige a fonte. O
   exemplo (examples/grid_fivewin.prg) so atribui estes blocks ao oBrw e adiciona
   colunas. Mantido aqui (sem FiveWin) p/ ser testavel headless. */
FUNCTION ORM_BrowseBlocks( oSource )
   LOCAL h := hb_Hash(), aCols := {}, hC, cName
   h[ "gotop" ]    := {|| oSource:GoTop() }
   h[ "gobottom" ] := {|| oSource:GoBottom() }
   h[ "skip" ]     := {| n | oSource:Skip( n ) }
   h[ "bof" ]      := {|| oSource:Bof() }
   h[ "eof" ]      := {|| oSource:Eof() }
   h[ "count" ]    := {|| oSource:Count() }
   h[ "recno" ]    := {|| oSource:RecNo() }
   FOR EACH hC IN oSource:Columns()
      cName := hC[ "name" ]
      AAdd( aCols, { "meta" => hC, "data" => BrowseColBlock( oSource, cName ) } )
   NEXT
   h[ "cols" ] := aCols
   RETURN h

/* bloco isolado por coluna -- captura cName por valor (FOR EACH reusa a var) */
STATIC FUNCTION BrowseColBlock( oSource, cName )
   RETURN {|| oSource:Value( cName ) }

/* "nome_cliente" -> "Nome Cliente" (conveniencia; o dev sobrepoe via aCols) */
STATIC FUNCTION MetaHeading( cName )
   LOCAL c := StrTran( cName, "_", " " ), i, cOut := "", lUp := .T.
   FOR i := 1 TO Len( c )
      cOut += iif( lUp, Upper( SubStr( c, i, 1 ) ), SubStr( c, i, 1 ) )
      lUp := ( SubStr( c, i, 1 ) == " " )
   NEXT
   RETURN cOut
