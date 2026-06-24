/* schema.prg -- fachada de DDL sobre uma Connection. Backend de prova: sqlite://.
   Conexao navegacional levanta (DDL navegacional fica p/ a fatia nav/dialetos). */
#include "hborm.ch"
#include "error.ch"

CREATE CLASS TORMSchema
   DATA oConn
   METHOD New( oConn ) CONSTRUCTOR
   METHOD CreateTable( cNome, bBlk )
   METHOD DropTable( cNome, lIfExists )
   METHOD HasTable( cNome )
   METHOD CriarTabela( cNome, bBlk )        INLINE ::CreateTable( cNome, bBlk )
   METHOD RemoverTabela( cNome, lIfExists ) INLINE ::DropTable( cNome, lIfExists )
   METHOD TemTabela( cNome )                INLINE ::HasTable( cNome )
   /* aliases ES */
   METHOD CrearTabla( cNome, bBlk )         INLINE ::CreateTable( cNome, bBlk )
   METHOD EliminarTabla( cNome, lIfExists ) INLINE ::DropTable( cNome, lIfExists )
   METHOD TieneTabla( cNome )               INLINE ::HasTable( cNome )
END CLASS

METHOD New( oConn ) CLASS TORMSchema
   ::oConn := iif( oConn == NIL, TORMConnection_Default(), oConn )
   RETURN Self

METHOD CreateTable( cNome, bBlk ) CLASS TORMSchema
   LOCAL oBp, hAst, oG, r, hIdx, lOk
   oBp := TORMBlueprint():New( cNome )
   Eval( bBlk, oBp )
   hAst := oBp:ToAst()
   IF ::oConn:IsNavigational()
      RETURN NavCreateTable( ::oConn, hAst )          // ramo navegacional (DBF)
   ENDIF
   oG := TORMGrammar():New()
   r  := oG:Compile( hAst )
   lOk := ::oConn:Execute( r[ "sql" ], r[ "params" ] )
   IF lOk
      FOR EACH hIdx IN hAst[ "indexes" ]
         /* indice declarado que falha = sucesso PARCIAL: sinaliza .F. (nunca
            reporta tabela+indices ok quando um indice nao subiu). */
         IF ! ::oConn:CreateIndex( cNome, hIdx[ "columns" ], hIdx[ "unique" ], hIdx[ "name" ] )
            lOk := .F.
         ENDIF
      NEXT
   ENDIF
   RETURN lOk

METHOD DropTable( cNome, lIfExists ) CLASS TORMSchema
   LOCAL oG := TORMGrammar():New(), r
   GuardNav( ::oConn )
   r := oG:Compile( hb_Hash( "type", "dropTable", "table", cNome, ;
                             "ifExists", ( lIfExists == .T. ) ) )
   RETURN ::oConn:Execute( r[ "sql" ], r[ "params" ] )

METHOD HasTable( cNome ) CLASS TORMSchema
   LOCAL aRows := ::oConn:Query( ;
      "SELECT name FROM sqlite_master WHERE type = :p1 AND name = :p2", ;
      { { "p1", "table" }, { "p2", cNome } } )
   RETURN Len( aRows ) > 0

STATIC PROCEDURE GuardNav( oConn )
   LOCAL oErr
   IF oConn:IsNavigational()
      oErr := ErrorNew()
      oErr:Subsystem( "hb_orm" )
      oErr:SubCode( 1015 )
      oErr:Severity( ES_ERROR )
      oErr:Description( "DDL nao suportado nesta fatia (backend navegacional)" )
      oErr:Operation( "TORMSchema" )
      oErr:CanRetry( .F. )
      Eval( ErrorBlock(), oErr )
   ENDIF
   RETURN
