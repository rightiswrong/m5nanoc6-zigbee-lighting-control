"""Check the pairing banner the Matter firmware printed (captured by the host test).

1. Rebuild the QR code from the Unicode half blocks on the "serial monitor".
2. Decode it with an independent decoder (jsQR, run under node).
3. Decode the Matter payload and compare passcode / discriminator with what the
   firmware stored, and the printed manual code with one computed here.

Usage: check_qr.py LOG [--jsqr PATH/TO/jsQR.js] [--light]
Exits 0 on success, 1 on mismatch, 2 when node/jsQR are unavailable (skipped).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import matter_codes  # noqa: E402

GLYPHS = {"█": (True, True), "▀": (True, False), "▄": (False, True), " ": (False, False)}


def modules_from_log(text: str, dark_terminal: bool) -> list[list[bool]]:
    """Return rows of booleans, True = dark module."""
    start = text.index("ready to pair")
    end = text.index("Manual pairing code", start)
    block = [ln[6:] for ln in text[start:end].splitlines() if ln.startswith("      ") and set(ln[6:]) <= set(GLYPHS)]
    block = [ln for ln in block if ln.strip(" ") or len(ln) > 10]
    rows: list[list[bool]] = []
    for ln in block:
        top, bottom = [], []
        for ch in ln:
            t, b = GLYPHS[ch]
            # ink is light in dark-terminal mode, dark otherwise
            top.append(not t if dark_terminal else t)
            bottom.append(not b if dark_terminal else b)
        rows += [top, bottom]
    return rows


def decode_with_jsqr(rows: list[list[bool]], jsqr: str) -> str | None:
    scale, border = 8, 4
    h, w = len(rows), len(rows[0])
    W, H = (w + 2 * border) * scale, (h + 2 * border) * scale
    px = bytearray([255]) * (W * H * 4)
    for y, row in enumerate(rows):
        for x, dark in enumerate(row):
            if not dark:
                continue
            for dy in range(scale):
                base = (((y + border) * scale + dy) * W + (x + border) * scale) * 4
                for dx in range(scale):
                    i = base + dx * 4
                    px[i : i + 3] = b"\x00\x00\x00"
    script = (
        "const jsQR=require(process.argv[1]);const fs=require('fs');"
        "const b=fs.readFileSync(0);const W=+process.argv[2],H=+process.argv[3];"
        "const r=jsQR(new Uint8ClampedArray(b),W,H);"
        "console.log(JSON.stringify(r?r.data:null));"
    )
    out = subprocess.run(["node", "-e", script, jsqr, str(W), str(H)], input=bytes(px), capture_output=True, check=True)
    return json.loads(out.stdout)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--jsqr", default=os.environ.get("JSQR", ""))
    ap.add_argument("--light", action="store_true", help="QR printed for a light-background terminal")
    a = ap.parse_args()
    text = Path(a.log).read_text(encoding="utf-8")

    m = re.search(r"expect passcode=(\d+) discriminator=(\d+)", text)
    want_pin, want_disc = int(m.group(1)), int(m.group(2))
    printed_payload = re.search(r"QR payload\s*:\s*(MT:[0-9A-Z.\-]+)", text).group(1)
    printed_manual = re.search(r"Manual pairing code\s*:\s*([\d-]+)", text).group(1)

    p = matter_codes.decode_qr(printed_payload)
    ok = True
    if (p.passcode, p.discriminator) != (want_pin, want_disc):
        print(f"FAIL payload carries {p.passcode}/{p.discriminator}, firmware stored {want_pin}/{want_disc}")
        ok = False
    if p.discovery != matter_codes.DISCOVERY_BLE:
        print(f"FAIL discovery capabilities {p.discovery:#x}, expected BLE")
        ok = False
    manual = matter_codes.format_manual(matter_codes.encode_manual(p))
    if manual != printed_manual:
        print(f"FAIL manual code printed {printed_manual}, expected {manual}")
        ok = False

    if not a.jsqr or not Path(a.jsqr).exists() or not shutil.which("node"):
        print("SKIP QR image decode (set JSQR=/path/to/jsQR/dist/jsQR.js and install node)")
        print("OK payload/manual code" if ok else "FAILED")
        return 0 if ok else 1

    rows = modules_from_log(text, dark_terminal=not a.light)
    decoded = decode_with_jsqr(rows, a.jsqr)
    if decoded != printed_payload:
        print(f"FAIL QR on the serial log decodes to {decoded!r}, expected {printed_payload!r}")
        ok = False
    else:
        print(f"OK QR ({len(rows[0])}x{len(rows)} modules incl. quiet zone) decodes to {decoded}")
    print(f"OK passcode {want_pin}, discriminator {want_disc}, manual code {manual}" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
