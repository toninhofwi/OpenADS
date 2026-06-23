/* grammar.prg -- AST -> { sql, params }. Sem literais: placeholders + bind. */
#include "hborm.ch"
#include "error.ch"

CREATE CLASS TORMGrammar
   METHOD New() CONSTRUCTOR
   METHOD Compile( hAst )
   METHOD QuoteIdent( cName )
   METHOD CompileWheres( aWheres, aParams )   // -> "" ou " WHERE ..."
   METHOD BuildPredicates( aList, aParams )
   METHOD CompileSimple( hItem, aParams )
   METHOD CompileSimpleArr( aItem, aParams )
   METHOD CompileIn( hItem, aParams )
   METHOD CompileRaw( hItem, aParams )
   METHOD QuoteQualified( cName )
   METHOD CompileJoins( aJoins )
   METHOD CompileGroups( aGroups )
   METHOD CompileHaving( aHavings, aParams )
   METHOD CompileAggregate( hAgg )
END CLASS

METHOD New() CLASS TORMGrammar
   RETURN Self

METHOD QuoteIdent( cName ) CLASS TORMGrammar
   LOCAL oErr
   IF ! HB_ISSTRING( cName ) .OR. ! hb_regexLike( "[A-Za-z_][A-Za-z0-9_]*", cName )
      oErr := ErrorNew()
      oErr:Subsystem( "hb_orm" )
      oErr:SubCode( 1001 )
      oErr:Severity( ES_ERROR )
      oErr:Description( "identificador invalido" )
      oErr:Operation( "QuoteIdent" )
      oErr:CanRetry( .F. )
      Eval( ErrorBlock(), oErr )
   ENDIF
   RETURN cName

/* registra um valor e devolve o placeholder ":pN" (sem mexer no valor) */
STATIC FUNCTION Bind( aParams, xVal )
   LOCAL nSeq := Len( aParams ) + 1
   AAdd( aParams, { "p" + LTrim( Str( nSeq ) ), xVal } )
   RETURN ":p" + LTrim( Str( nSeq ) )

STATIC PROCEDURE OrmRaise( nCode, cOp, cDesc )
   LOCAL oErr := ErrorNew()
   oErr:Subsystem( "hb_orm" )
   oErr:SubCode( nCode )
   oErr:Severity( ES_ERROR )
   oErr:Description( cDesc )
   oErr:Operation( cOp )
   oErr:CanRetry( .F. )
   Eval( ErrorBlock(), oErr )
   RETURN

METHOD CompileWheres( aWheres, aParams ) CLASS TORMGrammar
   LOCAL c := ::BuildPredicates( aWheres, aParams )
   RETURN iif( Empty( c ), "", " WHERE " + c )

METHOD BuildPredicates( aList, aParams ) CLASS TORMGrammar
   LOCAL c := "", a, lFirst := .T., cBool, cKind
   IF aList == NIL .OR. Len( aList ) == 0
      RETURN ""
   ENDIF
   FOR EACH a IN aList
      IF HB_ISHASH( a )
         cBool := iif( lFirst, "", " " + hb_HGetDef( a, "bool", "AND" ) + " " )
         cKind := hb_HGetDef( a, "kind", "simple" )
         DO CASE
         CASE cKind == "simple" ; c += cBool + ::CompileSimple( a, aParams )
         CASE cKind == "in"     ; c += cBool + ::CompileIn( a, aParams )
         CASE cKind == "raw"    ; c += cBool + ::CompileRaw( a, aParams )
         OTHERWISE
            OrmRaise( 1003, "BuildPredicates", "kind invalido: " + hb_CStr( cKind ) )
         ENDCASE
      ELSE
         c += iif( lFirst, "", " AND " ) + ::CompileSimpleArr( a, aParams )
      ENDIF
      lFirst := .F.
   NEXT
   RETURN c

STATIC FUNCTION CheckOp( cOp )
   LOCAL c := Upper( AllTrim( cOp ) )
   IF !( c $ "=|!=|<>|<|>|<=|>=|LIKE" )
      OrmRaise( 1002, "CompileWheres", "operador nao permitido: " + c )
   ENDIF
   RETURN c

METHOD CompileSimple( hItem, aParams ) CLASS TORMGrammar
   RETURN ::QuoteQualified( hItem[ "col" ] ) + " " + CheckOp( hItem[ "op" ] ) + " " + ;
          Bind( aParams, hItem[ "val" ] )

