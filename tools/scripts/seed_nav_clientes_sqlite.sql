-- ABI/NAV fixture for OpenADS sqlite:// backend
DROP TABLE IF EXISTS clientes;
CREATE TABLE clientes (
  id   INTEGER PRIMARY KEY,
  nome TEXT,
  saldo REAL
);
INSERT INTO clientes (id, nome, saldo) VALUES
  (1, 'Ana', 10.5),
  (2, 'Bob', NULL),
  (3, 'Cid', 0.0);