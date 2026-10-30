"""Draw the app icon (original pixel art) and export every Android size GameMaker asks for.

Usage: py -3 scripts/make_icon.py [--preview]
Writes gm/runner/icons/: legacy icons (ldpi..xxxhdpi), adaptive foreground/background layers,
and build/icon_preview.png (a big preview with the round and squircle masks Android uses).
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "gm/runner/icons"

G = 32  # design grid (pixels)

# UFO 50's palette (read from the game's textures); every icon pixel is snapped to it
PALETTE = [tuple(int(h[i:i + 2], 16) for i in (1, 3, 5)) for h in (
    "#0B0606", "#4C0000", "#9A0B0B", "#991515", "#A48080", "#E03C32", "#CA4A00", "#8A6042", "#FE7000", "#FEB854", "#A3A322", "#E8EA4A", "#A4F022", "#63B50D", "#63B31D", "#003904", "#003D10", "#58F5B1", "#008456", "#64A4A4", "#27BADB", "#006AB4", "#202040", "#4430BA", "#340058", "#9600DC", "#CC68E4", "#FE48DE", "#870B4F", "#861650", "#783250", "#D10F4C", "#FE626E", "#000000", "#6F6F6F", "#C8C8C8", "#FFFFFF")]


def snap(im):
    """Map every pixel to the nearest palette colour (keeps alpha)."""
    im = im.convert("RGBA")
    cache = {}
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            k = (r, g, b)
            if k not in cache:
                cache[k] = min(PALETTE, key=lambda p: (p[0] - r) ** 2 * 0.3 + (p[1] - g) ** 2 * 0.59 + (p[2] - b) ** 2 * 0.11)
            px[x, y] = cache[k] + (a,)
    return im

DIGITS = {
    "5": ["#######", "#######", "##.....", "######.", "#######", ".....##", ".....##", "##...##", "#######", ".#####."],
    "0": [".#####.", "#######", "##...##", "##...##", "##...##", "##...##", "##...##", "##...##", "#######", ".#####."],
}


def sky(n, s=1.0, hills=0.80):
    """Night sky with a little moon over two green hills, n x n pixels (s = scale of a 32 grid)."""
    im = Image.new("RGB", (n, n))
    bands = [(36, 10, 88), (52, 22, 124), (72, 40, 160), (92, 58, 190)]
    for y in range(n):
        im.paste(bands[min(3, y * 4 // n)], (0, y, n, y + 1))
    d = ImageDraw.Draw(im)
    mx, my = 5 * s + (n - 32 * s) / 2, 5 * s + (n - 32 * s) / 2   # small moon, top left
    d.ellipse((mx - 2.5 * s, my - 2.5 * s, mx + 2.5 * s, my + 2.5 * s), fill=(150, 70, 225))
    d.ellipse((mx - 1.5 * s, my - 2.5 * s, mx + 2 * s, my), fill=(176, 104, 240))
    d.rectangle((0, n * (hills + 0.2), n, n), fill=(0, 61, 16))   # ground under both hills
    d.ellipse((-n * 0.25, n * hills, n * 0.70, n * (hills + 0.65)), fill=(0, 61, 16))
    d.ellipse((n * 0.40, n * (hills + 0.03), n * 1.30, n * (hills + 0.65)), fill=(0, 132, 86))
    for x, y, c in ((27, 3, (255, 255, 230)), (9, 1, (255, 226, 140)), (2, 13, (200, 200, 255)), (29, 13, (255, 255, 230)),
                    (16, 1, (200, 200, 255)), (23, 6, (255, 226, 140))):
        im.putpixel((int(x * s), int(y * s)), c)
    return im


def art(n, ox, oy):
    """UFO beaming up a bold "50", on an n x n transparent layer, design origin (ox, oy)."""
    lay = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    # tractor beam
    d.polygon([(ox + 11, oy + 13), (ox + 20, oy + 13), (ox + 27, oy + 27), (ox + 4, oy + 27)], fill=(100, 164, 164, 255))
    d.polygon([(ox + 13, oy + 13), (ox + 18, oy + 13), (ox + 22, oy + 27), (ox + 9, oy + 27)], fill=(88, 245, 177, 255))
    # saucer: build the shape on a mask, colour it, then outline it
    shape = Image.new("L", (n, n), 0)
    sd = ImageDraw.Draw(shape)
    sd.ellipse((ox + 11, oy + 2, ox + 20, oy + 11), fill=1)      # dome
    sd.ellipse((ox + 3, oy + 7, ox + 28, oy + 14), fill=2)       # body
    body = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    for y in range(n):
        for x in range(n):
            v = shape.getpixel((x, y))
            if v == 1:
                body.putpixel((x, y), (96, 222, 238, 255))
            elif v == 2:
                body.putpixel((x, y), (122, 116, 158, 255) if y >= oy + 12 else (210, 208, 226, 255))
    for i, x in enumerate(range(ox + 6, ox + 27, 3)):              # rim lights
        body.putpixel((x, oy + 11), (238, 72, 60, 255) if i % 2 == 0 else (255, 214, 72, 255))
    for x, y in ((ox + 14, oy + 4), (ox + 13, oy + 5), (ox + 13, oy + 6)):   # dome glint
        body.putpixel((x, y), (255, 255, 255, 255))
    for y in range(n):
        for x in range(n):
            if shape.getpixel((x, y)) == 0 and any(0 <= x + dx < n and 0 <= y + dy < n and shape.getpixel((x + dx, y + dy))
                                                  for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                body.putpixel((x, y), (18, 8, 36, 255))
    lay.alpha_composite(body)
    # "50": white with a dark drop shadow
    pix = [(ox + 8 + i * 9 + x, oy + 15 + y) for i, ch in enumerate("50")
           for y, row in enumerate(DIGITS[ch]) for x, c in enumerate(row) if c == "#"]
    for x, y in pix:
        lay.putpixel((x + 1, y + 1), (40, 14, 82, 255))
    for x, y in pix:
        lay.putpixel((x, y), (255, 255, 255, 255))
    return lay


def legacy_icon():
    im = sky(G).convert("RGBA")
    im.alpha_composite(art(G, 0, 0))
    return im


def adaptive_layers():
    """108dp adaptive icon on a 48-px grid; the 32-px design sits in the middle (inside the safe zone)."""
    return sky(48, 1.5, hills=0.70), art(48, 8, 8)


def up(im, size):
    return im.resize((size, size), Image.NEAREST)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    icon = snap(legacy_icon())
    sizes = {"ldpi": 36, "mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
    for name, px in sizes.items():
        big = up(icon, G * -(-px // G))  # integer scale up, then down to size (stays crisp)
        big.resize((px, px), Image.LANCZOS if px % G else Image.NEAREST).save(OUT / f"icon_{name}.png")
    bg, fg = adaptive_layers()
    bg, fg = snap(bg).convert("RGB"), snap(fg)
    asizes = {"ldpi": 81, "mdpi": 108, "hdpi": 162, "xhdpi": 216, "xxhdpi": 324, "xxxhdpi": 432}
    for name, px in asizes.items():
        for layer, tag in ((bg, "bg"), (fg, "fg")):
            big = up(layer, 48 * -(-px // 48))
            big.resize((px, px), Image.LANCZOS if px % 48 else Image.NEAREST).save(OUT / f"adaptive_{tag}_{name}.png")

    # preview: legacy icon, adaptive in a circle mask and in a squircle mask, on a launcher-ish backdrop
    pv = Image.new("RGB", (1500, 560), (34, 36, 48))
    pv.paste(up(icon, 384), (60, 88))
    combo = bg.convert("RGBA"); combo.alpha_composite(fg); combo = up(combo, 432)
    for i, shape in enumerate(("circle", "squircle")):
        mask = Image.new("L", (432, 432), 0)
        md = ImageDraw.Draw(mask)
        inset = 432 * 18 // 108  # launchers show the middle 72dp of the 108dp layer
        box = (inset, inset, 432 - inset, 432 - inset)
        if shape == "circle":
            md.ellipse(box, fill=255)
        else:
            md.rounded_rectangle(box, radius=90, fill=255)
        crop = combo.crop(box).resize((384, 384), Image.NEAREST)
        m = mask.crop(box).resize((384, 384), Image.LANCZOS)
        pv.paste(crop, (560 + i * 460, 88), m)
    pv.save(ROOT / "build/icon_preview.png")
    print("icons written to", OUT)


if __name__ == "__main__":
    main()
