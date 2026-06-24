/* migrator.prg -- registry de migrations (estado de processo intencional) +
   runner TORMMigrator. Reset isola testes. */
#include "hborm.ch"
#include "error.ch"

STATIC s_aMig := {}

FUNCTION ORM_Migrations_Register( oMig )
   IF AScan( s_aMig, {| o | o:cVersion == oMig:cVersion } ) > 0
      MigRaise( "versao de migration duplicada: " + oMig:cVersion )
   ENDIF
   AAdd( s_aMig, oMig )
   RETURN NIL

FUNCTION ORM_Migrations_Registry()
   LOCAL a := AClone( s_aMig )
   ASort( a, , , {| x, y | x:cVersion < y:cVersion } )
   RETURN a

FUNCTION ORM_Migrations_Reset()
   s_aMig := {}
   RETURN NIL

STATIC PROCEDURE MigRaise( cWhy )
   LOCAL oErr := ErrorNew()
   oErr:Subsystem( "hb_orm" )
   oErr:SubCode( 1017 )
   oErr:Severity( ES_ERROR )
   oErr:Description( cWhy )                       // sem SQL/path
   oErr:Operation( "ORM_Migrations" )
   oErr:CanRetry( .F. )
   Eval( ErrorBlock(), oErr )
   RETURN

CREATE CLASS TORMMigrator
   DATA oConn
   DATA oSchema
   METHOD New( oConn ) CONSTRUCTOR
   METHOD EnsureTable()
   METHOD Applied()
   METHOD MaxBatch()
   METHOD Pending()
   METHOD Migrate()
   METHOD Rollback()
   METHOD Status()
   METHOD Migrar()   INLINE ::Migrate()
   METHOD Reverter() INLINE ::Rollback()
   METHOD Situacao() INLINE ::Status()
   /* aliases ES */
   METHOD Revertir()  INLINE ::Rollback()
   METHOD Situacion() INLINE ::Status()
END CLASS

METHOD New( oConn ) CLASS TORMMigrator
   ::oConn   := iif( oConn == NIL, TORMConnection_Default(), oConn )
   ::oSchema := TORMSchema():New( ::oConn )
   RETURN Self

METHOD EnsureTable() CLASS TORMMigrator
   IF ! ::oSchema:HasTable( "schema_migrations" )
      ::oSchema:CreateTable( "schema_migrations", {| b | ;
         b:String( "version", 80 ), ;
         b:String( "name", 160 ), ;
         b:Integer( "batch" ), ;
         b:String( "applied_at", 40 ) } )
   ENDIF
   RETURN NIL

METHOD Applied() CLASS TORMMigrator
   LOCAL aRows := ::oConn:Query( "SELECT version FROM schema_migrations", {} )
   LOCAL a := {}, hR
   FOR EACH hR IN aRows
      AAdd( a, AllTrim( hb_CStr( hb_HGetDef( hR, "version", "" ) ) ) )
   NEXT
   RETURN a

METHOD MaxBatch() CLASS TORMMigrator
   LOCAL aRows := ::oConn:Query( "SELECT MAX(batch) AS m FROM schema_migrations", {} )
   IF Len( aRows ) == 0
      RETURN 0
   ENDIF
   RETURN Int( Val( hb_CStr( hb_HGetDef( aRows[ 1 ], "m", "0" ) ) ) )

METHOD Pending() CLASS TORMMigrator
   LOCAL aApp := ::Applied(), aReg := ORM_Migrations_Registry(), a := {}, o
   FOR EACH o IN aReg
      IF AScan( aApp, {| c | c == o:cVersion } ) == 0
         AAdd( a, o )
      ENDIF
   NEXT
   RETURN a

METHOD Migrate() CLASS TORMMigrator
   LOCAL aPend, o, nBatch, nDone := 0
   ::EnsureTable()
   aPend := ::Pending()
   IF Len( aPend ) == 0
      RETURN hb_Hash( "applied", 0, "batch", NIL )
   ENDIF
   nBatch := ::MaxBatch() + 1
   FOR EACH o IN aPend
      /* cada migration aplica ATOMICA: Up + bookkeeping numa transacao. Falha no
         Up desfaz a migration inteira (DDL inclusa) e re-levanta -> Migrate para. */
      ::oConn:Transaction( MigApplyBlock( ::oSchema, o, nBatch ) )
      nDone++
   NEXT
   RETURN hb_Hash( "applied", nDone, "batch", nBatch )

