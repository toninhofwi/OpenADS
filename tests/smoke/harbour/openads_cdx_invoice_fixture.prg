/*
 * openads_cdx_invoice_fixture.prg
 *
 * Build a set of related DBF/CDX tables for OpenADS smoke tests:
 *
 *   F:\OpenADS\testdata\invoices\
 *     customer.dbf / .cdx   - 100 customers
 *     items.dbf    / .cdx   - 20 catalogue items
 *     invoices.dbf / .cdx   - 1000 invoices linked to customers
 *     invoicedetail.dbf/.cdx- 3+ detail lines per invoice
 *
 * The directory is created if it does not exist.  Tables are always
 * rebuilt from scratch so the fixture is in a known state.
 *
 * Usage:
 *   openads_cdx_invoice_fixture.exe          -- unattended / CI
 *   openads_cdx_invoice_fixture.exe BROWSE   -- interactive: browse each table
 */

#include "ads.ch"

#require "rddads"
REQUEST ADS
REQUEST ADSCDX

#define DATA_DIR           "F:\OpenADS\testdata\invoices"
#define CUSTOMER_COUNT     100
#define INVOICE_COUNT      1000
#define ITEM_COUNT         20
#define MIN_DETAIL_PER_INV 3

//----------------------------------------------------------------------------

PROCEDURE Main()
   LOCAL cDataDir := DATA_DIR
   LOCAL lBrowse  := ( PCount() > 0 )

   ErrorBlock( { |oErr| MyHandler( oErr ) } )

   SET FILETYPE TO CDX
   RDDSETDEFAULT( "ADSCDX" )

   ? "OpenADS DBF/CDX invoice fixture generator"
   ? "Data directory:", cDataDir
   ? "Browse mode   :", IIF( lBrowse, "ON (interactive)", "OFF (unattended)" )
   ?

   IF ! hb_DirExists( cDataDir )
      hb_DirCreate( cDataDir )
      IF ! hb_DirExists( cDataDir )
         ? "ERROR: Could not create data directory:", cDataDir
         RETURN
      ENDIF
   ENDIF

   IF ! BuildTables( cDataDir, lBrowse )
      RETURN
   ENDIF

   ?
   ? "Fixture creation complete."
   ? "  Customers:", CUSTOMER_COUNT
   ? "  Items    :", ITEM_COUNT
   ? "  Invoices :", INVOICE_COUNT
   ? "  Details  :", INVOICE_COUNT * MIN_DETAIL_PER_INV
   RETURN

//----------------------------------------------------------------------------

STATIC FUNCTION BuildTables( cDataDir, lBrowse )
   LOCAL aCustomers := GenerateCustomerRows()
   LOCAL aItems     := GenerateItems()
   LOCAL aInvoices  := GenerateInvoices( aCustomers )
   LOCAL aDetails   := GenerateInvoiceDetails( aInvoices, aItems )

   IF ! CreateCustomerTable( cDataDir, aCustomers, lBrowse ) ; RETURN .F. ; ENDIF
   IF ! CreateItemTable( cDataDir, aItems, lBrowse )         ; RETURN .F. ; ENDIF
   IF ! CreateInvoiceTable( cDataDir, aInvoices, lBrowse )   ; RETURN .F. ; ENDIF
   IF ! CreateInvoiceDetailTable( cDataDir, aDetails, lBrowse ) ; RETURN .F. ; ENDIF

   RETURN .T.

//----------------------------------------------------------------------------

STATIC FUNCTION CreateCustomerTable( cDataDir, aCustomers, lBrowse )
   LOCAL cFile := cDataDir + "\customer.dbf"
   LOCAL i

   DeleteExistingFixtureFiles( cFile )

   DbCreate( cFile, ;
      { { "CUSTNO",  "C",  8, 0 }, ;
        { "NAME",    "C", 50, 0 }, ;
        { "COUNTRY", "C", 24, 0 }, ;
        { "CREATED", "D",  8, 0 }, ;
        { "BALANCE", "N", 12, 2 } } )

   USE ( cFile ) ALIAS CUSTOMER SHARED NEW
   IF Select( "CUSTOMER" ) == 0
      ? "ERROR: Failed to open", cFile
      RETURN .F.
   ENDIF

   INDEX ON CUSTOMER->CUSTNO TAG CUSTNO
   INDEX ON CUSTOMER->NAME   TAG CUSTNAME

   FOR i := 1 TO Len( aCustomers )
      CUSTOMER->( DbAppend() )
      CUSTOMER->CUSTNO  := aCustomers[i][1]
      CUSTOMER->NAME    := aCustomers[i][2]
      CUSTOMER->COUNTRY := aCustomers[i][3]
      CUSTOMER->CREATED := aCustomers[i][4]
      CUSTOMER->BALANCE := aCustomers[i][5]
   NEXT

   CUSTOMER->( DbCommit() )
   CUSTOMER->( DbGoTop() )

   IF lBrowse
      SELECT CUSTOMER
      BROWSE()
   ENDIF

   USE
   RETURN .T.

//----------------------------------------------------------------------------