METHOD CompileSimpleArr( aItem, aParams ) CLASS TORMGrammar
   RETURN ::QuoteQualified( aItem[ 1 ] ) + " " + CheckOp( aItem[ 2 ] ) + " " + ;
          Bind( aParams, aItem[ 3 ] )

METHOD CompileIn( hItem, aParams ) CLASS TORMGrammar
   LOCAL aVals := hItem[ "vals" ], aPh := {}, x
   IF aVals == NIL .OR. Len( aVals ) == 0
      OrmRaise( 1004, "CompileIn", "lista IN vazia" )
   ENDIF
   FOR EACH x IN aVals
      AAdd( aPh, Bind( aParams, x ) )
   NEXT
   RETURN ::QuoteQualified( hItem[ "col" ] ) + " IN ( " + JoinStr( aPh, ", " ) + " )"

METHOD CompileRaw( hItem, aParams ) CLASS TORMGrammar
   RETURN RenumberRaw( hItem[ "frag" ], hb_HGetDef( hItem, "params", {} ), aParams )

STATIC FUNCTION RenumberRaw( cFrag, aVals, aParams )
   LOCAL i, c := cFrag, nCount := Len( aVals ), cPh, nFound := 0
   FOR i := 1 TO nCount
      IF ( ":p" + LTrim( Str( i ) ) ) $ cFrag
         nFound++
      ENDIF
   NEXT
   /* detecta placeholders sobrando no fragmento alem do que params cobre */
   IF ( ":p" + LTrim( Str( nCount + 1 ) ) ) $ cFrag
      nFound := -1   // forca mismatch
   ENDIF
   IF nFound != nCount
      OrmRaise( 1005, "WhereRaw", "placeholders do fragmento nao batem com params" )
   ENDIF
   FOR i := nCount TO 1 STEP -1
      c := StrTran( c, ":p" + LTrim( Str( i ) ), Chr( 1 ) + LTrim( Str( i ) ) + Chr( 1 ) )
   NEXT
   FOR i := 1 TO nCount
      cPh := Bind( aParams, aVals[ i ] )
      c := StrTran( c, Chr( 1 ) + LTrim( Str( i ) ) + Chr( 1 ), cPh )
   NEXT
   RETURN c

METHOD QuoteQualified( cName ) CLASS TORMGrammar
   LOCAL aParts, p
   IF ! HB_ISSTRING( cName )
      OrmRaise( 1008, "QuoteQualified", "identificador invalido" )
   ENDIF
   aParts := hb_ATokens( cName, "." )
   IF Len( aParts ) == 0 .OR. Len( aParts ) > 2
      OrmRaise( 1008, "QuoteQualified", "identificador qualificado invalido: " + cName )
   ENDIF
   FOR EACH p IN aParts
      ::QuoteIdent( p )                  // valida cada parte (levanta se invalida)
   NEXT
   RETURN cName

METHOD CompileJoins( aJoins ) CLASS TORMGrammar
   LOCAL c := "", j, cType, cOp
   IF aJoins == NIL .OR. Len( aJoins ) == 0
      RETURN ""
   ENDIF
   FOR EACH j IN aJoins
      cType := iif( Upper( j[ "type" ] ) == "LEFT", "LEFT", "INNER" )
      cOp   := Upper( AllTrim( j[ "op" ] ) )
      IF !( cOp $ "=|!=|<>|<|>|<=|>=" )
         OrmRaise( 1007, "CompileJoins", "operador de join invalido: " + cOp )
      ENDIF
      c += " " + cType + " JOIN " + ::QuoteIdent( j[ "table" ] ) + " ON " + ;
           ::QuoteQualified( j[ "c1" ] ) + " " + cOp + " " + ::QuoteQualified( j[ "c2" ] )
   NEXT
   RETURN c

METHOD CompileGroups( aGroups ) CLASS TORMGrammar
   LOCAL c := "", cCol
   IF aGroups == NIL .OR. Len( aGroups ) == 0
      RETURN ""
   ENDIF
   FOR EACH cCol IN aGroups
      c += iif( Empty( c ), "", ", " ) + ::QuoteQualified( cCol )
   NEXT
   RETURN " GROUP BY " + c

METHOD CompileHaving( aHavings, aParams ) CLASS TORMGrammar
   LOCAL c := ::BuildPredicates( aHavings, aParams )
   RETURN iif( Empty( c ), "", " HAVING " + c )

