// idxprobe.prg — isolate the ADSCDX index-order bug.
// Stages a table whose NAME order != record order, builds an
// INDEX ON NAME, then dumps the index walk. Compares ADSCDX
// (OpenADS) against the DBFCDX baseline.
//
// Build: hbmk2 idxprobe.prg -comp=msvc64 -gtstd
// Run:   idxprobe.exe          (DBFCDX baseline)
//        idxprobe.exe /ads     (ADSCDX -> OpenADS)

#require "rddads"

REQUEST DBFCDX
REQUEST ADSCDX

PROCEDURE Main( cMode )

   LOCAL cDir := TempFolder() + "\openads_idxprobe"
   LOCAL cDbf := cDir + "\idx.dbf"
   LOCAL lAds := ( ValType( cMode ) == "C" .AND. "/ads" $ Lower( cMode ) )
   LOCAL aNames := { "Mike", "Anna", "Zoe", "Carl", "Kate", "Bob", ;
                     "Yuri", "Dave", "Emma", "Xena", "Fred", "Gina" }
   LOCAL i, cPrev, lSorted

   StageDbf( cDir, cDbf, aNames )

   IF lAds
      AdsSetFileType( 2 )        // ADS_CDX / FoxPro
      AdsSetServerType( 1 )      // ADS_LOCAL_SERVER
      IF ! AdsConnect( cDir )
         ? "AdsConnect failed for " + cDir
         RETURN
      ENDIF
      RddSetDefault( "ADSCDX" )
      USE ( "idx" ) VIA "ADSCDX" ALIAS T SHARED NEW
   ELSE
      RddSetDefault( "DBFCDX" )
      USE ( cDbf ) ALIAS T SHARED NEW
   ENDIF
   IF Select( "T" ) == 0
      ? "USE failed"
      RETURN
   ENDIF

   ? "=== index-order probe — RDD=" + T->( RddName() ) + " ==="
   ? "records (record order):"
   T->( DbGoTop() )
   DO WHILE ! T->( Eof() )
      ? "  rec " + Str( T->( RecNo() ), 2 ) + "  NAME=[" + ;
        AllTrim( T->NAME ) + "]"
      T->( DbSkip() )
   ENDDO

   INDEX ON T->NAME TAG NAME
   OrdSetFocus( "NAME" )

   ?
   ? "OrdKeyCount=" + Str( T->( OrdKeyCount() ), 3 ) + ;
     "  (expected " + Str( Len( aNames ), 3 ) + ")"
   ?
   ? "index walk (GoTop + Skip) — should be NAME-ascending:"
   lSorted := .T.
   cPrev := ""
   i := 0
   T->( DbGoTop() )
   DO WHILE ! T->( Eof() ) .AND. i < 50
      i++
      ? "  KeyNo " + Str( T->( OrdKeyNo() ), 2 ) + ;
        "  rec " + Str( T->( RecNo() ), 2 ) + ;
        "  NAME=[" + AllTrim( T->NAME ) + "]"
      IF AllTrim( T->NAME ) < cPrev
         lSorted := .F.
      ENDIF
      cPrev := AllTrim( T->NAME )
      T->( DbSkip() )
   ENDDO
   ? "walked " + Str( i, 2 ) + " keys   SORTED=" + iif( lSorted, "YES", "NO  <<< BUG" )

   ?
   ? "seek probes:"
   SeekOne( "Anna" )    // first alphabetically
   SeekOne( "Mike" )    // middle
   SeekOne( "Zoe" )     // last
   SeekOne( "Carl" )

   CLOSE ALL
   RETURN

//----------------------------------------------------------------------

STATIC PROCEDURE SeekOne( cKey )
   LOCAL lHit := T->( DbSeek( cKey ) )
   ? "  Seek([" + cKey + "]) -> found=" + iif( lHit, "YES", "no " ) + ;
     "  Eof=" + iif( T->( Eof() ), "T", "F" ) + ;
     "  rec=" + Str( T->( RecNo() ), 2 ) + ;
     "  NAME=[" + iif( T->( Eof() ), "<eof>", AllTrim( T->NAME ) ) + "]"
   RETURN

STATIC FUNCTION TempFolder()
   LOCAL c := GetEnv( "TEMP" )
   IF Empty( c ) ; c := GetEnv( "TMP" ) ; ENDIF
   IF Empty( c ) ; c := "C:\Temp" ; ENDIF
   RETURN c

STATIC FUNCTION StageDbf( cDir, cDbf, aNames )
   LOCAL cName
   IF ! hb_DirExists( cDir ) ; hb_DirCreate( cDir ) ; ENDIF
   IF File( cDbf ) ; FErase( cDbf ) ; ENDIF
   IF File( hb_FNameExtSet( cDbf, ".cdx" ) )
      FErase( hb_FNameExtSet( cDbf, ".cdx" ) )
   ENDIF
   DbCreate( cDbf, { { "NAME", "C", 12, 0 }, { "AGE", "N", 3, 0 } } )
   USE ( cDbf ) ALIAS _S SHARED NEW
   FOR EACH cName IN aNames
      _S->( DbAppend() )
      _S->NAME := cName
      _S->AGE  := 20 + cName:__enumIndex()
   NEXT
   _S->( DbCommit() )
   _S->( DbCloseArea() )
   RETURN nil
