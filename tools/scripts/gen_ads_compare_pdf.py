#!/usr/bin/env python3
"""Build ADS vs OpenADS engine comparison PDF from ads_vs_openads_latest.json."""
from __future__ import annotations

import json
import sys
from datetime import datetime
from pathlib import Path

from fpdf import FPDF

SCRIPT = Path(__file__).resolve()
ADO_ROOT = SCRIPT.parents[2]
BENCH_DIR = ADO_ROOT / "tools" / "bench" / "results"
JSON_DEFAULT = BENCH_DIR / "ads_vs_openads_latest.json"
OUT_PDF = BENCH_DIR / "OPENADS_ADS_VS_OPENADS_FWH.pdf"


def clean(text: str) -> str:
    text = text.replace("\u2014", "-").replace("\u2013", "-")
    return text.encode("latin-1", "replace").decode("latin-1")


class ComparePDF(FPDF):
    def header(self):
        self.set_font("Helvetica", "B", 10)
        self.cell(
            0,
            7,
            "OpenADS engine bench - SAP ADS vs OpenADS (local DBF/CDX)",
            align="C",
            new_x="LMARGIN",
            new_y="NEXT",
        )
        self.ln(1)

    def footer(self):
        self.set_y(-12)
        self.set_font("Helvetica", "I", 8)
        self.cell(0, 8, f"Page {self.page_no()}", align="C")


def ratio_label(ratio: float | None) -> str:
    if ratio is None:
        return "n/a"
    if ratio <= 1.05:
        return f"{ratio:.2f}x (on par)"
    if ratio <= 1.5:
        return f"{ratio:.2f}x (acceptable)"
    return f"{ratio:.2f}x (review)"


def main() -> int:
    json_path = Path(sys.argv[1]) if len(sys.argv) > 1 else JSON_DEFAULT
    if not json_path.is_file():
        print(f"JSON not found: {json_path}", file=sys.stderr)
        return 1

    data = json.loads(json_path.read_text(encoding="utf-8"))
    ts = data.get("timestamp") or datetime.now().isoformat(timespec="seconds")
    rows_n = data.get("rows", "?")
    repeats = data.get("repeats", "?")
    branch = data.get("branch", "unknown")
    passed = data.get("pass", False)
    comparison = data.get("comparison") or []

    pdf = ComparePDF()
    pdf.set_auto_page_break(auto=True, margin=14)
    pdf.add_page()
    pdf.set_font("Helvetica", "", 10)

    pdf.set_font("Helvetica", "B", 12)
    pdf.cell(0, 8, "Summary", new_x="LMARGIN", new_y="NEXT")
    pdf.set_font("Helvetica", "", 10)
    def short_path(path: str, max_len: int = 72) -> str:
        path = str(path or "")
        if len(path) <= max_len:
            return path
        head = path[:28]
        tail = path[-(max_len - len(head) - 3) :]
        return head + "..." + tail

    for line in [
        f"Timestamp: {ts}",
        f"Branch: {branch}",
        f"Rows: {rows_n}  Repeats (median): {repeats}",
        f"RDD workloads: {data.get('rdd', False)}",
        f"Result: {'PASS' if passed else 'FAIL'}",
        f"Data: {short_path(data.get('data_dir', ''))}",
        f"SAP DLL: {short_path(data.get('sap_dll_dir', ''))}",
        f"OpenADS DLL: {short_path(data.get('open_dll_dir', ''))}",
    ]:
        pdf.set_x(pdf.l_margin)
        pdf.multi_cell(0, 5, clean(line))
    pdf.ln(3)

    pdf.set_font("Helvetica", "B", 12)
    pdf.cell(0, 8, "Median latency comparison (ms)", new_x="LMARGIN", new_y="NEXT")
    pdf.set_font("Helvetica", "B", 9)
    col_w = (pdf.epw - 10) / 4
    for hdr in ("Workload", "SAP ADS", "OpenADS", "Ratio"):
        pdf.cell(col_w, 6, hdr, border=1)
    pdf.ln()
    pdf.set_font("Helvetica", "", 9)
    for row in comparison:
        wl = str(row.get("workload", ""))
        sap = row.get("sap_ms")
        oa = row.get("openads_ms")
        ratio = row.get("ratio")
        pdf.cell(col_w, 6, clean(wl), border=1)
        pdf.cell(col_w, 6, clean(f"{sap:.1f}" if sap is not None else "-"), border=1)
        pdf.cell(col_w, 6, clean(f"{oa:.1f}" if oa is not None else "-"), border=1)
        pdf.cell(col_w, 6, clean(ratio_label(ratio if ratio is not None else None)), border=1)
        pdf.ln()

    pdf.ln(4)
    pdf.set_font("Helvetica", "B", 11)
    pdf.cell(0, 7, "Methodology", new_x="LMARGIN", new_y="NEXT")
    pdf.set_font("Helvetica", "", 9)
    bullets = [
        "Same bench.dbf + bench.cdx; engines swapped via separate Harbour exes.",
        "SAP baseline: Harbour ace64.lib link + ace64.dll runtime (FWH samples or Advantage).",
        "OpenADS: openace64.dll drop-in; SQL-on-DBF via AdsExecuteSQLDirect.",
        "Workloads: sql_count, sql_fetch (default); optional RDD seek/aof/scan with -Rdd.",
        "Full doc: docs/OPENADS_ADS_VS_OPENADS_BENCH.md in the OpenADS repo.",
    ]
    for b in bullets:
        pdf.set_x(pdf.l_margin)
        pdf.multi_cell(0, 5, clean(f"- {b}"))

    OUT_PDF.parent.mkdir(parents=True, exist_ok=True)
    pdf.output(str(OUT_PDF))
    print(f"Wrote {OUT_PDF}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())