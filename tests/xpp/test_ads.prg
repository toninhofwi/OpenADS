//////////////////////////////////////////////////////////////////////
//
//  TEST_ADS.PRG
//
//  Xbase++ smoke test for OpenADS -- raw ACE C API called through
//  DllPrepareCall / DllExecuteCall (cdecl) against openace32.dll.
//  No ADSDBE, no Advantage Local Server: this exercises OpenADS's
//  exported ABI from Xbase++, proving calling convention + marshaling.
//
//  Why not DllCall(): DllCall silently drops arguments past ~8, so the
//  10-arg AdsCreateTable lost its field-def string ("no fields"). The
//  prepared-template path (DllPrepareCall) takes an explicit type list,
//  has no such limit, marshals @byref outputs via DLL_CALLMODE_COPY,
//  and restores the FPU (DLL_CALLMODE_RESTOREFPU) for safety.
//
//  Drop OpenADS in as openace32.dll next to the .exe (the 32-bit
//  build/release-x86 build). cdecl exports, undecorated names.
//
//  Console output (?) goes to a PM window, not stdout, so every step
//  is logged to test_result.log for headless verification.
//
//  Flow: prepare -> connect(local) -> create -> append -> index ->
//        seek -> read-back -> close.
//
//////////////////////////////////////////////////////////////////////

#include "dll.ch"

// --- ACE constants (Xbase++ ads.ch lacks the raw ACE values) ---
#define ADS_LOCAL_SERVER         1
#define ADS_CDX                  2
#define ADS_ANSI                 1
#define ADS_PROPRIETARY_LOCKING  1
#define ADS_IGNORERIGHTS         4
#define ADS_STRINGKEY            1
#define ADS_SOFTSEEK             1

#define TEST_DIR  "C:\OpenADS\tests\xpp"
#define ACE_DLL   "openace32.dll"

// cdecl, UNSIGNED32 return, copy byref outputs back, restore FPU.
#define ACE_FLAGS  (DLL_CDECL + DLL_TYPE_UINT32 + DLL_CALLMODE_COPY + DLL_CALLMODE_RESTOREFPU)

// ADSHANDLE is uint64_t in OpenADS -- even on the 32-bit DLL. Handles
// passed BY VALUE occupy two stack slots; getting this wrong shifts
// every following argument by 4 bytes. So every hConn/hTable/hIndex
// (in and out) is marshaled as 64-bit, not UINT32.
#define DLL_TYPE_HANDLE  DLL_TYPE_INT64

STATIC s_nLog := -1

