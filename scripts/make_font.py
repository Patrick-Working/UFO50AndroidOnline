"""Pack exported font frames (scripts/export_font.csx) into 95-glyph strips for the overlay.

Usage: py -3 scripts/make_font.py <font_raw_dir> <sprite> <out.png> [--preview text]
The strip is 95 cells wide (ASCII 32-126), one cell per glyph, white glyphs on transparent;
the shadow (if any) keeps its own colour.
"""
import sys
from pathlib import Path
from PIL import Image

src, sprite, out = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
cells = [Image.open(src / sprite / f"{i}.png").convert("RGBA") for i in range(95)]
w, h = cells[0].size
strip = Image.new("RGBA", (w * 95, h))
for i, c in enumerate(cells):
    strip.paste(c, (i * w, 0))
out.parent.mkdir(parents=True, exist_ok=True)
strip.save(out)
print(f"{out}: {w}x{h} cells")
if "--preview" in sys.argv:
    text = sys.argv[sys.argv.index("--preview") + 1]
    pv = Image.new("RGBA", (w * len(text), h), (40, 40, 60, 255))
    for i, ch in enumerate(text):
        k = ord(ch) - 32
        if 0 <= k < 95:
            pv.alpha_composite(cells[k], (i * w, 0))
    pv.resize((pv.width * 6, pv.height * 6), Image.NEAREST).save(out.with_name(out.stem + "_preview.png"))