METHOD Rollback() CLASS TORMMigrator
   LOCAL nBatch, aRows, aVers, aReg, aBatch := {}, o, i, nDone := 0, hR
   ::EnsureTable()
   nBatch := ::MaxBatch()
   IF nBatch == 0
      RETURN hb_Hash( "rolledBack", 0, "batch", NIL )
   ENDIF
   aRows := ::oConn:Query( "SELECT version FROM schema_migrations WHERE batch = :p1", ;
      { { "p1", nBatch } } )
   aVers := {}
   FOR EACH hR IN aRows
      AAdd( aVers, AllTrim( hb_CStr( hb_HGetDef( hR, "version", "" ) ) ) )
   NEXT
   aReg := ORM_Migrations_Registry()               // ascendente
   FOR EACH o IN aReg
      IF AScan( aVers, {| c | c == o:cVersion } ) > 0
         AAdd( aBatch, o )
      ENDIF
   NEXT
   /* orphan: versao no batch mas ausente no registry (classe removida/renomeada).
      Sem o codigo nao da p/ rodar o Down -> falha honesta, nao desfaz pela metade. */
   IF Len( aBatch ) != Len( aVers )
      MigRaise( "rollback: migration do ultimo batch ausente no registry" )
   ENDIF
   FOR i := Len( aBatch ) TO 1 STEP -1             // ordem inversa de version
      /* cada reversao ATOMICA: Down + DELETE da linha numa transacao */
      ::oConn:Transaction( MigRevertBlock( ::oSchema, aBatch[ i ] ) )
      nDone++
   NEXT
   RETURN hb_Hash( "rolledBack", nDone, "batch", nBatch )

METHOD Status() CLASS TORMMigrator
   LOCAL aReg, hApp := hb_Hash(), aRows, hR, o, a := {}, cV, lApp
   ::EnsureTable()
   aRows := ::oConn:Query( "SELECT version, batch FROM schema_migrations", {} )
   FOR EACH hR IN aRows
      hApp[ AllTrim( hb_CStr( hb_HGetDef( hR, "version", "" ) ) ) ] := ;
         Int( Val( hb_CStr( hb_HGetDef( hR, "batch", "0" ) ) ) )
   NEXT
   aReg := ORM_Migrations_Registry()
   FOR EACH o IN aReg
      cV   := o:cVersion
      lApp := hb_HHasKey( hApp, cV )
      AAdd( a, hb_Hash( "version", cV, "name", o:cName, ;
            "applied", lApp, "batch", iif( lApp, hApp[ cV ], NIL ) ) )
   NEXT
   RETURN a

STATIC FUNCTION NowIso()
   RETURN hb_TToC( hb_DateTime(), "YYYY-MM-DD", "HH:MM:SS" )

/* bloco aplicado dentro de UMA transacao: Up + INSERT de bookkeeping juntos.
   Se qualquer um levantar, o helper Transaction desfaz a migration inteira. */
STATIC FUNCTION MigApplyBlock( oSchema, oMig, nBatch )
   RETURN {| oConn | ;
      oMig:Up( oSchema ), ;
      iif( oConn:Execute( "INSERT INTO schema_migrations " + ;
         "( version, name, batch, applied_at ) VALUES ( :p1, :p2, :p3, :p4 )", ;
         { { "p1", oMig:cVersion }, { "p2", oMig:cName }, { "p3", nBatch }, { "p4", NowIso() } } ), ;
         NIL, MigRaise( "falha ao registrar migration aplicada: " + oMig:cVersion ) ) }

/* bloco da reversao dentro de UMA transacao: Down + DELETE da linha. */
STATIC FUNCTION MigRevertBlock( oSchema, oMig )
   RETURN {| oConn | ;
      oMig:Down( oSchema ), ;
      iif( oConn:Execute( "DELETE FROM schema_migrations WHERE version = :p1", ;
         { { "p1", oMig:cVersion } } ), ;
         NIL, MigRaise( "falha ao remover registro de migration: " + oMig:cVersion ) ) }
