// AdsSmoke_remote.prg — drives X#'s Advantage RDD against a *remote*
// OpenADS server (openads_serverd) over the wire, instead of local
// DBF files.
//
// How the X# RDD reaches a remote server (from XSharp.Rdd/Advantage):
//   1. AdsConnect60("tcp://host:port/<datadir>", ADS_REMOTE_SERVER,
//                   NULL, NULL, 0, OUT hConn)   — OpenADS' tcp:// URI.
//   2. AX_SetConnectionHandle(hConn)            — parks it in CoreDb's
//                                                 RDD info.
//   3. RddSetDefault("AXDBFCDX"); DbUseArea(...) — ADSRDD:Open then
//      calls AdsOpenTable90(hConn, ...) and every later nav/get goes
//      to the server.
//
// This variant opens an *existing* table on the server (it does not
// CREATE one — AdsCreateTable doesn't route remotely yet) and does
// read-only navigation, so it works against any data dir that has a
// non-empty `customer.dbf`.
//
// Build with the X# compiler; put OpenADS' ace64.dll first on PATH.
// Server URI: env var OPENADS_XS_REMOTE, else the documented dev box.
// Exit code 0 = pass.

#using System

FUNCTION Start() AS INT
    TRY
        RETURN RunRemote()
    CATCH e AS Exception
        ? "FAIL: exception " + e:GetType():FullName + ": " + e:Message
        RETURN 2
    END TRY

STATIC FUNCTION Fail(cMsg AS STRING) AS INT
    ? "FAIL:", cMsg
    RETURN 1

STATIC FUNCTION RunRemote() AS INT
    LOCAL cUri  AS STRING
    LOCAL hConn AS IntPtr
    LOCAL rc    AS DWORD

    cUri := Environment.GetEnvironmentVariable("OPENADS_XS_REMOTE")
    IF String.IsNullOrEmpty(cUri)
        cUri := "tcp://192.168.18.184:16262//tmp/openads_mac"
    ENDIF
    ? "ace64.dll in use:", System.IntPtr.Size, "byte handles"
    ? "Remote URI:", cUri

    rc := AdsConnect60(cUri, 2, NULL_STRING, NULL_STRING, 0u, OUT hConn)
    IF rc != 0
        RETURN Fail("AdsConnect60 rc = " + AsString(rc))
    ENDIF
    IF hConn == IntPtr.Zero
        RETURN Fail("AdsConnect60 returned a null handle")
    ENDIF
    AX_SetConnectionHandle(hConn)

    RddSetDefault("AXDBFCDX")
    IF .NOT. DbUseArea(TRUE, "AXDBFCDX", "customer", "cust", TRUE, TRUE)
        RETURN Fail("DbUseArea 'customer' (remote)")
    ENDIF

    LOCAL n AS INT
    n := RecCount()
    IF n <= 0
        RETURN Fail("RecCount() = " + AsString(n))
    ENDIF
    ? "customer.dbf records:", AsString(n)

    DbGoTop()
    IF Bof() .OR. (INT) RecNo() != 1
        RETURN Fail("after GoTop: RecNo=" + AsString(RecNo()) + " Bof=" + AsString(Bof()))
    ENDIF
    LOCAL cTopRec AS INT
    cTopRec := (INT) RecNo()

    DbSkip(1)
    IF n > 1 .AND. (INT) RecNo() != 2
        RETURN Fail("after Skip+1: RecNo=" + AsString(RecNo()))
    ENDIF

    DbGoBottom()
    IF Eof() .OR. (INT) RecNo() != n
        RETURN Fail("after GoBottom: RecNo=" + AsString(RecNo()) + " (expected " + AsString(n) + ")")
    ENDIF
    DbSkip(1)
    IF .NOT. Eof()
        RETURN Fail("expected Eof() after GoBottom + Skip+1")
    ENDIF

    DbGoTop()
    IF (INT) RecNo() != cTopRec
        RETURN Fail("re-GoTop: RecNo=" + AsString(RecNo()) + " (expected " + AsString(cTopRec) + ")")
    ENDIF

    // Touch field 1 — schema-agnostic, just verify the read path works.
    LOCAL oVal AS OBJECT
    oVal := FieldGet(1)
    IF oVal == NULL
        RETURN Fail("FieldGet(1) returned NULL")
    ENDIF

    DbCloseArea()
    AdsDisconnect(hConn)
    ? "OK: X# remote smoke passed (" + cUri + ")"
    RETURN 0
