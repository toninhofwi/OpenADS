-- ABI/NAV fixture for OpenADS mariadb:// backend
DROP TABLE IF EXISTS clientes;
CREATE TABLE clientes (
  id   INTEGER NOT NULL PRIMARY KEY,
  nome VARCHAR(64),
  saldo DOUBLE
);
INSERT INTO clientes (id, nome, saldo) VALUES
  (1, 'Ana', 10.5),
  (2, 'Bob', NULL),
  (3, 'Cid', 0.0);