STATIC FUNCTION CreateItemTable( cDataDir, aItems, lBrowse )
   LOCAL cFile := cDataDir + "\items.dbf"
   LOCAL j

   DeleteExistingFixtureFiles( cFile )

   DbCreate( cFile, ;
      { { "ITEMNO", "C",  8, 0 }, ;
        { "DESCR",  "C", 60, 0 }, ;
        { "PRICE",  "N", 10, 2 } } )

   USE ( cFile ) ALIAS ITEMS SHARED NEW
   IF Select( "ITEMS" ) == 0
      ? "ERROR: Failed to open", cFile
      RETURN .F.
   ENDIF

   INDEX ON ITEMS->ITEMNO TAG ITEMNO

   FOR j := 1 TO Len( aItems )
      ITEMS->( DbAppend() )
      ITEMS->ITEMNO := aItems[j][1]
      ITEMS->DESCR  := aItems[j][2]
      ITEMS->PRICE  := aItems[j][3]
   NEXT

   ITEMS->( DbCommit() )
   ITEMS->( DbGoTop() )

   IF lBrowse
      SELECT ITEMS
      BROWSE()
   ENDIF

   USE
   RETURN .T.

//----------------------------------------------------------------------------

STATIC FUNCTION CreateInvoiceTable( cDataDir, aInvoices, lBrowse )
   LOCAL cFile := cDataDir + "\invoices.dbf"
   LOCAL k

   DeleteExistingFixtureFiles( cFile )

   DbCreate( cFile, ;
      { { "INVNO",   "C", 12, 0 }, ;
        { "CUSTNO",  "C",  8, 0 }, ;
        { "INVDATE", "D",  8, 0 }, ;
        { "TOTAL",   "N", 12, 2 }, ;
        { "STATUS",  "C",  1, 0 } } )

   USE ( cFile ) ALIAS INVOICES SHARED NEW
   IF Select( "INVOICES" ) == 0
      ? "ERROR: Failed to open", cFile
      RETURN .F.
   ENDIF

   INDEX ON INVOICES->INVNO  TAG INVNO
   INDEX ON INVOICES->CUSTNO TAG CUSTIDX

   FOR k := 1 TO Len( aInvoices )
      INVOICES->( DbAppend() )
      INVOICES->INVNO   := aInvoices[k][1]
      INVOICES->CUSTNO  := aInvoices[k][2]
      INVOICES->INVDATE := aInvoices[k][3]
      INVOICES->TOTAL   := aInvoices[k][4]
      INVOICES->STATUS  := aInvoices[k][5]
   NEXT

   INVOICES->( DbCommit() )
   INVOICES->( DbGoTop() )

   IF lBrowse
      SELECT INVOICES
      BROWSE()
   ENDIF

   USE
   RETURN .T.

//----------------------------------------------------------------------------

STATIC FUNCTION CreateInvoiceDetailTable( cDataDir, aDetails, lBrowse )
   LOCAL cFile := cDataDir + "\invoicedetail.dbf"
   LOCAL n

   DeleteExistingFixtureFiles( cFile )

   DbCreate( cFile, ;
      { { "INVNO",  "C", 12, 0 }, ;
        { "LINENO", "N",  4, 0 }, ;
        { "ITEMNO", "C",  8, 0 }, ;
        { "QTY",    "N",  5, 0 }, ;
        { "PRICE",  "N", 10, 2 }, ;
        { "AMOUNT", "N", 12, 2 } } )

   USE ( cFile ) ALIAS DETAIL SHARED NEW
   IF Select( "DETAIL" ) == 0
      ? "ERROR: Failed to open", cFile
      RETURN .F.
   ENDIF

   INDEX ON DETAIL->INVNO  TAG INVNO
   INDEX ON DETAIL->ITEMNO TAG ITEMIDX

   FOR n := 1 TO Len( aDetails )
      DETAIL->( DbAppend() )
      DETAIL->INVNO  := aDetails[n][1]
      DETAIL->LINENO := aDetails[n][2]
      DETAIL->ITEMNO := aDetails[n][3]
      DETAIL->QTY    := aDetails[n][4]
      DETAIL->PRICE  := aDetails[n][5]
      DETAIL->AMOUNT := aDetails[n][6]
   NEXT

   DETAIL->( DbCommit() )
   DETAIL->( DbGoTop() )

   IF lBrowse
      SELECT DETAIL
      BROWSE()
   ENDIF

   USE
   RETURN .T.

//----------------------------------------------------------------------------

STATIC PROCEDURE DeleteExistingFixtureFiles( cDbf )
   LOCAL cCdx := hb_FNameExtSet( cDbf, ".cdx" )
   LOCAL cFpt := hb_FNameExtSet( cDbf, ".fpt" )

   IF File( cDbf ) ; FErase( cDbf ) ; ENDIF
   IF File( cCdx ) ; FErase( cCdx ) ; ENDIF
   IF File( cFpt ) ; FErase( cFpt ) ; ENDIF
   RETURN

//----------------------------------------------------------------------------

