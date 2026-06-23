-- OpenADS ODBC ABI fixture: table clientes (id PK, nome VARCHAR(64), saldo DOUBLE)

SET TERM ^ ;
EXECUTE BLOCK AS
BEGIN
  IF (EXISTS(SELECT 1 FROM RDB$RELATIONS WHERE RDB$RELATION_NAME = 'CLIENTES')) THEN
    EXECUTE STATEMENT 'DROP TABLE CLIENTES';
END^
SET TERM ; ^

CREATE TABLE clientes (
  id   INTEGER NOT NULL PRIMARY KEY,
  nome VARCHAR(64),
  saldo DOUBLE PRECISION
);

INSERT INTO clientes (id, nome, saldo) VALUES (1, 'Ana', 10.5);
INSERT INTO clientes (id, nome, saldo) VALUES (2, 'Bob', NULL);
INSERT INTO clientes (id, nome, saldo) VALUES (3, 'Cid', 0.0);

COMMIT;
EXIT;