#!/usr/bin/env python3
"""Create SQLite NAV fixture (stdlib only)."""
from __future__ import annotations

import sqlite3
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve()
ADO_ROOT = SCRIPT.parents[2]
DEFAULT_DB = ADO_ROOT / "tools" / "bench" / "fixtures" / "nav_clientes.db"
SEED_SQL = SCRIPT.with_name("seed_nav_clientes_sqlite.sql")


def main() -> int:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_DB
    if not SEED_SQL.is_file():
        print(f"ERROR: seed SQL missing: {SEED_SQL}")
        return 1
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()
    sql = SEED_SQL.read_text(encoding="utf-8")
    with sqlite3.connect(out) as conn:
        conn.executescript(sql)
    print(f"OK: {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())