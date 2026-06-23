/* connection.prg -- one connection on the ABI engine, by URI. */
#include "hborm.ch"

CREATE CLASS TORMConnection
   DATA nConn   INIT 0
   DATA cUri    INIT ""
   DATA lNav    INIT .F.
   METHOD New( cUri ) CONSTRUCTOR
   METHOD IsOpen()         INLINE ::nConn != 0
   METHOD IsNavigational() INLINE ::lNav
   METHOD Execute( cSql )
   METHOD Query( cSql )
   METHOD NavInsert( cTable, hValues )
   METHOD NavFind( cTable, cPk, xId )
   METHOD NavUpdate( cTable, cPk, xId, hValues )
   METHOD NavDelete( cTable, cPk, xId )
   METHOD Close()
   METHOD LastError() INLINE hbo_LastErr()
END CLASS

METHOD New( cUri ) CLASS TORMConnection
   ::cUri  := cUri
   /* Backend capability: sqlite:// is SQL-only; every other backend (native
      DBF/ADT, the tcp:// server, and the navigational-only pg/maria/odbc)
      exposes the table-cursor ABI, so the model takes the navigational path
      there -- it works on the navigational-only backends and sidesteps the
      SQL-over-xBase WHERE/LIMIT caveat (findings F3) on DBF/ADS. */
   ::lNav  := ! ( Lower( Left( AllTrim( cUri ), 9 ) ) == "sqlite://" )
   ::nConn := hbo_Connect( cUri )
   IF ::nConn != 0
      /* ORM policy: deleted rows are gone. On xBase backends (DBF/ADT) DELETE
         only sets the deletion flag and rows stay visible until PACK; hide them
         so DELETE behaves consistently with the SQL backends. */
      hbo_ShowDeleted( .F. )
   ENDIF
   RETURN Self

METHOD Execute( cSql ) CLASS TORMConnection
   LOCAL nStmt, nCur := 0, lOk
   IF ::nConn == 0
      RETURN .F.
   ENDIF
   nStmt := hbo_StmtNew( ::nConn )
   IF nStmt == 0
      RETURN .F.
   ENDIF
   lOk := hbo_ExecDirect( nStmt, cSql, @nCur )
   IF nCur != 0
      hbo_TableClose( nCur )            // DDL/DML should not yield a cursor; guard anyway
   ENDIF
   hbo_StmtClose( nStmt )
   RETURN lOk

METHOD Query( cSql ) CLASS TORMConnection
   LOCAL nStmt, nCur := 0, aRows := {}, aNames := {}, i, nFields, hRow
   IF ::nConn == 0
      RETURN aRows
   ENDIF
   nStmt := hbo_StmtNew( ::nConn )
   IF nStmt == 0
      RETURN aRows
   ENDIF
   IF ! hbo_ExecDirect( nStmt, cSql, @nCur ) .OR. nCur == 0
      hbo_StmtClose( nStmt )
      RETURN aRows
   ENDIF
   nFields := hbo_NumFields( nCur )
   FOR i := 1 TO nFields
      AAdd( aNames, hbo_FieldName( nCur, i ) )
   NEXT
   hbo_GoTop( nCur )
   DO WHILE ! hbo_Eof( nCur )
      hRow := hb_Hash()
      FOR i := 1 TO nFields
         hRow[ aNames[ i ] ] := hbo_Field( nCur, aNames[ i ] )
      NEXT
      AAdd( aRows, hRow )
      hbo_Skip( nCur, 1 )
   ENDDO
   hbo_TableClose( nCur )
   hbo_StmtClose( nStmt )
   RETURN aRows

/* ---- navigational CRUD (table cursor, not SQL) -------------------------- */

METHOD NavInsert( cTable, hValues ) CLASS TORMConnection
   LOCAL nTbl, lOk := .F.
   IF ::nConn == 0
      RETURN .F.
   ENDIF
   nTbl := hbo_OpenTable( ::nConn, cTable )
   IF nTbl == 0
      RETURN .F.
   ENDIF
   IF hbo_Append( nTbl )
      lOk := NavSetFields( nTbl, hValues )
      hbo_WriteRec( nTbl )
   ENDIF
   hbo_TableClose( nTbl )
   RETURN lOk