METHOD CompileAggregate( hAgg ) CLASS TORMGrammar
   LOCAL cFn := Upper( AllTrim( hAgg[ "fn" ] ) ), cCol := hb_HGetDef( hAgg, "col", NIL )
   IF !( cFn $ "COUNT|SUM|AVG|MIN|MAX" )
      OrmRaise( 1006, "CompileAggregate", "funcao agregada invalida: " + cFn )
   ENDIF
   RETURN cFn + "( " + iif( cCol == NIL .OR. Empty( cCol ), "*", ::QuoteQualified( cCol ) ) + " ) AS aggregate"

METHOD Compile( hAst ) CLASS TORMGrammar
   LOCAL cType := hAst[ "type" ]
   LOCAL cTable := iif( hb_HHasKey( hAst, "table" ), ::QuoteIdent( hAst[ "table" ] ), "" )
   LOCAL aParams := {}, c, aCols, aPh, cK, a, cCols, cSel, hAgg

   DO CASE
   CASE cType == "insert"
      aCols := {} ; aPh := {}
      FOR EACH cK IN hb_HKeys( hAst[ "values" ] )
         AAdd( aCols, ::QuoteIdent( cK ) )
         AAdd( aPh, Bind( aParams, hAst[ "values" ][ cK ] ) )
      NEXT
      c := "INSERT INTO " + cTable + " ( " + JoinStr( aCols, ", " ) + ;
           " ) VALUES ( " + JoinStr( aPh, ", " ) + " )"

   CASE cType == "createIndex"
      aCols := {}
      FOR EACH cK IN hAst[ "columns" ]
         AAdd( aCols, ::QuoteIdent( cK ) )
      NEXT
      c := "CREATE " + iif( hb_HGetDef( hAst, "unique", .F. ), "UNIQUE ", "" ) + ;
           "INDEX " + ::QuoteIdent( hAst[ "index" ] ) + " ON " + cTable + ;
           " ( " + JoinStr( aCols, ", " ) + " )"

   CASE cType == "dropIndex"
      c := "DROP INDEX " + ::QuoteIdent( hAst[ "index" ] )

   CASE cType == "insertMany"
      c := BuildValuesTuples( Self, hAst, aParams )

   CASE cType == "upsert"
      c := BuildValuesTuples( Self, hAst, aParams ) + CompileOnConflict( Self, hAst )

   CASE cType == "createTable"
      c := BuildCreateTable( Self, hAst )

   CASE cType == "dropTable"
      c := "DROP TABLE " + iif( hb_HGetDef( hAst, "ifExists", .F. ), "IF EXISTS ", "" ) + cTable

   CASE cType == "select"
      hAgg := hb_HGetDef( hAst, "aggregate", NIL )
      IF hAgg != NIL
         cSel := ::CompileAggregate( hAgg )
      ELSE
         cSel := iif( hAst[ "columns" ] == NIL .OR. Len( hAst[ "columns" ] ) == 0, "*", ;
                      JoinIdent( Self, hAst[ "columns" ] ) )
      ENDIF
      c := "SELECT " + cSel + " FROM " + cTable
      c += ::CompileJoins( hb_HGetDef( hAst, "joins", NIL ) )
      c += ::CompileWheres( hb_HGetDef( hAst, "wheres", NIL ), aParams )
      c += ::CompileGroups( hb_HGetDef( hAst, "groups", NIL ) )
      c += ::CompileHaving( hb_HGetDef( hAst, "havings", NIL ), aParams )
      IF hAgg == NIL
         IF hb_HGetDef( hAst, "orders", NIL ) != NIL .AND. Len( hAst[ "orders" ] ) > 0
            cCols := ""
            FOR EACH a IN hAst[ "orders" ]
               cCols += iif( Empty( cCols ), "", ", " ) + ::QuoteQualified( a[ 1 ] ) + " " + ;
                        iif( Upper( a[ 2 ] ) == "DESC", "DESC", "ASC" )
            NEXT
            c += " ORDER BY " + cCols
         ENDIF
         c += CompileLimitOffset( hb_HGetDef( hAst, "limit", NIL ), hb_HGetDef( hAst, "offset", NIL ) )
      ENDIF

   CASE cType == "update"
      c := ""
      FOR EACH cK IN hb_HKeys( hAst[ "values" ] )
         c += iif( Empty( c ), "", ", " ) + ::QuoteIdent( cK ) + " = " + ;
              Bind( aParams, hAst[ "values" ][ cK ] )
      NEXT
      c := "UPDATE " + cTable + " SET " + c + ;
           ::CompileWheres( hb_HGetDef( hAst, "wheres", NIL ), aParams )

   CASE cType == "delete"
      c := "DELETE FROM " + cTable + ::CompileWheres( hb_HGetDef( hAst, "wheres", NIL ), aParams )

   OTHERWISE
      c := ""
   ENDCASE

   RETURN { "sql" => c, "params" => aParams }