PROCEDURE Main
   LOCAL nRC
   LOCAL hConn  := 0, hTable := 0, hIndex := 0
   LOCAL aData, i
   LOCAL nFound := 0, nRecno := 0, nId := 0, nCount := 0
   LOCAL cName, cFields
   LOCAL tConnect, tCreate, tAppend, tSetStr, tCount, tIndex, tSeek, tRecNo, tGetLong, tClose, tDisc

   FErase("test_result.log")
   s_nLog := FCreate("test_result.log")
   LogLine("=== OpenADS Xbase++ raw-ACE (DllPrepareCall/cdecl) Smoke Test ===")

   // --- Prepare typed call templates (one per export) ---
   tConnect  := DllPrepareCall(ACE_DLL, ACE_FLAGS, "AdsConnect60", ;
                  DLL_TYPE_STRING, DLL_TYPE_UINT32, DLL_TYPE_STRING, DLL_TYPE_STRING, ;
                  DLL_TYPE_UINT32, DLL_TYPE_HANDLE)
   tCreate   := DllPrepareCall(ACE_DLL, ACE_FLAGS, "AdsCreateTable", ;
                  DLL_TYPE_HANDLE, DLL_TYPE_STRING, DLL_TYPE_STRING, DLL_TYPE_UINT32, ;
                  DLL_TYPE_UINT32, DLL_TYPE_UINT32, DLL_TYPE_UINT32, DLL_TYPE_UINT32, ;
                  DLL_TYPE_STRING, DLL_TYPE_HANDLE)
   tAppend   := DllPrepareCall(ACE_DLL, ACE_FLAGS, "AdsAppendRecord", DLL_TYPE_HANDLE)
   tSetStr   := DllPrepareCall(ACE_DLL, ACE_FLAGS, "AdsSetString", ;
                  DLL_TYPE_HANDLE, DLL_TYPE_STRING, DLL_TYPE_STRING, DLL_TYPE_UINT32)
   tCount    := DllPrepareCall(ACE_DLL, ACE_FLAGS, "AdsGetRecordCount", ;
                  DLL_TYPE_HANDLE, DLL_TYPE_UINT32, DLL_TYPE_UINT32)
   tIndex    := DllPrepareCall(ACE_DLL, ACE_FLAGS, "AdsCreateIndex61", ;
                  DLL_TYPE_HANDLE, DLL_TYPE_STRING, DLL_TYPE_STRING, DLL_TYPE_STRING, ;
                  DLL_TYPE_STRING, DLL_TYPE_STRING, DLL_TYPE_UINT32, DLL_TYPE_UINT32, ;
                  DLL_TYPE_HANDLE)
   tSeek     := DllPrepareCall(ACE_DLL, ACE_FLAGS, "AdsSeek", ;
                  DLL_TYPE_HANDLE, DLL_TYPE_STRING, DLL_TYPE_UINT32, DLL_TYPE_UINT32, ;
                  DLL_TYPE_UINT32, DLL_TYPE_UINT32)
   tRecNo    := DllPrepareCall(ACE_DLL, ACE_FLAGS, "AdsGetRecordNum", ;
                  DLL_TYPE_HANDLE, DLL_TYPE_UINT32, DLL_TYPE_UINT32)
   tGetLong  := DllPrepareCall(ACE_DLL, ACE_FLAGS, "AdsGetLong", ;
                  DLL_TYPE_HANDLE, DLL_TYPE_STRING, DLL_TYPE_INT32)
   tClose    := DllPrepareCall(ACE_DLL, ACE_FLAGS, "AdsCloseTable", DLL_TYPE_HANDLE)
   tDisc     := DllPrepareCall(ACE_DLL, ACE_FLAGS, "AdsDisconnect", DLL_TYPE_HANDLE)

   IF tConnect == NIL .OR. tCreate == NIL
      LogLine("FAIL: DllPrepareCall returned NIL (DLL not found / export missing)")
      Done(1)
   ENDIF
   LogLine("templates prepared")

   // --- 1. Connect to local data dir ---
   nRC := DllExecuteCall(tConnect, TEST_DIR, ADS_LOCAL_SERVER, "", "", 0, @hConn)
   IF nRC != 0 .OR. hConn == 0
      LogLine("[1] FAIL AdsConnect60 rc=" + Ltrim(Str(nRC)) + " hConn=" + Ltrim(Str(hConn)))
      Done(1)
   ENDIF
   LogLine("[1] OK connect, hConn=" + Ltrim(Str(hConn)))

   // --- 2. Create CDX table (fields via def string) ---
   cFields := "ID,N,8,0;NAME,C,20,0;"
   nRC := DllExecuteCall(tCreate, hConn, "test_ads", "", ADS_CDX, ADS_ANSI, ;
                  ADS_PROPRIETARY_LOCKING, ADS_IGNORERIGHTS, 0, cFields, @hTable)
   IF nRC != 0 .OR. hTable == 0
      LogLine("[2] FAIL AdsCreateTable rc=" + Ltrim(Str(nRC)) + " hTable=" + Ltrim(Str(hTable)))
      LogLastErr()
      Done(1)
   ENDIF
   LogLine("[2] OK create, hTable=" + Ltrim(Str(hTable)))

   // --- 3. Append 5 records (numeric set as text -> set_field coerces) ---
   aData := {{"1","Alice"}, {"2","Bob"}, {"3","Charlie"}, {"4","David"}, {"5","Eve"}}
   FOR i := 1 TO Len(aData)
      DllExecuteCall(tAppend, hTable)
      DllExecuteCall(tSetStr, hTable, "ID",   aData[i,1], Len(aData[i,1]))
      DllExecuteCall(tSetStr, hTable, "NAME", aData[i,2], Len(aData[i,2]))
   NEXT
   DllExecuteCall(tCount, hTable, 0, @nCount)
   IF nCount != 5
      LogLine("[3] FAIL record count=" + Ltrim(Str(nCount)) + " expected 5"); Done(1)
   ENDIF
   LogLine("[3] OK appended 5, count=" + Ltrim(Str(nCount)))

   // --- 4. Build CDX index on NAME ---
   nRC := DllExecuteCall(tIndex, hTable, "test_ads.cdx", "NAME_TAG", "NAME", ;
                  "", "", 0, 512, @hIndex)
   IF nRC != 0 .OR. hIndex == 0
      LogLine("[4] FAIL AdsCreateIndex61 rc=" + Ltrim(Str(nRC)) + " hIndex=" + Ltrim(Str(hIndex)))
      Done(1)
   ENDIF
   LogLine("[4] OK index NAME_TAG, hIndex=" + Ltrim(Str(hIndex)))

   // --- 5. Seek "Charlie", read back recno + ID ---
   cName := "Charlie"
   nRC := DllExecuteCall(tSeek, hIndex, cName, Len(cName), ADS_STRINGKEY, ADS_SOFTSEEK, @nFound)
   IF nRC != 0 .OR. nFound == 0
      LogLine("[5] FAIL AdsSeek rc=" + Ltrim(Str(nRC)) + " found=" + Ltrim(Str(nFound)))
      Done(1)
   ENDIF
   DllExecuteCall(tRecNo,   hTable, 0, @nRecno)
   DllExecuteCall(tGetLong, hTable, "ID", @nId)
   LogLine("[5] seek Charlie -> recno=" + Ltrim(Str(nRecno)) + " ID=" + Ltrim(Str(nId)))
   IF nId != 3
      LogLine("[5] FAIL expected ID=3, got " + Ltrim(Str(nId))); Done(1)
   ENDIF
   LogLine("[5] OK seek + readback")

   // --- 6. Close ---
   DllExecuteCall(tClose, hTable)
   DllExecuteCall(tDisc,  hConn)
   LogLine("[6] OK closed + disconnected")

   LogLine("=== All tests passed ===")
   Done(0)
RETURN


PROCEDURE LogLastErr()
   LOCAL t, nEC := 0, cEB := Space(260), nEL := 260
   t := DllPrepareCall(ACE_DLL, ACE_FLAGS, "AdsGetLastError", ;
          DLL_TYPE_UINT32, DLL_TYPE_STRING, DLL_TYPE_UINT32)
   DllExecuteCall(t, @nEC, @cEB, @nEL)
   LogLine("      lasterr code=" + Ltrim(Str(nEC)) + " len=" + Ltrim(Str(nEL)) + ;
           " msg=[" + Left(cEB, Max(nEL, 0)) + "]")
RETURN


PROCEDURE LogLine(cMsg)
   IF s_nLog >= 0
      FWrite(s_nLog, cMsg + Chr(13) + Chr(10))
   ENDIF
RETURN


PROCEDURE Done(nCode)
   LogLine("EXIT " + Ltrim(Str(nCode)))
   IF s_nLog >= 0
      FClose(s_nLog)
   ENDIF
   QUIT
RETURN
