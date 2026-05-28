<?php
/**
 * openads extension stubs for IDE only.
 *
 * This file is intentionally placed in the workspace so PHP language servers
 * (Intelephense, PHP Language Server, Psalm, etc.) can index the classes
 * provided by the `php_openads` extension. It does nothing at runtime when
 * the real extension is available.
 */

if (!extension_loaded('openads') && !class_exists('AdsConnection')) {
    class AdsException extends Exception {}

    class AdsConnection
    {
        /**
         * Connect to an OpenADS DD.
         *
         * @param array $opts Array with keys: 'path' (string), 'user' (string|null), 'password' (string|null)
         * @return AdsConnection
         */
        public static function connect(array $opts) {}

        /**
         * Close the connection.
         * @return void
         */
        public function close() {}

        // Add other methods/signatures here if your code uses them.
    }
}
