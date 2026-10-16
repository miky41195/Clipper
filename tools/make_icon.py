"""Draws Clipper's app icon (green rounded square with a camera glyph) as a multi-size .ico.

usage: python tools/make_icon.py src/app/clipper.ico
"""
import sys
from PIL import Image, ImageDraw

ACCENT = (61, 220, 132, 255)
DARK = (15, 17, 21, 255)


def draw(size: int) -> Image.Image:
    s = 4  # supersampling
    n = size * s
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pad = n * 0.04
    d.rounded_rectangle([pad, pad, n - pad, n - pad], radius=n * 0.24, fill=ACCENT)
    # camera body
    w = n * 0.07
    bx0, by0, bx1, by1 = n * 0.20, n * 0.32, n * 0.64, n * 0.68
    d.rounded_rectangle([bx0, by0, bx1, by1], radius=n * 0.08, outline=DARK, width=int(w))
    # lens wedge
    d.polygon([(n * 0.66, n * 0.50), (n * 0.82, n * 0.36), (n * 0.82, n * 0.64)], outline=DARK, fill=None, width=int(w))
    d.line([(n * 0.66, n * 0.44), (n * 0.66, n * 0.56)], fill=DARK, width=int(w))
    return img.resize((size, size), Image.LANCZOS)


def main(out: str) -> None:
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    base = draw(256)
    base.save(out, sizes=[(s, s) for s in sizes], append_images=[draw(s) for s in sizes[:-1]])


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "src/app/clipper.ico")
