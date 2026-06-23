/* dynmodel.prg -- Porta A: model dinamico montado por introspecao, sem subclasse
   manual. TableName/PrimaryKey/Casts devolvem o que o catalogo entregou. */
#include "hborm.ch"

CREATE CLASS TORMDynModel FROM TORMModel
   DATA cTbl  INIT ""
   DATA hCast
   DATA cPk   INIT "id"
   METHOD TableName()  INLINE ::cTbl
   METHOD PrimaryKey() INLINE ::cPk
   METHOD Casts()      INLINE iif( ::hCast == NIL, {=>}, ::hCast )
   METHOD SetSchema( cTable, hCasts, cPk )
END CLASS

METHOD SetSchema( cTable, hCasts, cPk ) CLASS TORMDynModel
   ::cTbl  := cTable
   ::hCast := hCasts
   ::cPk   := cPk
   RETURN Self

FUNCTION ORM_Abrir( cTable, oConn )
   LOCAL aCols, oM
   IF oConn == NIL
      oConn := TORMConnection_Default()
   ENDIF
   aCols := ORM_Introspect( oConn, cTable )
   oM    := TORMDynModel():New( oConn )
   oM:SetSchema( cTable, ORM_CastsFromCols( aCols ), ORM_PkFromCols( aCols ) )
   RETURN oM
