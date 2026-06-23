/* migration.prg -- base abstrata de uma migration. Subclasse define cVersion/cName
   e sobrescreve Up/Down. Down ausente (base) levanta "irreversivel". */
#include "hborm.ch"
#include "error.ch"

CREATE CLASS TORMMigration
   DATA cVersion INIT ""
   DATA cName    INIT ""
   METHOD New() CONSTRUCTOR
   METHOD Up( oSchema ) VIRTUAL
   METHOD Down( oSchema )
END CLASS

METHOD New() CLASS TORMMigration
   RETURN Self

METHOD Down( oSchema ) CLASS TORMMigration
   LOCAL oErr := ErrorNew()
   HB_SYMBOL_UNUSED( oSchema )
   oErr:Subsystem( "hb_orm" )
   oErr:SubCode( 1016 )
   oErr:Severity( ES_ERROR )
   oErr:Description( "migration irreversivel: " + ::cName )
   oErr:Operation( "TORMMigration:Down" )
   oErr:CanRetry( .F. )
   Eval( ErrorBlock(), oErr )
   RETURN NIL
