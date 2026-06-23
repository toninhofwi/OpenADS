# OpenADS — M6 ADI: índices nos tipos ADT restantes

**Projeto:** `openads2` (LOCAL-ONLY, fork Grok)  
**Branch base:** `feat/adt-adi-create-read-seek` (M5 mergeado localmente)  
**Atualizado:** 2026-06-22

> Milestone **local** da trilha ADT/ADI — **não** confundir com M6 upstream (Data Dictionary).

---

## 1. Objetivo

Fechar **criação + população + navegação + seek** de índices `.adi` em tabelas `.adt` criadas pelo OpenADS, para os tipos ADT que o M5 **não cobriu com índice**:

| Tipo ADT | Código | Layout chave ADI |
|----------|--------|------------------|
| Date     | 3      | 8 B total-order (JDN → `pack_double_key`) |
| Time     | 13     | 8 B total-order (ms desde meia-noite) |
| Timestamp| 14     | 8 B total-order (JDN+ms combinados) |
| Money    | 18     | 8 B total-order (IEEE754 LE → BE flipped) |
| Logical  | 1      | 8 B total-order (**falta hoje** em `encode_adt_key`) |

**Critério de pronto:** cada tipo passa em teste doctest local (create → append → `AdsCreateIndex61` → `AdsGotoTop`/`AdsSkip` ordem correta → `AdsSeek` exato).

---

## 2. Fora de escopo (M6)

| Item | Motivo | Milestone |
|------|--------|-----------|
| **CICHAR / CiChar** | layout char-key 7 páginas + case-fold; leitura de fixtures SAP já parcial, **create** não certificado | **M7** |
| **Descendente** (`ADS_DESCENDING`) | `AdiIndex::descending()` fixo `false`; sem bit on-disk ADI | **M7** |
| **Composto** (`ADS_COMPOUND` / `expr` multi-campo) | F-markers múltiplos, concat de componentes | **M7** |
| Interop DLL SAP nativa | ADI OpenADS↔vendor = ERRO conhecido (`CLASSIFICACAO_C_engine_vs_RE.LOCAL.md` §6) | nunca neste PR |
| Fixtures `testdata/pmsys/` | copyright / quarentena | nunca neste PR |
| `tests/smoke/adt_sap_cross_*` | harness vendor | nunca neste PR |

---

## 3. Estado atual (pós-M5)

### Verde (M5)

- `AdsCreateTable(ADS_ADT)` + `AdsCreateIndex61` → `.adi` novo
- Índice **Character** (tag char, 7 páginas) e **Numeric/Integer** (8 B, 6 páginas)
- `AdiIndex::insert` para tags numéricas **ignora** `evaluate_index_expr` e usa `key_for_recno_()` → `encode_adt_key()` (correto)
- Testes: `abi_adi_create_test.cpp`, `abi_adt_scope_validation_test.cpp` (tabela com Date/Time/… mas **sem** índice nesses campos)

### Gap M6

```cpp
// adi_index.cpp — encode_adt_key()
default:
    return std::string(8, '\0');  // LOGICAL cai aqui → todas as chaves iguais
```

- DATE / TIME / TIMESTAMP / MONEY: **código existe** em `encode_adt_key`, mas **sem teste de create+seek**
- LOGICAL: **não implementado**

---

## 4. Entregas

| ID | Entrega | Arquivo(s) |
|----|---------|------------|
| M6.1 | `encode_adt_key`: caso `ADT_TYPE_LOGICAL` (byte on-disk `'T'`/`'F'` ou 0/1 → chave 8 B ordenável) | `src/drivers/adi/adi_index.cpp` |
| M6.2 | Revisar TIMESTAMP (4+4 LE) e DATE (JDN u32) — ajustar só se teste falhar | idem |
| M6.3 | Testes `abi_adi_types_test.cpp`: 5 `TEST_CASE` (um por tipo), padrão M5 | `tests/unit/` + `CMakeLists.txt` |
| M6.4 | Tag doctest `M6 ADI` para filtro CI local | idem |

### Padrão de teste (cada tipo)

1. `AdsConnect60` local, temp dir
2. `AdsCreateTable` com **um** campo indexável + chave auxiliar (ex.: `Tag,Character,10` para desempate visual)
3. Append 3 registros com valores **fora de ordem**
4. `AdsCreateIndex61` expressão = nome do campo (bare field)
5. `AdsSetIndexOrderByHandle` + `AdsGotoTop` + `AdsSkip` → ordem ascendente verificada
6. `AdsSeek` no valor médio → recno correto

### Valores de referência (tabela `tipos_idx.adt`)

| Campo | Tipo | Valores append (ordem inserção ≠ ordem índice) |
|-------|------|------------------------------------------------|
| `Nasc` | Date | `20000615`, `19991231`, `19850315` |
| `Hora` | Time | 52200000 ms, 3600000 ms, 0 ms |
| `Criado` | Timestamp | `20251225120000`, `20250621143000`, `19990101120000` |
| `Saldo` | Money | 300.50, 100.00, 200.25 |
| `Ativo` | Logical | T, F, T |

Reutilizar helpers de `abi_adt_scope_validation_test.cpp` (`set_field_str`, `AdsSetLogical`, etc.).

---

## 5. Comando de validação

```bat
cmake --build build\plus-msvc-x86 --config Release --target openads_unit_tests
build\plus-msvc-x86\tests\openads_unit_tests.exe -tc="M6 ADI"
```

Sem `serverd`, sem Harbour, sem DLL vendor.

---

## 6. PR

- **Título sugerido:** `feat(adt): M6 ADI index keys for Date/Time/Timestamp/Money/Logical`
- **Base:** `feat/adt-adi-create-read-seek` (ou `main` após merge M5)
- **Diff esperado:** ~1 arquivo engine + 1 arquivo teste + CMake — sem docs prose extras, sem smoke cross-vendor

---

## 7. M7 (próxima fatia — só referência)

1. CICHAR create + seek case-insensitive  
2. `ADS_DESCENDING` persistido no per-tag header ADI  
3. Índice composto (`propertyID;EndDate` etc.)  
4. (Opcional) reabrir interop ADI se layout convergir — hoje **bloqueado**

---

## 8. Referências

- M5 testes: `tests/unit/abi_adi_create_test.cpp`, `abi_adt_scope_validation_test.cpp`
- Engine: `src/drivers/adi/adi_index.{h,cpp}`, `src/abi/ace_exports.cpp` (`AdsCreateIndex61` ramo `.adi`)
- Plano geral ADT: `docs/PLANO_ADT_ADI_REAL.md` (fase 2 = create; M6 = tipos de chave)
- Gap interop: `CLASSIFICACAO_C_engine_vs_RE.LOCAL.md` §6