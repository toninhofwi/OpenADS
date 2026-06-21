#!/usr/bin/env python3
"""Build NAV Harbour bench PDF from nav_bench_*_latest.json."""
from __future__ import annotations

import json
import os
import re
from datetime import datetime
from pathlib import Path

from fpdf import FPDF

SCRIPT = Path(__file__).resolve()
ADO_ROOT = SCRIPT.parents[2]
BENCH_DIR = ADO_ROOT / "tools" / "bench" / "results"
JSON_DEFAULT = BENCH_DIR / "nav_bench_odbc_latest.json"

OUT_BY_MODE = {
    "odbc": "OPENADS_NAV_BENCH_ODBC_FWH.pdf",
    "postgresql": "OPENADS_NAV_BENCH_PG_FWH.pdf",
    "pg": "OPENADS_NAV_BENCH_PG_FWH.pdf",
    "sqlite": "OPENADS_NAV_BENCH_SQLITE_FWH.pdf",
    "mssql": "OPENADS_NAV_BENCH_MSSQL_FWH.pdf",
    "mariadb": "OPENADS_NAV_BENCH_MARIA_FWH.pdf",
    "maria": "OPENADS_NAV_BENCH_MARIA_FWH.pdf",
}

BACKEND_LABELS = {
    "odbc": "ODBC / CLIENTES table (Firebird)",
    "postgresql": "PostgreSQL / clientes table (libpq)",
    "pg": "PostgreSQL / clientes table (libpq)",
    "sqlite": "SQLite / clientes table (sqlite://)",
    "mssql": "ODBC / clientes table (SQL Server)",
    "mariadb": "MariaDB / clientes table (mariadb://)",
    "maria": "MariaDB / clientes table (mariadb://)",
}
METHODOLOGY_BULLETS = {
    "odbc": [
        "One cycle = ODBC connect + AdsOpenTable + read 3 rows + disconnect.",
        "Direct ACE glue (OADS_Connect60); build without rddads.",
        "Fixture: OPENADS_ODBC_FIXTURE + ODBC driver (env).",
        "Small stress run: 30 typical cycles; warm-up 1 discarded.",
        "Full doc: docs/OPENADS_NAV_BENCH_METHODOLOGY.md in the OpenADS repo.",
    ],
    "postgresql": [
        "One cycle = postgresql:// connect + AdsOpenTable + 3 rows + disconnect.",
        "Direct ACE glue (OADS_Connect60); build without rddads; pg-msvc DLL.",
        "Fixture: seed_nav_clientes_pg.sql (OPENADS_TEST_PG_URI).",
        "Small stress run: 30 typical cycles; warm-up 1 discarded.",
        "Full doc: docs/OPENADS_NAV_BENCH_METHODOLOGY.md in the OpenADS repo.",
    ],
    "sqlite": [
        "One cycle = sqlite:// connect + AdsOpenTable + 3 rows + disconnect.",
        "Direct ACE glue (OADS_Connect60); build without rddads; sqlpass DLL.",
        "Fixture: tools/bench/fixtures/nav_clientes.db (seed_nav_clientes_sqlite.py).",
        "Small stress run: 30 typical cycles; warm-up 1 discarded.",
        "Full doc: docs/OPENADS_NAV_BENCH_METHODOLOGY.md in the OpenADS repo.",
    ],
    "mssql": [
        "One cycle = ODBC SQL Server connect + AdsOpenTable + 3 rows + disconnect.",
        "Direct ACE glue (OADS_Connect60); ODBC DLL; Enterprise driver via env.",
        "Fixture: seed_nav_clientes_mssql.sql (OPENADS_MSSQL_ODBC_CONNSTR).",
        "Small stress run: 30 typical cycles; warm-up 1 discarded.",
        "Full doc: docs/OPENADS_NAV_BENCH_METHODOLOGY.md in the OpenADS repo.",
    ],
    "mariadb": [
        "One cycle = mariadb:// connect + AdsOpenTable + 3 rows + disconnect.",
        "Direct ACE glue (OADS_Connect60); build without rddads; maria-msvc DLL.",
        "Fixture: seed_nav_clientes_maria.sql (OPENADS_TEST_MARIADB_URI).",
        "Small stress run: 30 typical cycles; warm-up 1 discarded.",
        "Full doc: docs/OPENADS_NAV_BENCH_METHODOLOGY.md in the OpenADS repo.",
    ],
    "maria": [
        "One cycle = mariadb:// connect + AdsOpenTable + 3 rows + disconnect.",
        "Direct ACE glue (OADS_Connect60); build without rddads; maria-msvc DLL.",
        "Fixture: seed_nav_clientes_maria.sql (OPENADS_TEST_MARIADB_URI).",
        "Small stress run: 30 typical cycles; warm-up 1 discarded.",
        "Full doc: docs/OPENADS_NAV_BENCH_METHODOLOGY.md in the OpenADS repo.",
    ],
}


