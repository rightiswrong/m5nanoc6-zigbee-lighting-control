"""Matter onboarding codes: the "MT:..." QR payload and the 11-digit manual code.

Pure standard library, so it runs anywhere. Used by tools/matter_label.py and the
tests; it follows the Matter Core Specification §5.1 (Onboarding Payload):

  QR payload (88 bits, little-endian bit packing, then Base38):
      version:3  vendor_id:16  product_id:16  custom_flow:2
      discovery:8  discriminator:12  passcode:27  padding:4
  Manual code (11 digits, standard flow):
      chunk1 (1 digit)  = discriminator[11:10]          (vid_pid_present = 0)
      chunk2 (5 digits) = discriminator[9:8] << 14 | passcode[13:0]
      chunk3 (4 digits) = passcode[26:14]
      + Verhoeff check digit
"""

from __future__ import annotations

from dataclasses import dataclass

BASE38 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-."

DISCOVERY_SOFTAP = 0x01
DISCOVERY_BLE = 0x02
DISCOVERY_ON_NETWORK = 0x04

# Passcodes the spec forbids (trivially guessable).
INVALID_PASSCODES = frozenset(
    {0, 11111111, 22222222, 33333333, 44444444, 55555555, 66666666, 77777777, 88888888, 99999999, 12345678, 87654321}
)


@dataclass(frozen=True)
class SetupPayload:
    vendor_id: int
    product_id: int
    discriminator: int  # 12 bits
    passcode: int  # 27 bits, 1..99999998
    discovery: int = DISCOVERY_BLE
    custom_flow: int = 0
    version: int = 0


def is_valid_passcode(passcode: int) -> bool:
    return 1 <= passcode <= 99999998 and passcode not in INVALID_PASSCODES


# ------------------------------------------------------------------ Base38
def base38_encode(data: bytes) -> str:
    out = []
    i = 0
    while i < len(data):
        chunk = data[i : i + 3]
        value = int.from_bytes(chunk, "little")
        chars = {3: 5, 2: 4, 1: 2}[len(chunk)]
        for _ in range(chars):
            value, rem = divmod(value, 38)
            out.append(BASE38[rem])
        i += 3
    return "".join(out)


def base38_decode(text: str) -> bytes:
    out = bytearray()
    i = 0
    while i < len(text):
        chunk = text[i : i + 5]
        nbytes = {5: 3, 4: 2, 2: 1}[len(chunk)]
        value = 0
        for ch in reversed(chunk):
            value = value * 38 + BASE38.index(ch)
        out += value.to_bytes(nbytes, "little")
        i += 5
    return bytes(out)


# ------------------------------------------------------------------ QR payload
_FIELDS = (("version", 3), ("vendor_id", 16), ("product_id", 16), ("custom_flow", 2),
           ("discovery", 8), ("discriminator", 12), ("passcode", 27))  # fmt: skip


def encode_qr(p: SetupPayload) -> str:
    bits = 0
    pos = 0
    for name, width in _FIELDS:
        value = getattr(p, name)
        if not 0 <= value < (1 << width):
            raise ValueError(f"{name}={value} does not fit in {width} bits")
        bits |= value << pos
        pos += width
    pos += 4  # padding
    return "MT:" + base38_encode(bits.to_bytes(pos // 8, "little"))


def decode_qr(payload: str) -> SetupPayload:
    if not payload.startswith("MT:"):
        raise ValueError("Matter QR payloads start with 'MT:'")
    raw = base38_decode(payload[3:])
    bits = int.from_bytes(raw[:11], "little")
    values = {}
    pos = 0
    for name, width in _FIELDS:
        values[name] = (bits >> pos) & ((1 << width) - 1)
        pos += width
    return SetupPayload(**values)


# ------------------------------------------------------------------ manual code
_D = [[0, 1, 2, 3, 4, 5, 6, 7, 8, 9], [1, 2, 3, 4, 0, 6, 7, 8, 9, 5], [2, 3, 4, 0, 1, 7, 8, 9, 5, 6],
      [3, 4, 0, 1, 2, 8, 9, 5, 6, 7], [4, 0, 1, 2, 3, 9, 5, 6, 7, 8], [5, 9, 8, 7, 6, 0, 4, 3, 2, 1],
      [6, 5, 9, 8, 7, 1, 0, 4, 3, 2], [7, 6, 5, 9, 8, 2, 1, 0, 4, 3], [8, 7, 6, 5, 9, 3, 2, 1, 0, 4],
      [9, 8, 7, 6, 5, 4, 3, 2, 1, 0]]  # fmt: skip
_P = [[0, 1, 2, 3, 4, 5, 6, 7, 8, 9], [1, 5, 7, 6, 2, 8, 3, 0, 9, 4], [5, 8, 0, 3, 7, 9, 6, 1, 4, 2],
      [8, 9, 1, 6, 0, 4, 3, 5, 2, 7], [9, 4, 5, 3, 1, 2, 6, 8, 7, 0], [4, 2, 8, 6, 5, 7, 3, 9, 0, 1],
      [2, 7, 9, 3, 8, 0, 6, 4, 1, 5], [7, 0, 4, 6, 9, 1, 3, 2, 5, 8]]  # fmt: skip
_INV = [0, 4, 3, 2, 1, 5, 6, 7, 8, 9]


def verhoeff_check_digit(digits: str) -> str:
    c = 0
    for i, ch in enumerate(reversed(digits)):
        c = _D[c][_P[(i + 1) % 8][int(ch)]]
    return str(_INV[c])


def encode_manual(p: SetupPayload) -> str:
    d = p.discriminator >> 8  # manual codes carry only the top 4 discriminator bits
    chunk1 = (d >> 2) & 0x3
    chunk2 = ((d & 0x3) << 14) | (p.passcode & 0x3FFF)
    chunk3 = p.passcode >> 14
    digits = f"{chunk1}{chunk2:05d}{chunk3:04d}"
    return digits + verhoeff_check_digit(digits)


def format_manual(code: str) -> str:
    """'34970112332' -> '3497-011-2332' (how most apps print it)."""
    return f"{code[:4]}-{code[4:7]}-{code[7:]}" if len(code) == 11 else code


if __name__ == "__main__":
    import argparse

    ap = argparse.ArgumentParser(description="Encode or decode Matter onboarding codes")
    ap.add_argument("payload", nargs="?", help="decode this MT:... payload")
    ap.add_argument("--vid", type=lambda s: int(s, 0), default=0xFFF1)
    ap.add_argument("--pid", type=lambda s: int(s, 0), default=0x8000)
    ap.add_argument("--discriminator", type=lambda s: int(s, 0))
    ap.add_argument("--passcode", type=int)
    a = ap.parse_args()
    if a.payload:
        p = decode_qr(a.payload)
        print(p, "\nmanual code:", format_manual(encode_manual(p)))
    else:
        p = SetupPayload(a.vid, a.pid, a.discriminator, a.passcode)
        print(encode_qr(p), format_manual(encode_manual(p)))
