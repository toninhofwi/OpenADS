/*
 * 03_transactions.prg
 * ------------------------------------------------------------------
 * COOKBOOK / console track -- pure Harbour against OpenADS, NO ORM.
 *
 * Level: INTERMEDIATE.
 *
 * What it shows:
 *   - Wrapping changes in a transaction (AdsBeginTransaction).
 *   - COMMIT making changes durable.
 *   - ROLLBACK undoing every change since BEGIN, including appends.
 *
 * A transaction groups several writes so they either ALL take effect
 * (commit) or NONE do (rollback). This is how you keep related rows
 * consistent -- e.g. never leave an order without its order lines.
 *
 * Note on a rolled-back APPEND: the engine marks the appended record
 * as deleted (the classic xBase convention) rather than physically
 * shrinking the file, so LastRec() is unchanged but the row is hidden
 * while SET DELETED is ON. This example shows that.
 *
 * Data: an invented "accounts" table (id, owner, balance). We move
 * money between two accounts and show what commit vs rollback do.
 *
 * Build & run: see this folder's README.md / build.cmd.
 * ------------------------------------------------------------------
 */

#include "ads.ch"
#include "rddsys.ch"

REQUEST ADS, ADSCDX, ADSNTX

PROCEDURE Main()

   LOCAL cDir := hb_DirTemp() + "oa_cookbook_03"
   LOCAL cDbf := cDir + hb_ps() + "accounts.dbf"

   ? "OpenADS cookbook -- 03 transactions (pure Harbour, local)"
   ?

   AdsSetServerType( ADS_LOCAL_SERVER )
   AdsSetFileType( ADS_CDX )
   RddSetDefault( "ADSCDX" )
   hb_DirCreate( cDir )

   IF ! AdsConnect( cDir )
      ? "AdsConnect failed, DosError =", DosError()
      ErrorLevel( 1 )
      QUIT
   ENDIF

   IF File( cDbf )
      FErase( cDbf )
      FErase( hb_FNameExtSet( cDbf, ".cdx" ) )
   ENDIF

   /* ---- set up two invented accounts --------------------------- */
   DbCreate( cDbf, { ;
       { "ID",      "N",  4, 0 }, ;
       { "OWNER",   "C", 20, 0 }, ;
       { "BALANCE", "N", 12, 2 } }, "ADSCDX" )

   USE ( cDbf ) VIA "ADSCDX" NEW EXCLUSIVE
   INDEX ON FIELD->ID TAG BY_ID

   dbAppend() ; FIELD->ID := 1 ; FIELD->OWNER := "Account A" ; FIELD->BALANCE := 100.00
   dbAppend() ; FIELD->ID := 2 ; FIELD->OWNER := "Account B" ; FIELD->BALANCE :=  20.00
   dbCommit()

   ShowBalances( "Start" )

   /* ============================================================= */
   /* 1. COMMITTED transfer: move 30 from A to B, then COMMIT.      */
   /* ============================================================= */
   ? "-- transfer 30 from A to B, then COMMIT --"
   AdsBeginTransaction()

   Adjust( 1, -30.00 )            // debit A
   Adjust( 2, +30.00 )            // credit B

   AdsCommitTransaction()
   ShowBalances( "After commit" )       // A=70, B=50

   /* ============================================================= */
   /* 2. ROLLED-BACK transfer: start moving 1000, then ROLLBACK.    */
   /*    Nothing should change -- the engine restores both rows.    */
   /* ============================================================= */
   ? "-- transfer 1000 from A to B, then ROLLBACK --"
   AdsBeginTransaction()

   Adjust( 1, -1000.00 )
   Adjust( 2, +1000.00 )
   ? "   (inside the transaction the rows are already changed)"
   ShowBalances( "   mid-transaction" )

   AdsRollback()
   ShowBalances( "After rollback" )     // back to A=70, B=50

   /* ============================================================= */
   /* 3. APPEND inside a rolled-back transaction also disappears.   */
   /* ============================================================= */
   ? "-- append a new account inside a transaction, then ROLLBACK --"
   SET DELETED ON
   ? "   visible rows before:", VisibleCount()
   AdsBeginTransaction()
   dbAppend() ; FIELD->ID := 3 ; FIELD->OWNER := "Ghost" ; FIELD->BALANCE := 999.00
   AdsRollback()
   /* The rolled-back append is now flagged deleted: LastRec() still
    * counts it physically, but it is hidden from any SET DELETED ON
    * scan, so the visible count is back to what it was. */
   ? "   physical LastRec() after rollback:", LastRec()
   ? "   visible rows after rollback :", VisibleCount(), "(the appended row is hidden)"
   SET DELETED OFF
   ?

   dbCloseArea()
   AdsDisconnect()

   ? "Done."
   RETURN

/* ---- helpers --------------------------------------------------- */

/* Add nDelta to the BALANCE of the account whose ID == nId.
 * We locate the row with a short record-order scan instead of dbSeek()
 * so the transaction demo stands on its own; see 02_index_seek.prg and
 * the cookbook's known-issues note for the state of indexed seek. */
STATIC PROCEDURE Adjust( nId, nDelta )
   dbGoTop()
   DO WHILE ! Eof() .AND. FIELD->ID != nId
      dbSkip()
   ENDDO
   IF ! Eof()
      FIELD->BALANCE := FIELD->BALANCE + nDelta
   ENDIF
   RETURN

/* Count records visible under the current SET DELETED setting. */
STATIC FUNCTION VisibleCount()
   LOCAL n := 0
   dbGoTop()
   DO WHILE ! Eof()
      n++
      dbSkip()
   ENDDO
   RETURN n

STATIC PROCEDURE ShowBalances( cWhen )
   LOCAL nOld := RecNo()
   ? "  " + PadR( cWhen, 18 ) + ": "
   dbGoTop()
   DO WHILE ! Eof()
      ?? AllTrim( FIELD->OWNER ) + "=" + LTrim( Str( FIELD->BALANCE, 12, 2 ) ) + "  "
      dbSkip()
   ENDDO
   ?
   dbGoto( nOld )
   RETURN
