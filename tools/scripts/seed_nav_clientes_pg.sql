-- ABI/NAV fixture for OpenADS postgresql:// backend
DROP TABLE IF EXISTS clientes;
CREATE TABLE clientes (
  id   INTEGER PRIMARY KEY,
  nome VARCHAR(64),
  saldo DOUBLE PRECISION
);
INSERT INTO clientes (id, nome, saldo) VALUES
  (1, 'Ana', 10.5),
  (2, 'Bob', NULL),
  (3, 'Cid', 0.0);