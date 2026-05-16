<?php
declare(strict_types=1);

namespace OpenADS\Ffi;

use FFI;
use OpenADS\Exception\OpenAdsException;

/**
 * Loads the ACE shared library and declares the C surface the
 * binding calls. The only class that references \FFI directly.
 */
final class AceLibrary
{
    /** C declarations for the ACE exports the binding uses. */
    private const CDEF = <<<'C'
        typedef uint32_t UNSIGNED32;
        typedef uint16_t UNSIGNED16;
        typedef uint8_t  UNSIGNED8;
        typedef uint64_t ADSHANDLE;

        UNSIGNED32 AdsConnect60(UNSIGNED8* pucServer, UNSIGNED16 usServerType,
            UNSIGNED8* pucUserName, UNSIGNED8* pucPassword,
            UNSIGNED32 ulOptions, ADSHANDLE* phConnect);
        UNSIGNED32 AdsDisconnect(ADSHANDLE hConnect);
        UNSIGNED32 AdsGetLastError(UNSIGNED32* pulCode, UNSIGNED8* pucBuf,
            UNSIGNED16* pusBufLen);

        UNSIGNED32 AdsCreateSQLStatement(ADSHANDLE hConnect, ADSHANDLE* phStatement);
        UNSIGNED32 AdsCloseSQLStatement(ADSHANDLE hStatement);
        UNSIGNED32 AdsExecuteSQLDirect(ADSHANDLE hStatement, UNSIGNED8* pucSQL,
            ADSHANDLE* phCursor);

        UNSIGNED32 AdsOpenTable(ADSHANDLE hConnect, UNSIGNED8* pucName,
            UNSIGNED8* pucAlias, UNSIGNED16 usTableType, UNSIGNED16 usCharType,
            UNSIGNED16 usLockType, UNSIGNED16 usCheckRights, UNSIGNED16 usMode,
            ADSHANDLE* phTable);
        UNSIGNED32 AdsCloseTable(ADSHANDLE hTable);

        UNSIGNED32 AdsGotoTop(ADSHANDLE hTable);
        UNSIGNED32 AdsGotoBottom(ADSHANDLE hTable);
        UNSIGNED32 AdsGotoRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord);
        UNSIGNED32 AdsSkip(ADSHANDLE hTable, int32_t lRows);
        UNSIGNED32 AdsAtEOF(ADSHANDLE hTable, UNSIGNED16* pbAtEnd);
        UNSIGNED32 AdsAtBOF(ADSHANDLE hTable, UNSIGNED16* pbAtBegin);
        UNSIGNED32 AdsGetRecordCount(ADSHANDLE hTable, UNSIGNED16 bFilterOption,
            UNSIGNED32* pulRecordCount);
        UNSIGNED32 AdsGetRecordNum(ADSHANDLE hTable, UNSIGNED16 bFilterOption,
            UNSIGNED32* pulRecordNum);

        UNSIGNED32 AdsGetNumFields(ADSHANDLE hTable, UNSIGNED16* pusFields);
        UNSIGNED32 AdsGetFieldName(ADSHANDLE hTable, UNSIGNED16 usFieldNum,
            UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
        UNSIGNED32 AdsGetFieldType(ADSHANDLE hTable, UNSIGNED8* pucField,
            UNSIGNED16* pusType);
        UNSIGNED32 AdsGetField(ADSHANDLE hTable, UNSIGNED8* pucField,
            UNSIGNED8* pucBuf, UNSIGNED32* pulLen, UNSIGNED16 usOption);
        UNSIGNED32 AdsSetField(ADSHANDLE hObj, UNSIGNED8* pucFldId,
            UNSIGNED8* pucBuf, UNSIGNED32 ulLen);

        UNSIGNED32 AdsAppendRecord(ADSHANDLE hTable);
        UNSIGNED32 AdsWriteRecord(ADSHANDLE hTable);
        UNSIGNED32 AdsDeleteRecord(ADSHANDLE hTable);
        UNSIGNED32 AdsRecallRecord(ADSHANDLE hTable);
        UNSIGNED32 AdsLockRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord);
        UNSIGNED32 AdsUnlockRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord);
        UNSIGNED32 AdsSetAOF(ADSHANDLE hTable, UNSIGNED8* pucCondition,
            UNSIGNED16 usOptions);
        UNSIGNED32 AdsClearAOF(ADSHANDLE hTable);
        C;

    private static ?self $instance = null;

    private function __construct(private FFI $ffi)
    {
    }

    /**
     * Load the library at $path (defaults to OPENADS_ACE_LIB).
     * Cached — repeated calls return the same instance.
     */
    public static function load(?string $path = null): self
    {
        if (self::$instance !== null) {
            return self::$instance;
        }
        $path ??= self::resolvePath();
        try {
            $ffi = FFI::cdef(self::CDEF, $path);
        } catch (\FFI\Exception $e) {
            throw new OpenAdsException("cannot load ACE library '$path': " . $e->getMessage());
        }
        return self::$instance = new self($ffi);
    }

    private static function resolvePath(): string
    {
        $env = getenv('OPENADS_ACE_LIB');
        if ($env !== false && $env !== '') {
            return $env;
        }
        $is64 = PHP_INT_SIZE === 8;
        if (PHP_OS_FAMILY === 'Windows') {
            return $is64 ? 'ace64.dll' : 'ace32.dll';
        }
        // CMake sets OUTPUT_NAME ace64/ace32, so the POSIX shared
        // object is libace64.so / libace32.so — not libace.so.
        return $is64 ? 'libace64.so' : 'libace32.so';
    }

    public function ffi(): FFI
    {
        return $this->ffi;
    }

    /** Allocate an ADSHANDLE out-parameter, zero-initialised. */
    public function newHandle(): FFI\CData
    {
        $h = $this->ffi->new('ADSHANDLE');
        $h->cdata = 0;
        return $h;
    }

    public function handleValue(FFI\CData $handle): int
    {
        return (int) $handle->cdata;
    }

    /** Pull the latest ACE error code + text after a failed call. */
    public function lastError(): array
    {
        $code = $this->ffi->new('UNSIGNED32');
        $len  = $this->ffi->new('UNSIGNED16');
        $len->cdata = 256;
        $buf  = $this->ffi->new('UNSIGNED8[256]');
        $this->ffi->AdsGetLastError(FFI::addr($code), $buf, FFI::addr($len));
        // FFI::string() requires a char* pointer, not an array CData — cast via addr of first element.
        $ptr  = FFI::cast('char*', FFI::addr($buf[0]));
        return [(int) $code->cdata, FFI::string($ptr)];
    }
}