STATIC FUNCTION CompileLimitOffset( xLimit, xOffset )
   LOCAL c := ""
   IF xLimit != NIL
      c += " LIMIT " + LTrim( Str( Int( xLimit ) ) )
   ELSEIF xOffset != NIL
      c += " LIMIT -1"                       // SQLite exige LIMIT antes de OFFSET
   ENDIF
   IF xOffset != NIL
      c += " OFFSET " + LTrim( Str( Int( xOffset ) ) )
   ENDIF
   RETURN c

/* helpers file-local */
STATIC FUNCTION JoinStr( a, cSep )
   LOCAL c := "", i
   FOR i := 1 TO Len( a )
      c += iif( i == 1, "", cSep ) + a[ i ]
   NEXT
   RETURN c

STATIC FUNCTION JoinIdent( oG, aCols )
   LOCAL a := {}, c
   FOR EACH c IN aCols
      AAdd( a, oG:QuoteQualified( c ) )    // colunas de SELECT podem ser qualificadas (table.col) em joins
   NEXT
   RETURN JoinStr( a, ", " )

/* monta "INSERT INTO t ( cols ) VALUES ( ... ), ( ... )" com todos os valores
   bindados; valida linhas nao-vazias e aridade linha==colunas. Reusado por upsert.
   ⚠️ HAZARD DE ENGINE: o passthrough sqlite mal-mapeia os params nomeados quando
   ha VARIAS linhas (perto do teto ~18 params) -> grava dados EMBARALHADOS apesar de
   Execute() devolver .T. Por isso TORMModel:InsertMany/Upsert executam POR-LINHA.
   NAO Compile() um insertMany/upsert de >1 linha e Execute() direto ate o engine
   corrigir o binding (candidato a PR). O SQL gerado aqui esta CORRETO. */
STATIC FUNCTION BuildValuesTuples( oG, hAst, aParams )
   LOCAL aCols := hAst[ "columns" ], aRows := hAst[ "rows" ]
   LOCAL aColsQ := {}, aTuples := {}, aPh, aRow, xV, cK
   IF aRows == NIL .OR. Len( aRows ) == 0
      OrmRaise( 1009, "insertMany", "lista de linhas vazia" )
   ENDIF
   FOR EACH cK IN aCols
      AAdd( aColsQ, oG:QuoteIdent( cK ) )
   NEXT
   FOR EACH aRow IN aRows
      IF Len( aRow ) != Len( aCols )
         OrmRaise( 1010, "insertMany", "linha com aridade diferente das colunas" )
      ENDIF
      aPh := {}
      FOR EACH xV IN aRow
         AAdd( aPh, Bind( aParams, xV ) )
      NEXT
      AAdd( aTuples, "( " + JoinStr( aPh, ", " ) + " )" )
   NEXT
   RETURN "INSERT INTO " + oG:QuoteIdent( hAst[ "table" ] ) + " ( " + ;
          JoinStr( aColsQ, ", " ) + " ) VALUES " + JoinStr( aTuples, ", " )

/* renderiza " ON CONFLICT ( cols ) DO UPDATE SET c = excluded.c, ..." ou DO NOTHING.
   Dialeto SQLite/PostgreSQL (excluded.); MySQL/MariaDB diferem -> fatia de dialetos. */
STATIC FUNCTION CompileOnConflict( oG, hAst )
   LOCAL aConf := hAst[ "conflict" ], aUpd := hb_HGetDef( hAst, "update", {} )
   LOCAL aConfQ := {}, aSet := {}, cK
   IF aConf == NIL .OR. Len( aConf ) == 0
      OrmRaise( 1011, "upsert", "colunas de conflito vazias" )
   ENDIF
   FOR EACH cK IN aConf
      AAdd( aConfQ, oG:QuoteIdent( cK ) )
   NEXT
   IF aUpd == NIL .OR. Len( aUpd ) == 0
      RETURN " ON CONFLICT ( " + JoinStr( aConfQ, ", " ) + " ) DO NOTHING"
   ENDIF
   FOR EACH cK IN aUpd
      AAdd( aSet, oG:QuoteIdent( cK ) + " = excluded." + oG:QuoteIdent( cK ) )
   NEXT
   RETURN " ON CONFLICT ( " + JoinStr( aConfQ, ", " ) + " ) DO UPDATE SET " + ;
          JoinStr( aSet, ", " )

