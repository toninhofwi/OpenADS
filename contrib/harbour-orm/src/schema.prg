/* schema.prg -- schema builder / migrations. A blueprint collects columns in a
 * dialect-agnostic form; the schema renders portable DDL and runs it. */
#include "hborm.ch"

CREATE CLASS TORMBlueprint
   DATA aCols  INIT {}                          // { { name, type, len, dec, flags } ... }
   METHOD New() CONSTRUCTOR
   METHOD Id( cName )                           // integer primary key
   METHOD Integer( cName )
   METHOD String( cName, nLen )
   METHOD Text( cName )
   METHOD Decimal( cName, nLen, nDec )
   METHOD Boolean( cName )
   METHOD Date( cName )
   METHOD DateTime( cName )
   METHOD Col( cName, cType, nLen, nDec )       // add raw column descriptor
END CLASS

METHOD New() CLASS TORMBlueprint
   RETURN Self

METHOD Col( cName, cType, nLen, nDec ) CLASS TORMBlueprint
   AAdd( ::aCols, { cName, cType, nLen, nDec } )
   RETURN Self

METHOD Id( cName )       CLASS TORMBlueprint
   RETURN ::Col( iif( cName == NIL, "id", cName ), "id", NIL, NIL )
METHOD Integer( cName )  CLASS TORMBlueprint
   RETURN ::Col( cName, "integer", NIL, NIL )
METHOD String( cName, nLen ) CLASS TORMBlueprint
   RETURN ::Col( cName, "string", iif( nLen == NIL, 255, nLen ), NIL )
METHOD Text( cName )     CLASS TORMBlueprint
   RETURN ::Col( cName, "text", NIL, NIL )
METHOD Decimal( cName, nLen, nDec ) CLASS TORMBlueprint
   RETURN ::Col( cName, "decimal", iif( nLen == NIL, 18, nLen ), iif( nDec == NIL, 2, nDec ) )
METHOD Boolean( cName )  CLASS TORMBlueprint
   RETURN ::Col( cName, "boolean", NIL, NIL )
METHOD Date( cName )     CLASS TORMBlueprint
   RETURN ::Col( cName, "date", NIL, NIL )
METHOD DateTime( cName ) CLASS TORMBlueprint
   RETURN ::Col( cName, "datetime", NIL, NIL )


CREATE CLASS TORMSchema
   DATA oConn
   METHOD New( oConn ) CONSTRUCTOR
   METHOD Create( cTable, bDef )
   METHOD Drop( cTable )
   METHOD CompileCreate( cTable, oBp )          // -> DDL string
   METHOD MapType( aCol )                       // descriptor -> SQL type
END CLASS

METHOD New( oConn ) CLASS TORMSchema
   ::oConn := iif( oConn == NIL, TORMConnection_Default(), oConn )
   RETURN Self

METHOD MapType( aCol ) CLASS TORMSchema
   LOCAL cType := aCol[ 2 ], nLen := aCol[ 3 ], nDec := aCol[ 4 ]
   DO CASE
   CASE cType == "id"       ; RETURN "INTEGER"
   CASE cType == "integer"  ; RETURN "INTEGER"
   CASE cType == "string"   ; RETURN "VARCHAR(" + LTrim( Str( nLen ) ) + ")"
   CASE cType == "text"     ; RETURN "MEMO"
   CASE cType == "decimal"  ; RETURN "NUMERIC(" + LTrim( Str( nLen ) ) + "," + LTrim( Str( nDec ) ) + ")"
   CASE cType == "boolean"  ; RETURN "LOGICAL"
   CASE cType == "date"     ; RETURN "DATE"
   CASE cType == "datetime" ; RETURN "TIMESTAMP"
   ENDCASE
   RETURN "VARCHAR(255)"

METHOD CompileCreate( cTable, oBp ) CLASS TORMSchema
   LOCAL c := "", aCol
   FOR EACH aCol IN oBp:aCols
      c += iif( Empty( c ), "", ", " ) + aCol[ 1 ] + " " + ::MapType( aCol )
   NEXT
   RETURN "CREATE TABLE " + cTable + " ( " + c + " )"

METHOD Create( cTable, bDef ) CLASS TORMSchema
   LOCAL oBp := TORMBlueprint():New()
   Eval( bDef, oBp )                            // caller fills the blueprint
   RETURN ::oConn:Execute( ::CompileCreate( cTable, oBp ) )

METHOD Drop( cTable ) CLASS TORMSchema
   RETURN ::oConn:Execute( "DROP TABLE " + cTable )
