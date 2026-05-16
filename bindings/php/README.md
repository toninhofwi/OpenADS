# OpenADS PHP binding

Modern PHP binding for the [OpenADS](https://github.com/FiveTechSoft/OpenADS)
database engine. Wraps the ACE C library through PHP's FFI
extension — no compiled C code, install with Composer.

## Requirements

- PHP 8.1 or newer
- `ext-ffi` enabled (`ffi.enable=1` in `php.ini`)
- An OpenADS ACE library reachable on the host:
  `ace64.dll` / `ace32.dll` (Windows) or `libace64.so` /
  `libace32.so` (Linux/macOS).
  Point `OPENADS_ACE_LIB` at it, or place it on the system
  library path.

## Install

```bash
composer require openads/openads-php
```

## Connecting

Local data directory:

```php
use OpenADS\Connection;

$conn = new Connection('/var/data/myapp');
```

Remote `openads_serverd`:

```php
$conn = new Connection('tcp://db.example.com:6262/myapp', 'user', 'pass');
```

## SQL with parameters

```php
$stmt   = $conn->statement();
$cursor = $stmt->query(
    'SELECT * FROM people WHERE name = ?',
    ["O'Brien"]            // quoted safely; never concatenate
);
foreach ($cursor as $row) {
    echo $row['NAME'], "\n";
}
```

Named parameters work too: `:id` with `[':id' => 5]`.

## Navigational access

```php
$table = $conn->table('people');
for ($table->gotoTop(); !$table->atEof(); $table->skip(1)) {
    echo $table->record()->get('NAME'), "\n";
}
```

## Writes

```php
$rec = $conn->table('people')->record();
$rec->append();
$rec->set('ID', 42);
$rec->set('NAME', 'Reinaldo');
$rec->save();
```

## License

Apache-2.0. See the repository `LICENSE`.
