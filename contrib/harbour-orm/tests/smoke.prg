/* smoke.prg -- console gate. Each slice adds a section; all must stay green. */
#include "hborm.ch"

#define DB_URI  "sqlite://orm_smoke.db"

PROCEDURE Main()

   ? "=== hb_orm MVP smoke ==="
   ? "engine:", hbo_Version()
   ?

   Sec_Connect()
   Sec_Connection()
   Sec_Grammar()
   Sec_Query()
   Sec_Model()
   Sec_Schema()
   Sec_Relations()

   IF T_Summary() > 0
      ErrorLevel( 1 )
   ENDIF
   RETURN

STATIC PROCEDURE Sec_Connect()
   LOCAL nConn
   ? "[connect]"
   nConn := hbo_Connect( DB_URI )
   T_Ok( "hbo_Connect returns a handle", nConn != 0 )
   IF nConn != 0
      T_Ok( "hbo_Disconnect succeeds", hbo_Disconnect( nConn ) )
   ENDIF
   RETURN

STATIC PROCEDURE Sec_Connection()
   LOCAL oCn, aRows
   ? "[connection]"
   oCn := TORMConnection():New( DB_URI )
   IF ! T_Ok( "connection opens", oCn:IsOpen() )
      RETURN
   ENDIF
   oCn:Execute( "DROP TABLE clientes" )            // idempotent: ok if absent
   T_Ok( "create table", ;
      oCn:Execute( "CREATE TABLE clientes ( id INTEGER, nome VARCHAR(40), uf CHAR(2) )" ) )
   T_Ok( "insert 1", oCn:Execute( "INSERT INTO clientes VALUES ( 1, 'Ana Souza', 'SP' )" ) )
   T_Ok( "insert 2", oCn:Execute( "INSERT INTO clientes VALUES ( 2, 'Bruno Lima', 'RJ' )" ) )
   aRows := oCn:Query( "SELECT id, nome, uf FROM clientes ORDER BY id" )
   T_Eq( "row count", Len( aRows ), 2 )
   IF Len( aRows ) >= 1
      T_Eq( "row1 nome", AllTrim( hb_HGetDef( aRows[ 1 ], "nome", "" ) ), "Ana Souza" )
      T_Eq( "row1 uf",   AllTrim( hb_HGetDef( aRows[ 1 ], "uf", "" ) ), "SP" )
   ENDIF
   oCn:Close()
   RETURN

STATIC PROCEDURE Sec_Grammar()
   LOCAL oG := TORMGrammar():New(), h
   ? "[grammar]"

   T_Eq( "quote string", oG:Quote( "Ana's" ), "'Ana''s'" )
   T_Eq( "quote number", oG:Quote( 12 ), "12" )
   T_Eq( "quote nil",    oG:Quote( NIL ), "NULL" )
   T_Eq( "quote logical", oG:Quote( .T. ), "1" )

   h := { "type" => "insert", "table" => "clientes", ;
          "values" => Ordered( { "id", 1 }, { "nome", "Ana" } ) }
   T_Eq( "insert", oG:Compile( h ), ;
      "INSERT INTO clientes ( id, nome ) VALUES ( 1, 'Ana' )" )

   h := { "type" => "select", "table" => "clientes", "columns" => NIL, ;
          "wheres" => { { "uf", "=", "SP" } }, ;
          "orders" => { { "id", "ASC" } }, "limit" => 10 }
   T_Eq( "select", oG:Compile( h ), ;
      "SELECT * FROM clientes WHERE uf = 'SP' ORDER BY id ASC LIMIT 10" )

   h := { "type" => "update", "table" => "clientes", ;
          "values" => Ordered( { "uf", "RJ" } ), ;
          "wheres" => { { "id", "=", 1 } } }
   T_Eq( "update", oG:Compile( h ), ;
      "UPDATE clientes SET uf = 'RJ' WHERE id = 1" )

   h := { "type" => "delete", "table" => "clientes", ;
          "wheres" => { { "id", "=", 2 } } }
   T_Eq( "delete", oG:Compile( h ), "DELETE FROM clientes WHERE id = 2" )
   RETURN

/* helper: build an insertion-ordered hash from { key, val } pairs */
STATIC FUNCTION Ordered( ... )
   LOCAL h := hb_Hash(), aPair
   FOR EACH aPair IN hb_AParams()
      h[ aPair[ 1 ] ] := aPair[ 2 ]
   NEXT
   RETURN h

STATIC PROCEDURE Sec_Query()
   LOCAL oCn, oQ, aRows
   ? "[query]"
   oCn := TORMConnection():New( DB_URI )
   oCn:Execute( "DROP TABLE clientes" )
   oCn:Execute( "CREATE TABLE clientes ( id INTEGER, nome VARCHAR(40), uf CHAR(2) )" )
   oCn:Execute( "INSERT INTO clientes VALUES ( 1, 'Ana Souza', 'SP' )" )
   oCn:Execute( "INSERT INTO clientes VALUES ( 2, 'Bruno Lima', 'RJ' )" )
   oCn:Execute( "INSERT INTO clientes VALUES ( 3, 'Carla Reis', 'SP' )" )

   oQ := TORMQuery():New( oCn, "clientes" )
   oQ:Where( "uf", "SP" ):OrderBy( "id", "DESC" ):Limit( 5 )
   T_Eq( "builder sql", oQ:ToSql(), ;
      "SELECT * FROM clientes WHERE uf = 'SP' ORDER BY id DESC LIMIT 5" )

   aRows := oQ:Get()
   T_Eq( "builder rows", Len( aRows ), 2 )
   IF Len( aRows ) >= 1
      T_Eq( "builder first id (DESC)", AllTrim( hb_HGetDef( aRows[ 1 ], "id", "" ) ), "3" )
   ENDIF
   oCn:Close()
   RETURN