def _doc_global_dir() -> Path | None:
    raw = os.environ.get("OPENADS_DOC_GLOBAL_DIR", "").strip()
    if raw:
        return Path(raw)
    return None


def clean(text: str) -> str:
    text = text.replace("\u2014", "-").replace("\u2013", "-")
    return text.encode("latin-1", "replace").decode("latin-1")


class BenchPDF(FPDF):
    def header(self):
        self.set_font("Helvetica", "B", 10)
        self.cell(0, 7, "OpenADS NAV bench - TOpenAdsConnection + AdsOpenTable", align="C", new_x="LMARGIN", new_y="NEXT")
        self.ln(1)

    def footer(self):
        self.set_y(-12)
        self.set_font("Helvetica", "I", 8)
        self.cell(0, 8, f"Page {self.page_no()}/{{nb}}", align="C")


def _norm_stdout(stdout: str) -> str:
    """Collapse accidental line breaks inside BENCH_* lines (narrow console)."""
    buf: list[str] = []
    carry = ""
    for raw in stdout.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("BENCH_") or line.startswith("exit="):
            if carry:
                buf.append(carry)
                carry = ""
            if line.startswith("BENCH_") and not re.search(
                r"(ok=[01]|fail=\d|avg_\w+_ms=)", line
            ):
                carry = line
                continue
            buf.append(line)
        elif carry:
            carry += line
        else:
            buf.append(line)
    if carry:
        buf.append(carry)
    return "\n".join(buf)


def _strip_num(val: str) -> str:
    return re.sub(r"\s+", "", val.strip())


def parse_rows(stdout: str) -> list[dict]:
    rows = []
    for line in _norm_stdout(stdout).splitlines():
        if not line.startswith("BENCH_ROW,"):
            continue
        row: dict[str, str] = {}
        for part in line.split(",")[1:]:
            if "=" in part:
                k, v = part.split("=", 1)
                row[k.strip()] = _strip_num(v) if k.endswith("_ms") else v.strip()
        rows.append(row)
    return rows


def parse_summary(stdout: str) -> dict[str, str]:
    for line in _norm_stdout(stdout).splitlines():
        if line.startswith("BENCH_SUMMARY,"):
            out: dict[str, str] = {}
            for part in line.split(",")[1:]:
                if "=" in part:
                    k, v = part.split("=", 1)
                    v = v.strip()
                    if k.endswith("_ms") or k in ("iters", "pass", "fail"):
                        v = _strip_num(v)
                    out[k.strip()] = v
            return out
    return {}


