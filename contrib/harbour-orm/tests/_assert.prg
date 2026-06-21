/* Minimal console assert harness. No external framework on this toolchain. */
#include "hborm.ch"

STATIC s_nPass := 0
STATIC s_nFail := 0

FUNCTION T_Ok( cName, lCond )
   IF lCond
      s_nPass++
      ? "  ok   " + cName
   ELSE
      s_nFail++
      ? "  FAIL " + cName
   ENDIF
   RETURN lCond

FUNCTION T_Eq( cName, xGot, xExp )
   LOCAL lOk := ( ValToPrg( xGot ) == ValToPrg( xExp ) )
   IF lOk
      s_nPass++
      ? "  ok   " + cName
   ELSE
      s_nFail++
      ? "  FAIL " + cName
      ? "         got: " + ValToPrg( xGot )
      ? "         exp: " + ValToPrg( xExp )
   ENDIF
   RETURN lOk

FUNCTION T_Summary()
   ?
   ? "Summary: pass=" + LTrim( Str( s_nPass ) ) + ;
     " fail=" + LTrim( Str( s_nFail ) )
   RETURN s_nFail

STATIC FUNCTION ValToPrg( x )
   RETURN iif( HB_ISSTRING( x ), x, hb_ValToExp( x ) )
