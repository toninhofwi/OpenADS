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
        /** @param array $opts keys: path, user, password */
        public static function connect(array $opts): static {}
        public function close(): void {}
        public function query(string $sql): AdsStatement {}
        public function execute(string $sql): void {}
        public function isAlive(): bool {}
    }

    class AdsStatement
    {
        public function fetchAssoc(): ?array {}
        public function close(): void {}
    }

    class AdsTable
    {
        /** @param int $mode 0=read/write, 1=read-only */
        public static function open(AdsConnection $conn, string $table, int $mode = 0): static {}
        public function close(): void {}
        /** @return array[] each: ['tag'=>string,'expression'=>string,'descending'=>bool] */
        public function getIndexTags(): array {}
        public function recordCount(): int {}
    }

    class AdsDictionary
    {
        public static function open(string $path): static {}
        public static function fromConnection(AdsConnection $conn): static {}
        public function close(): void {}
        public function getTableProperty(string $table, int $property): string {}
        public function setTableProperty(string $table, int $property, string $value): void {}
        public function getFieldProperty(string $table, string $field, int $property): string {}
        public function setFieldProperty(string $table, string $field, int $property, string $value): void {}
        public function getDatabaseProperty(int $property): string {}
        public function setDatabaseProperty(int $property, string $value): void {}
        public function addTable(string $alias, string $path, int $tableType = 3, int $charType = 1, string $indexPath = '', string $comment = ''): void {}
        public function removeTable(string $alias, bool $deleteFiles = false): void {}
        public function addIndexFile(string $table, string $indexPath, string $comment = ''): void {}
        public function removeIndexFile(string $table, string $indexPath): void {}
        // Index property codes: ADS_DD_INDEX_UNIQUE=408, ADS_DD_INDEX_DESCENDING=407, ADS_DD_INDEX_EXPR=401
        public function getIndexProperty(string $table, string $indexName, int $property): string {}
        public function setIndexProperty(string $table, string $indexName, int $property, string $value): void {}
        // Trigger property codes: 502=event 1402=timing 503=container 505=enabled 506=priority 507=comment
        public function getTriggerProperty(string $name, int $property): string {}
        public function setTriggerProperty(string $name, int $property, string $value): void {}
        public function createTrigger(string $name, string $table, int $type, string $container = '', string $procedure = '', int $priority = 1): void {}
        public function dropTrigger(string $name): void {}
    }
}