STATIC FUNCTION GenerateCustomerRows()
   LOCAL aResult  := {}
   LOCAL aFirst   := { "Alex", "Beth", "Carl", "Dana", "Evan", "Fiona", ;
                       "Gabe", "Hana", "Iris", "Joel", "Kara", "Liam", ;
                       "Mia",  "Nate", "Owen", "Pia",  "Quinn","Rita", ;
                       "Sean", "Tina", "Udo",  "Vera", "Will", "Zara" }
   LOCAL aLast    := { "Bishop",  "Clark",   "Dawson",  "Evans",      "Frost", ;
                       "Garcia",  "Harris",  "Iverson", "Jones",      "Kline", ;
                       "Lee",     "Miller",  "Norton",  "Owens",      "Parker", ;
                       "Quincy",  "Reed",    "Stone",   "Turner",     "Underwood", ;
                       "Vega",    "Walker",  "Young",   "Zimmer" }
   LOCAL aCountry := { "USA", "Canada", "UK", "Germany", "France", ;
                       "Spain", "Italy", "Australia", "Brazil", "Sweden" }
   LOCAL i, cCustNo, cName, nDays, dCreated, nBalance

   hb_randomSeed( Int( Seconds() ) )

   FOR i := 1 TO CUSTOMER_COUNT
      cName    := aFirst[ 1 + Int( Len( aFirst )   * hb_random() ) ] + " " + ;
                  aLast[  1 + Int( Len( aLast )    * hb_random() ) ]
      cCustNo  := "C" + StrZero( i, 5 )
      nDays    := Int( 365 * 5 * hb_random() )
      dCreated := Date() - nDays
      nBalance := Round( 50 + 10000 * hb_random(), 2 )
      AAdd( aResult, { cCustNo, cName, ;
                       aCountry[ 1 + Int( Len( aCountry ) * hb_random() ) ], ;
                       dCreated, nBalance } )
   NEXT

   RETURN aResult

//----------------------------------------------------------------------------

STATIC FUNCTION GenerateItems()
   LOCAL aResult := {}
   LOCAL aWords  := { "Widget",    "Gadget",  "Service", "Kit",    "Module", ;
                      "Component", "Unit",    "Package", "Bundle", "Accessory" }
   LOCAL i, cItemNo, cDescr, nPrice

   hb_randomSeed( Int( Seconds() ) + 1234 )

   FOR i := 1 TO ITEM_COUNT
      cItemNo := "I" + StrZero( i, 4 )
      cDescr  := aWords[ 1 + Int( Len( aWords ) * hb_random() ) ] + " " + ;
                 aWords[ 1 + Int( Len( aWords ) * hb_random() ) ]
      nPrice  := Round( 5 + 195 * hb_random(), 2 )
      AAdd( aResult, { cItemNo, cDescr, nPrice } )
   NEXT

   RETURN aResult

//----------------------------------------------------------------------------

STATIC FUNCTION GenerateInvoices( aCustomers )
   LOCAL aResult  := {}
   LOCAL aStatus  := { "O", "P", "C" }
   LOCAL i, cInvNo, cCustNo, dDate, nStatusIdx

   hb_randomSeed( Int( Seconds() ) + 2345 )

   FOR i := 1 TO INVOICE_COUNT
      cInvNo     := "INV" + StrZero( i, 7 )
      cCustNo    := aCustomers[ 1 + Int( Len( aCustomers ) * hb_random() ) ][1]
      dDate      := Date() - Int( 365 * hb_random() )
      nStatusIdx := 1 + Int( Len( aStatus ) * hb_random() )
      AAdd( aResult, { cInvNo, cCustNo, dDate, 0.0, aStatus[nStatusIdx] } )
   NEXT

   RETURN aResult

//----------------------------------------------------------------------------

/* Builds detail rows; accumulates per-invoice totals into aInvoices[][4]. */
STATIC FUNCTION GenerateInvoiceDetails( aInvoices, aItems )
   LOCAL aResult := {}
   LOCAL i, nLine, cInvNo, cItemNo, nQty, nPrice, nAmount, nInvTotal

   hb_randomSeed( Int( Seconds() ) + 3456 )

   FOR i := 1 TO Len( aInvoices )
      cInvNo    := aInvoices[i][1]
      nInvTotal := 0.0
      FOR nLine := 1 TO MIN_DETAIL_PER_INV
         cItemNo  := aItems[ 1 + Int( Len( aItems ) * hb_random() ) ][1]
         nQty     := 1 + Int( 10 * hb_random() )
         nPrice   := aItems[ 1 + Int( Len( aItems ) * hb_random() ) ][3]
         nAmount  := Round( nQty * nPrice, 2 )
         nInvTotal += nAmount
         AAdd( aResult, { cInvNo, nLine, cItemNo, nQty, nPrice, nAmount } )
      NEXT
      aInvoices[i][4] := Round( nInvTotal, 2 )
   NEXT

   RETURN aResult

//----------------------------------------------------------------------------

STATIC PROCEDURE MyHandler( oErr )
   ? "ERROR:", oErr:Description
   ? "       gencode=",  oErr:GenCode, " subcode=", oErr:SubCode
   ? "       operation=", oErr:Operation
   ErrorLevel( 1 )
   QUIT
   RETURN
