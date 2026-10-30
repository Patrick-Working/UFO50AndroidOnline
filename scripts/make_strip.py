"""Pack a sprite's exported frames (scripts/export_sprites.csx) into one horizontal strip.

Usage: py -3 scripts/make_strip.py <frames_dir> <out.png>   (frames named 0.png, 1.png, ...)
"""
import sys
from pathlib import Path
from PIL import Image

src, out = Path(sys.argv[1]), Path(sys.argv[2])
frames = sorted(src.glob("*.png"), key=lambda p: int(p.stem))
cells = [Image.open(f).convert("RGBA") for f in frames]
w, h = cells[0].size
strip = Image.new("RGBA", (w * len(cells), h))
for i, c in enumerate(cells):
    strip.paste(c, (i * w, 0))
strip.save(out)
print(f"{out}: {len(cells)} cells of {w}x{h}")
