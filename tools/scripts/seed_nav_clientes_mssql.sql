-- ABI/NAV fixture for OpenADS ODBC -> SQL Server
IF OBJECT_ID('dbo.clientes', 'U') IS NOT NULL DROP TABLE dbo.clientes;
CREATE TABLE dbo.clientes (
  id    INT NOT NULL PRIMARY KEY,
  nome  NVARCHAR(64),
  saldo FLOAT
);
INSERT INTO dbo.clientes (id, nome, saldo) VALUES
  (1, N'Ana', 10.5),
  (2, N'Bob', NULL),
  (3, N'Cid', 0.0);