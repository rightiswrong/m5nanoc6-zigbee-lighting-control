"""Make printable Matter pairing labels for Spooky Lights devices (SVG, no dependencies).

Each light makes its own random setup code on first boot and prints it on the
serial monitor. Save that output (or copy the "QR payload" line) and turn it into
a sticker to put on the device, its box, or the inside of the pumpkin:

    python3 tools/matter_label.py --log serial.txt                  # → spooky-label.svg
    python3 tools/matter_label.py --payload MT:6FCJ1MBW17KGJG03P10 --name "Porch"
    python3 tools/matter_label.py --log a.txt --log b.txt -o sheet.svg   # several on one sheet

Open the SVG in a browser and print at 100 % scale. Labels are 50 × 30 mm.
The manual pairing code is recomputed from the payload, so they can't disagree.
"""

from __future__ import annotations

import argparse
import html
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import matter_codes  # noqa: E402
from vendor import qrcodegen  # noqa: E402

LABEL_W, LABEL_H = 50.0, 30.0  # mm
GAP = 4.0  # mm between labels on a sheet


@dataclass
class Label:
    payload: str
    manual: str
    board: str
    name: str


def parse_log(text: str) -> Label:
    payload = re.search(r"QR payload\s*:\s*(MT:[0-9A-Z.\-]+)", text)
    if not payload:
        url = re.search(r"data=MT%3A([0-9A-Z.\-]+)", text)
        if not url:
            raise SystemExit("No 'QR payload: MT:...' line found — is this the Matter firmware's serial output?")
        code = "MT:" + url.group(1)
    else:
        code = payload.group(1)
    board = re.search(r"(Nano[A-Z0-9]+) Spooky Lights", text)
    return make_label(code, board.group(1) if board else "", "")


def make_label(payload: str, board: str, name: str) -> Label:
    p = matter_codes.decode_qr(payload)  # validates the payload
    if not matter_codes.is_valid_passcode(p.passcode):
        raise SystemExit(f"{payload}: passcode {p.passcode} is not a valid Matter passcode")
    manual = matter_codes.format_manual(matter_codes.encode_manual(p))
    return Label(payload, manual, board, name)


def qr_modules(payload: str) -> list[list[bool]]:
    qr = qrcodegen.QrCode.encode_text(payload, qrcodegen.QrCode.Ecc.MEDIUM)
    n = qr.get_size()
    return [[qr.get_module(x, y) for x in range(n)] for y in range(n)]


def label_svg(lb: Label, x0: float, y0: float) -> str:
    mods = qr_modules(lb.payload)
    n = len(mods)
    quiet = 2
    qr_mm = 25.0
    cell = qr_mm / (n + 2 * quiet)
    qx, qy = x0 + 2.0, y0 + (LABEL_H - qr_mm) / 2
    path = []
    for y, row in enumerate(mods):
        x = 0
        while x < n:  # merge horizontal runs → small file, crisp print
            if row[x]:
                start = x
                while x < n and row[x]:
                    x += 1
                path.append(f"M{qx + (start + quiet) * cell:.3f} {qy + (y + quiet) * cell:.3f}"
                            f"h{(x - start) * cell:.3f}v{cell:.3f}h{-(x - start) * cell:.3f}z")  # fmt: skip
            else:
                x += 1
    tx = qx + qr_mm + 1.5
    title = html.escape(lb.name or "Spooky Lights")
    sub = html.escape(f"{lb.board} · Matter" if lb.board else "Matter")
    return f"""
  <g>
    <rect x="{x0:.2f}" y="{y0:.2f}" width="{LABEL_W}" height="{LABEL_H}" rx="2.5" fill="#fff" stroke="#bbb" stroke-width="0.2"/>
    <rect x="{qx:.3f}" y="{qy:.3f}" width="{qr_mm}" height="{qr_mm}" fill="#fff"/>
    <path d="{"".join(path)}" fill="#000"/>
    <circle cx="{tx + 1.6:.2f}" cy="{y0 + 6.2:.2f}" r="1.6" fill="#f7841c"/>
    <rect x="{tx + 1.35:.2f}" y="{y0 + 4.0:.2f}" width="0.5" height="0.9" fill="#4d6b1f"/>
    <text x="{tx + 4.0:.2f}" y="{y0 + 7.0:.2f}" font-family="Helvetica, Arial, sans-serif" font-size="2.2"
          font-weight="bold" fill="#222" textLength="{min(17.0, 1.25 * len(title)):.1f}" lengthAdjust="spacingAndGlyphs">{title}</text>
    <text x="{tx:.2f}" y="{y0 + 11.0:.2f}" font-family="Helvetica, Arial, sans-serif" font-size="2.0" fill="#555">{sub}</text>
    <text x="{tx:.2f}" y="{y0 + 18.5:.2f}" font-family="Helvetica, Arial, sans-serif" font-size="1.8" fill="#555">Manual code</text>
    <text x="{tx:.2f}" y="{y0 + 22.2:.2f}" font-family="Menlo, Consolas, monospace" font-size="2.5" font-weight="bold"
          fill="#000">{lb.manual}</text>
    <text x="{tx:.2f}" y="{y0 + 26.6:.2f}" font-family="Helvetica, Arial, sans-serif" font-size="1.5" fill="#888">Scan in Home Assistant</text>
  </g>"""


def sheet_svg(labels: list[Label], per_row: int = 3) -> str:
    cols = min(per_row, len(labels))
    rows = (len(labels) + per_row - 1) // per_row
    w = cols * LABEL_W + (cols + 1) * GAP
    h = rows * LABEL_H + (rows + 1) * GAP
    body = "".join(
        label_svg(lb, GAP + (i % per_row) * (LABEL_W + GAP), GAP + (i // per_row) * (LABEL_H + GAP))
        for i, lb in enumerate(labels)
    )
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}mm" height="{h}mm" viewBox="0 0 {w} {h}">'
        f"<title>Spooky Lights pairing labels</title>{body}\n</svg>\n"
    )


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", action="append", default=[], help="serial monitor output (repeatable)")
    ap.add_argument("--payload", action="append", default=[], help="MT:... payload (repeatable)")
    ap.add_argument("--name", default="", help="title on the label, e.g. 'Porch pumpkin'")
    ap.add_argument("--board", default="", help="board name for --payload labels (NanoC6 / NanoH2)")
    ap.add_argument("-o", "--output", default="spooky-label.svg")
    a = ap.parse_args()

    labels = [parse_log(Path(p).read_text(encoding="utf-8", errors="replace")) for p in a.log]
    labels += [make_label(p, a.board, "") for p in a.payload]
    if not labels:
        ap.error("give --log and/or --payload")
    if a.name:
        for lb in labels:
            lb.name = a.name
    Path(a.output).write_text(sheet_svg(labels), encoding="utf-8")
    for lb in labels:
        print(f"{lb.payload}  manual {lb.manual}")
    print(f"wrote {a.output} ({len(labels)} label{'s' if len(labels) != 1 else ''}) — print at 100 %")


if __name__ == "__main__":
    main()
