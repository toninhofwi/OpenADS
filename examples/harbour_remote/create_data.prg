/*
 * create_data.prg — regenerate sample CCOLONIA.DBF in ./data/
 *
 * Run locally (ADS_LOCAL_SERVER) after building with build.cmd:
 *   hbmk2 colonias_console.hbp -ocreate_data create_data.prg adsindex.c
 * Or use the pre-shipped data/CCOLONIA.DBF and skip this step.
 */
#include "ads.ch"

REQUEST ADS, ADSCDX

PROCEDURE Main()

   LOCAL cDir := CurDrive() + CurDir() + hb_ps() + "data"
   LOCAL cDbf := cDir + hb_ps() + "CCOLONIA.DBF"

   AdsSetServerType( ADS_LOCAL_SERVER )
   AdsSetFileType( ADS_CDX )

   hb_DirBuild( cDir )

   IF File( cDbf )
      FErase( cDbf )
      FErase( hb_FNameExtSet( cDbf, ".cdx" ) )
   ENDIF

   DbCreate( cDbf, { ;
      { "COLONIA", "C", 50, 0 }, ;
      { "NOMBRE",  "C", 50, 0 }, ;
      { "CP",      "C",  5, 0 }, ;
      { "CIUDAD",  "C", 30, 0 } }, "ADSCDX" )

   USE ( cDbf ) ALIAS CCOLONIA NEW EXCLUSIVE VIA "ADSCDX"

   SeedRow( "Centro",       "Av. Hidalgo",                  "06000", "Ciudad de Mexico" )
   SeedRow( "Roma Norte",   "Calle Orizaba",                "06700", "Ciudad de Mexico" )
   SeedRow( "Condesa",      "Av. Amsterdam",                "06140", "Ciudad de Mexico" )
   SeedRow( "Coyoacan",     "Calle Francisco Sosa",         "04000", "Ciudad de Mexico" )
   SeedRow( "San Angel",    "Calle Diego Rivera",           "01000", "Ciudad de Mexico" )
   SeedRow( "Polanco",      "Av. Presidente Masaryk",       "11560", "Ciudad de Mexico" )
   SeedRow( "Santa Fe",     "Av. Carlos Graef Fernandez",   "05348", "Ciudad de Mexico" )
   SeedRow( "Azcapotzalco", "Calle Comonfort",              "02000", "Ciudad de Mexico" )

   DbCommit()
   DbCloseArea()

   ? "Created:", cDbf
   ? "Records: 8 (indexes are built remotely by AdsCreateIndex in the demo)"

   RETURN

STATIC PROCEDURE SeedRow( cColonia, cNombre, cCp, cCiudad )

   APPEND BLANK
   CCOLONIA->COLONIA := cColonia
   CCOLONIA->NOMBRE  := cNombre
   CCOLONIA->CP      := cCp
   CCOLONIA->CIUDAD  := cCiudad

   RETURN