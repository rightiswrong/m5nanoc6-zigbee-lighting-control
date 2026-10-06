"""Draw the integration's brand icon: a jack-o'-lantern whose face is a string of
chained LEDs. Renders at 4x and downsamples for smooth edges.

    python3 tools/make_brand.py
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

OUT = Path(__file__).resolve().parent.parent / "custom_components" / "spooky_lights" / "brand"


def draw(size: int) -> Image.Image:
    s = size * 4
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx, cy = s / 2, s * 0.56

    # Pumpkin body: five overlapping lobes, darker at the seams
    lobes = [
        (-0.30, 0.20, "#b84a00"),
        (0.30, 0.20, "#b84a00"),
        (-0.16, 0.25, "#e2680c"),
        (0.16, 0.25, "#e2680c"),
        (0.0, 0.27, "#f7841c"),
    ]
    for dx, rx, col in lobes:
        x = cx + dx * s
        d.ellipse([x - rx * s, cy - 0.36 * s, x + rx * s, cy + 0.36 * s], fill=col)

    # Stem
    d.rounded_rectangle([cx - 0.04 * s, cy - 0.47 * s, cx + 0.05 * s, cy - 0.31 * s], radius=0.02 * s, fill="#4d6b1f")

    # Glow layer for the face
    glow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    g = ImageDraw.Draw(glow)
    eye_y = cy - 0.08 * s
    for ex in (-0.14, 0.14):
        x = cx + ex * s
        g.polygon(
            [(x - 0.08 * s, eye_y + 0.05 * s), (x + 0.08 * s, eye_y + 0.05 * s), (x, eye_y - 0.08 * s)], fill="#ffe28a"
        )
    # Mouth = a chain of glowing LED "beads" joined by a wire: the Grove chain
    mouth_y = cy + 0.13 * s
    beads = [(-0.20, 0.00), (-0.10, 0.05), (0.0, 0.065), (0.10, 0.05), (0.20, 0.00)]
    pts = [(cx + bx * s, mouth_y + by * s) for bx, by in beads]
    g.line(pts, fill="#ffd166", width=int(0.025 * s), joint="curve")
    for i, (x, y) in enumerate(pts):
        r = 0.045 * s
        col = ["#fff1b8", "#ffe28a", "#b6ff7a", "#ffe28a", "#fff1b8"][i]
        g.rounded_rectangle([x - r, y - r, x + r, y + r], radius=0.015 * s, fill=col)

    halo = glow.filter(ImageFilter.GaussianBlur(0.035 * s))
    img = Image.alpha_composite(img, halo)
    img = Image.alpha_composite(img, halo)
    img = Image.alpha_composite(img, glow)
    return img.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    draw(256).save(OUT / "icon.png", optimize=True)
    draw(512).save(OUT / "icon@2x.png", optimize=True)
    print("wrote", *sorted(p.name for p in OUT.iterdir()))
