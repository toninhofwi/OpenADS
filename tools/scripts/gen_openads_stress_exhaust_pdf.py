#!/usr/bin/env python3
"""PDF report for openads_stress_exhaust_latest.json."""
from __future__ import annotations

import json
import sys
from datetime import datetime
from pathlib import Path

from fpdf import FPDF

SCRIPT = Path(__file__).resolve()
ADO_ROOT = SCRIPT.parents[2]
OUT_PDF = ADO_ROOT / "tools" / "bench" / "results" / "OPENADS_STRESS_EXHAUST_FWH.pdf"


def clean(text: str) -> str:
    return text.encode("latin-1", "replace").decode("latin-1")


class ReportPDF(FPDF):
    def header(self):
        self.set_font("Helvetica", "B", 10)
        self.cell(0, 7, "OpenADS exhaustive stress - local + server", align="C", new_x="LMARGIN", new_y="NEXT")
        self.ln(1)


def main() -> int:
    json_path = Path(sys.argv[1]) if len(sys.argv) > 1 else ADO_ROOT / "tools/bench/results/openads_stress_exhaust_latest.json"
    if not json_path.is_file():
        print(f"JSON not found: {json_path}", file=sys.stderr)
        return 1

    data = json.loads(json_path.read_text(encoding="utf-8"))
    modes = data.get("modes") or []

    pdf = ReportPDF()
    pdf.set_auto_page_break(auto=True, margin=14)
    pdf.add_page()
    pdf.set_font("Helvetica", "", 10)

    pdf.set_font("Helvetica", "B", 12)
    pdf.cell(0, 8, "Summary", new_x="LMARGIN", new_y="NEXT")
    pdf.set_font("Helvetica", "", 10)
    for line in [
        f"Timestamp: {data.get('timestamp', '')}",
        f"Branch: {data.get('branch', '')}",
        f"Profile: {data.get('profile', '')}",
        f"Result: {'PASS' if data.get('pass') else 'FAIL'}",
        f"OpenADS DLL: {data.get('open_dll', '')}",
        f"Server skip: {data.get('server_skip') or '-'}",
    ]:
        pdf.set_x(pdf.l_margin)
        pdf.multi_cell(0, 5, clean(str(line)))

    pdf.ln(3)
    pdf.set_font("Helvetica", "B", 11)
    pdf.cell(0, 7, "Harbour modes (adsexhaust)", new_x="LMARGIN", new_y="NEXT")
    pdf.set_font("Helvetica", "B", 9)
    cw = (pdf.epw - 6) / 5
    for h in ("Profile", "Mode", "Arg", "Exit", "ms"):
        pdf.cell(cw, 6, h, border=1)
    pdf.ln()
    pdf.set_font("Helvetica", "", 8)
    for row in modes:
        pdf.cell(cw, 6, clean(str(row.get("profile", ""))), border=1)
        pdf.cell(cw, 6, clean(str(row.get("mode", ""))), border=1)
        pdf.cell(cw, 6, clean(str(row.get("arg", ""))), border=1)
        pdf.cell(cw, 6, clean(str(row.get("exit", ""))), border=1)
        pdf.cell(cw, 6, clean(str(row.get("wall_ms", ""))), border=1)
        pdf.ln()

    pdf.ln(4)
    pdf.set_font("Helvetica", "B", 11)
    pdf.cell(0, 7, "Matrix", new_x="LMARGIN", new_y="NEXT")
    pdf.set_font("Helvetica", "", 9)
    bullets = [
        "Local: openace64.dll in-process (ADS_LOCAL_SERVER).",
        "Server: openads_serverd + tcp:// URI (ADS_REMOTE_SERVER).",
        "Modes: init, read, write, lock, stress, dbf, tx, rel, pr (+ trylock local).",
        "Optional: C++ openads_stress, NAV backends, SAP vs OpenADS engine bench.",
        "Doc: docs/OPENADS_STRESS_EXHAUST.md",
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