METHOD NavFind( cTable, cPk, xId ) CLASS TORMConnection
   LOCAL nTbl, hRow := NIL, cTarget
   IF ::nConn == 0
      RETURN NIL
   ENDIF
   nTbl := hbo_OpenTable( ::nConn, cTable )
   IF nTbl == 0
      RETURN NIL
   ENDIF
   cTarget := AllTrim( ValToKey( xId ) )
   hbo_GoTop( nTbl )
   /* Honor deletion per-record: the SQL passthrough can leave the engine's
      global SET DELETED flag flipped, so we never trust it for a navigational
      Find -- a deleted row must not match. */
   DO WHILE ! hbo_Eof( nTbl )
      IF ! hbo_IsDeleted( nTbl ) .AND. AllTrim( hbo_Field( nTbl, cPk ) ) == cTarget
         hRow := NavReadRow( nTbl )
         EXIT
      ENDIF
      hbo_Skip( nTbl, 1 )
   ENDDO
   hbo_TableClose( nTbl )
   RETURN hRow

METHOD NavUpdate( cTable, cPk, xId, hValues ) CLASS TORMConnection
   LOCAL nTbl, lOk := .F., cTarget
   IF ::nConn == 0
      RETURN .F.
   ENDIF
   nTbl := hbo_OpenTable( ::nConn, cTable )
   IF nTbl == 0
      RETURN .F.
   ENDIF
   cTarget := AllTrim( ValToKey( xId ) )
   hbo_GoTop( nTbl )
   DO WHILE ! hbo_Eof( nTbl )
      IF ! hbo_IsDeleted( nTbl ) .AND. AllTrim( hbo_Field( nTbl, cPk ) ) == cTarget
         lOk := NavSetFields( nTbl, hValues )
         hbo_WriteRec( nTbl )
         EXIT
      ENDIF
      hbo_Skip( nTbl, 1 )
   ENDDO
   hbo_TableClose( nTbl )
   RETURN lOk

METHOD NavDelete( cTable, cPk, xId ) CLASS TORMConnection
   LOCAL nTbl, lOk := .F., cTarget
   IF ::nConn == 0
      RETURN .F.
   ENDIF
   nTbl := hbo_OpenTable( ::nConn, cTable )
   IF nTbl == 0
      RETURN .F.
   ENDIF
   cTarget := AllTrim( ValToKey( xId ) )
   hbo_GoTop( nTbl )
   DO WHILE ! hbo_Eof( nTbl )
      IF ! hbo_IsDeleted( nTbl ) .AND. AllTrim( hbo_Field( nTbl, cPk ) ) == cTarget
         lOk := hbo_DeleteRec( nTbl )
         EXIT
      ENDIF
      hbo_Skip( nTbl, 1 )
   ENDDO
   hbo_TableClose( nTbl )
   RETURN lOk

METHOD Close() CLASS TORMConnection
   IF ::nConn != 0
      hbo_Disconnect( ::nConn )
      ::nConn := 0
   ENDIF
   RETURN NIL

/* file-local helpers for the navigational path */
STATIC FUNCTION NavSetFields( nTbl, hValues )
   LOCAL cK, xV, lOk := .T.
   FOR EACH cK IN hb_HKeys( hValues )
      xV := hValues[ cK ]
      DO CASE
      CASE HB_ISNUMERIC( xV ) ; lOk := hbo_SetNum( nTbl, cK, xV ) .AND. lOk
      CASE HB_ISLOGICAL( xV ) ; lOk := hbo_SetLog( nTbl, cK, xV ) .AND. lOk
      CASE HB_ISDATE( xV )    ; lOk := hbo_SetStr( nTbl, cK, DToS( xV ) ) .AND. lOk
      OTHERWISE               ; lOk := hbo_SetStr( nTbl, cK, hb_CStr( xV ) ) .AND. lOk
      ENDCASE
   NEXT
   RETURN lOk

STATIC FUNCTION NavReadRow( nTbl )
   LOCAL hRow := hb_Hash(), n := hbo_NumFields( nTbl ), i, cName
   FOR i := 1 TO n
      cName := hbo_FieldName( nTbl, i )
      hRow[ cName ] := hbo_Field( nTbl, cName )
   NEXT
   RETURN hRow

/* primary-key value -> the trimmed string the scan compares against */
STATIC FUNCTION ValToKey( xId )
   DO CASE
   CASE HB_ISNUMERIC( xId ) ; RETURN LTrim( Str( xId ) )
   CASE HB_ISDATE( xId )    ; RETURN DToS( xId )
   ENDCASE
   RETURN hb_CStr( xId )

/* Thread-static default connection: set with an argument, read without. */
FUNCTION TORMConnection_Default( oConn )
   THREAD STATIC t_oDefault
   IF PCount() > 0
      t_oDefault := oConn
   ENDIF
   RETURN t_oDefault