def main(json_path: Path | None = None) -> int:
    src = json_path or JSON_DEFAULT
    if not src.is_file():
        print(f"ERROR: JSON not found: {src}")
        return 1

    data = json.loads(src.read_text(encoding="utf-8"))
    stdout = data.get("stdout", "")
    summary = parse_summary(stdout)
    rows = parse_rows(stdout)
    mode = str(data.get("mode", "odbc")).lower()
    backend_label = BACKEND_LABELS.get(mode, mode)
    bullets = METHODOLOGY_BULLETS.get(mode, METHODOLOGY_BULLETS.get("odbc", []))
    out_name = OUT_BY_MODE.get(mode, OUT_BY_MODE["odbc"])
    out_repo = BENCH_DIR / out_name
    doc_global = _doc_global_dir()
    out_global = (doc_global / out_name) if doc_global else None

    if summary.get("fail", "0") != "0" or int(summary.get("pass", "0")) == 0:
        print("ERROR: bench failed - PDF will not be published")
        return 1

    pdf = BenchPDF()
    pdf.alias_nb_pages()
    pdf.set_auto_page_break(auto=True, margin=14)
    pdf.add_page()

    pdf.set_font("Helvetica", "B", 15)
    pdf.cell(0, 9, "Stress / latency - NAV Harbour (ADO bridge)", new_x="LMARGIN", new_y="NEXT")
    pdf.set_font("Helvetica", "", 10)
    pdf.cell(0, 6, clean(f"Date: {data.get('timestamp', datetime.now().isoformat())}"), new_x="LMARGIN", new_y="NEXT")
    pdf.cell(0, 6, clean(f"Branch: {data.get('branch', 'main')}"), new_x="LMARGIN", new_y="NEXT")
    pdf.cell(0, 6, clean(f"Backend: {backend_label}"), new_x="LMARGIN", new_y="NEXT")
    pdf.cell(0, 6, clean(f"Iterations: {summary.get('iters', '?')} (warmup {data.get('warmup', '1')})"), new_x="LMARGIN", new_y="NEXT")
    pdf.ln(3)

    pdf.set_font("Helvetica", "B", 12)
    pdf.cell(0, 8, "Summary (ms)", new_x="LMARGIN", new_y="NEXT")
    pdf.set_font("Helvetica", "", 10)
    metrics = [
        ("Mean total latency", summary.get("avg_total_ms", "-")),
        ("p50 total", summary.get("p50_total_ms", "-")),
        ("p95 total", summary.get("p95_total_ms", "-")),
        ("min / max total", f"{summary.get('min_total_ms', '-')} / {summary.get('max_total_ms', '-')}"),
        ("Mean connect", summary.get("avg_connect_ms", "-")),
        ("Mean nav (open+skip)", summary.get("avg_nav_ms", "-")),
        ("Pass / fail", f"{summary.get('pass', '-')} / {summary.get('fail', '-')}"),
    ]
    for label, val in metrics:
        pdf.cell(55, 6, clean(label + ":"))
        pdf.cell(0, 6, clean(str(val)), new_x="LMARGIN", new_y="NEXT")
    pdf.ln(2)

    pdf.set_font("Helvetica", "B", 11)
    pdf.cell(0, 7, "Methodology (summary)", new_x="LMARGIN", new_y="NEXT")
    pdf.set_font("Helvetica", "", 9)
    for b in bullets:
        pdf.multi_cell(pdf.epw, 5, clean("- " + b))
    pdf.ln(2)

    if rows:
        pdf.set_font("Helvetica", "B", 11)
        pdf.cell(0, 7, "Per-iteration sample (first 15)", new_x="LMARGIN", new_y="NEXT")
        pdf.set_font("Helvetica", "B", 8)
        cols = ("iter", "connect", "nav", "total", "ok")
        widths = (12, 28, 28, 28, 12)
        for i, h in enumerate(cols):
            pdf.cell(widths[i], 6, h, border=1)
        pdf.ln()
        pdf.set_font("Helvetica", "", 8)
        for row in rows[:15]:
            vals = (
                row.get("iter", ""),
                row.get("connect_ms", ""),
                row.get("nav_ms", ""),
                row.get("total_ms", ""),
                row.get("ok", ""),
            )
            for i, v in enumerate(vals):
                pdf.cell(widths[i], 5, v[:12], border=1)
            pdf.ln()

    out_repo.parent.mkdir(parents=True, exist_ok=True)
    pdf.output(str(out_repo))
    print(f"PDF repo: {out_repo}")
    if out_global is not None:
        out_global.parent.mkdir(parents=True, exist_ok=True)
        out_global.write_bytes(out_repo.read_bytes())
        print(f"PDF global: {out_global}")
    return 0


if __name__ == "__main__":
    import sys

    path = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    raise SystemExit(main(path))