STATIC PROCEDURE Sec_Model()
   LOCAL oCn, oM, oFound
   ? "[model]"
   oCn := TORMConnection():New( DB_URI )
   oCn:Execute( "DROP TABLE clientes" )
   oCn:Execute( "CREATE TABLE clientes ( id INTEGER, nome VARCHAR(40), uf CHAR(2) )" )
   TORMConnection_Default( oCn )

   // create
   oM := TORMCliente():New()
   oM:Create( { "id" => 1, "nome" => "Ana Souza", "uf" => "SP" } )

   // find
   oFound := TORMCliente():New():Find( 1 )
   T_Ok( "find returns model", oFound != NIL )
   IF oFound != NIL
      T_Eq( "find nome", AllTrim( oFound:Get( "nome" ) ), "Ana Souza" )
      // update via save
      oFound:Set( "uf", "RJ" )
      T_Ok( "save (update)", oFound:Save() )
      T_Eq( "reload uf", AllTrim( TORMCliente():New():Find( 1 ):Get( "uf" ) ), "RJ" )
      // delete
      T_Ok( "delete", oFound:Delete() )
      T_Ok( "find after delete is NIL", TORMCliente():New():Find( 1 ) == NIL )
   ENDIF
   oCn:Close()
   RETURN

STATIC PROCEDURE Sec_Schema()
   LOCAL oCn, oS, cDdl, aRows
   ? "[schema]"
   oCn := TORMConnection():New( DB_URI )
   oS  := TORMSchema():New( oCn )

   /* DDL rendering from a blueprint */
   cDdl := oS:CompileCreate( "produtos", ;
      TORMBlueprint():New():Id():String( "descr", 30 ):Decimal( "preco", 10, 2 ) )
   T_Eq( "schema DDL", cDdl, ;
      "CREATE TABLE produtos ( id INTEGER, descr VARCHAR(30), preco NUMERIC(10,2) )" )

   /* live create + round-trip */
   oS:Drop( "produtos" )
   T_Ok( "schema create", oS:Create( "produtos", ;
      {| t | t:Id(), t:String( "descr", 30 ), t:Decimal( "preco", 10, 2 ) } ) )
   T_Ok( "schema insert", ;
      oCn:Execute( "INSERT INTO produtos ( id, descr, preco ) VALUES ( 10, 'Cabo USB-C', 29.90 )" ) )
   aRows := oCn:Query( "SELECT id, descr FROM produtos ORDER BY id" )
   T_Eq( "schema rows", Len( aRows ), 1 )
   IF Len( aRows ) >= 1
      T_Eq( "schema descr", AllTrim( hb_HGetDef( aRows[ 1 ], "descr", "" ) ), "Cabo USB-C" )
   ENDIF
   oS:Drop( "produtos" )
   oCn:Close()
   RETURN

STATIC PROCEDURE Sec_Relations()
   LOCAL oCn, oS, oOrder, aItems, oItem, oParent
   ? "[relations]"
   oCn := TORMConnection():New( DB_URI )
   TORMConnection_Default( oCn )
   oS := TORMSchema():New( oCn )
   oS:Drop( "pedidos" ) ; oS:Drop( "itens" )
   oS:Create( "pedidos", {| t | t:Id(), t:String( "cliente", 40 ) } )
   oS:Create( "itens", {| t | t:Id(), t:Integer( "pedido_id" ), t:String( "descr", 30 ) } )

   TORMPedido():New():Create( { "id" => 1, "cliente" => "Ana" } )
   TORMPedido():New():Create( { "id" => 2, "cliente" => "Bruno" } )
   TORMItem():New():Create( { "id" => 10, "pedido_id" => 1, "descr" => "Cabo" } )
   TORMItem():New():Create( { "id" => 11, "pedido_id" => 1, "descr" => "Fonte" } )
   TORMItem():New():Create( { "id" => 12, "pedido_id" => 2, "descr" => "Mouse" } )

   /* hasMany: pedido 1 -> 2 itens */
   oOrder := TORMPedido():New():Find( 1 )
   aItems := oOrder:HasMany( {| o | TORMItem():New( o ) }, "pedido_id" )
   T_Eq( "hasMany count", Len( aItems ), 2 )

   /* belongsTo: item 12 -> pedido 2 (Bruno) */
   oItem := TORMItem():New():Find( 12 )
   oParent := oItem:BelongsTo( {| o | TORMPedido():New( o ) }, "pedido_id" )
   T_Ok( "belongsTo !NIL", oParent != NIL )
   IF oParent != NIL
      T_Eq( "belongsTo cliente", AllTrim( oParent:Get( "cliente" ) ), "Bruno" )
   ENDIF

   oS:Drop( "pedidos" ) ; oS:Drop( "itens" )
   oCn:Close()
   RETURN

/* concrete models for the smoke */
CREATE CLASS TORMCliente FROM TORMModel
   METHOD TableName()  INLINE "clientes"
END CLASS

CREATE CLASS TORMPedido FROM TORMModel
   METHOD TableName()  INLINE "pedidos"
END CLASS

CREATE CLASS TORMItem FROM TORMModel
   METHOD TableName()  INLINE "itens"
END CLASS