/* DDL de tabela: CREATE TABLE <ident> ( <coldef>, ... ). Sem bind (DDL nao aceita
   placeholder); identificadores via QuoteIdent, defaults via literal seguro. */
STATIC FUNCTION BuildCreateTable( oG, hAst )
   LOCAL aCols := hAst[ "columns" ], aDefs := {}, hCol
   IF aCols == NIL .OR. Len( aCols ) == 0
      OrmRaise( 1012, "createTable", "tabela sem colunas" )
   ENDIF
   FOR EACH hCol IN aCols
      AAdd( aDefs, ColDef( oG, hCol ) )
   NEXT
   RETURN "CREATE TABLE " + oG:QuoteIdent( hAst[ "table" ] ) + ;
          " ( " + JoinStr( aDefs, ", " ) + " )"

STATIC FUNCTION ColDef( oG, hCol )
   LOCAL c := oG:QuoteIdent( hCol[ "name" ] ) + " " + TypeToSql( hCol )
   LOCAL lPk := hb_HGetDef( hCol, "pk", .F. )
   IF lPk .AND. hb_HGetDef( hCol, "autoinc", .F. )
      c += " PRIMARY KEY AUTOINCREMENT"
   ENDIF
   IF ! hb_HGetDef( hCol, "nullable", .T. ) .AND. ! lPk
      c += " NOT NULL"
   ENDIF
   IF hb_HGetDef( hCol, "unique", .F. )
      c += " UNIQUE"
   ENDIF
   IF hb_HHasKey( hCol, "default" )
      c += " DEFAULT " + RenderDefaultLiteral( hCol[ "default" ] )
   ENDIF
   RETURN c

STATIC FUNCTION TypeToSql( hCol )
   LOCAL cT := Lower( AllTrim( hb_HGetDef( hCol, "type", "string" ) ) )
   LOCAL nPrec := hb_HGetDef( hCol, "prec", NIL )
   DO CASE
   CASE cT == "id" .OR. cT == "integer" ; RETURN "INTEGER"
   CASE cT == "boolean"                 ; RETURN "INTEGER"
   CASE cT == "text" .OR. cT == "json"  ; RETURN "TEXT"
   CASE cT == "date" .OR. cT == "datetime" ; RETURN "TEXT"
   CASE cT == "string"
      RETURN "VARCHAR(" + LTrim( Str( Int( hb_HGetDef( hCol, "len", 255 ) ) ) ) + ")"
   CASE cT == "decimal"
      IF nPrec == NIL
         RETURN "NUMERIC"
      ENDIF
      RETURN "NUMERIC(" + LTrim( Str( Int( nPrec ) ) ) + "," + ;
             LTrim( Str( Int( hb_HGetDef( hCol, "scale", 0 ) ) ) ) + ")"
   ENDCASE
   OrmRaise( 1013, "createTable", "tipo de coluna desconhecido: " + cT )
   RETURN ""

/* valor confiavel do dev (vem da migration, nunca de input de usuario), mas
   escapado mesmo assim para nao quebrar o DDL. */
STATIC FUNCTION RenderDefaultLiteral( xVal )
   DO CASE
   CASE xVal == NIL          ; RETURN "NULL"
   CASE HB_ISLOGICAL( xVal ) ; RETURN iif( xVal, "1", "0" )
   CASE HB_ISNUMERIC( xVal ) ; RETURN hb_ntos( xVal )
   CASE HB_ISTIMESTAMP( xVal ) ; RETURN "'" + hb_TToC( xVal, "YYYY-MM-DD", "HH:MM:SS" ) + "'"
   CASE HB_ISDATE( xVal )    ; RETURN "'" + IsoDate( xVal ) + "'"
   CASE HB_ISSTRING( xVal )  ; RETURN "'" + StrTran( xVal, "'", "''" ) + "'"
   ENDCASE
   OrmRaise( 1014, "createTable", "default de tipo nao suportado" )
   RETURN ""

STATIC FUNCTION IsoDate( dVal )
   LOCAL cS := DToS( dVal )                 // "YYYYMMDD"
   RETURN Left( cS, 4 ) + "-" + SubStr( cS, 5, 2 ) + "-" + SubStr( cS, 7, 2 )
