<?php
declare(strict_types=1);

namespace OpenADS;

use FFI;
use OpenADS\Exception\ConnectionException;
use OpenADS\Ffi\AceLibrary;
use OpenADS\Ffi\AceTypes;

final class Connection
{
    private AceLibrary $lib;
    private int $handle = 0;
    private bool $open = false;

    /**
     * @param string $uri  Local data-dir path, or tcp:// / tls:// URI.
     */
    public function __construct(
        string $uri,
        ?string $user = null,
        ?string $pass = null
    ) {
        $this->lib = AceLibrary::load();
        $ffi       = $this->lib->ffi();

        $serverType = str_starts_with($uri, 'tcp://') || str_starts_with($uri, 'tls://')
            ? AceTypes::ADS_REMOTE_SERVER
            : AceTypes::ADS_LOCAL_SERVER;

        $out = $this->lib->newHandle();
        $rc  = $ffi->AdsConnect60(
            self::cstr($ffi, $uri),
            $serverType,
            $user !== null ? self::cstr($ffi, $user) : null,
            $pass !== null ? self::cstr($ffi, $pass) : null,
            0,
            FFI::addr($out)
        );
        if ($rc !== AceTypes::AE_SUCCESS) {
            [$code, $text] = $this->lib->lastError();
            throw new ConnectionException(
                "connect to '$uri' failed: " . AceTypes::errorName($code) . " ($code): $text",
                $code
            );
        }
        $this->handle = $this->lib->handleValue($out);
        $this->open   = true;
    }

    public function isOpen(): bool
    {
        return $this->open;
    }

    public function handle(): int
    {
        return $this->handle;
    }

    public function library(): AceLibrary
    {
        return $this->lib;
    }

    public function statement(): Statement
    {
        return new Statement($this);
    }

    public function table(string $name): Table
    {
        return new Table($this, $name);
    }

    public function close(): void
    {
        if (!$this->open) {
            return;
        }
        $this->lib->ffi()->AdsDisconnect($this->handle);
        $this->open = false;
    }

    public function __destruct()
    {
        $this->close();
    }

    /** Allocate a NUL-terminated C string buffer for $s. */
    public static function cstr(FFI $ffi, string $s): FFI\CData
    {
        $n   = strlen($s);
        $buf = $ffi->new('UNSIGNED8[' . ($n + 1) . ']');
        FFI::memcpy($buf, $s, $n);
        $buf[$n] = 0;
        return $buf;
    }
}
