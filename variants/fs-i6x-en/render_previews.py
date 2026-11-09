#!/usr/bin/env python3
"""Render actual host framebuffers as PNGs; no reconstructed UI or mock drawing."""
from pathlib import Path
from PIL import Image, ImageDraw

variant = Path(__file__).resolve().parent
preview = variant / "preview"
for path in sorted(preview.glob("*.pbm")):
    words = path.read_text().split()
    assert words[:3] == ["P1", "128", "64"] and len(words) == 8195, path
    assert set(words[3:]) <= {"0", "1"}, path
    frame = Image.new("L", (128, 64))
    frame.putdata([0 if pixel == "1" else 255 for pixel in words[3:]])
    frame.resize((512, 256), Image.NEAREST).save(path.with_suffix(".png"))

names = ["home-online", "bag-full", "bag-open", "home-offline", "home-fault",
         "maintenance", "debug-0", "debug-16", "confirm", "unlock-confirm",
         "battery-low", "battery-start-denied"]
sheet = Image.new("RGB", (1260, 1040), "#e9ece8")
draw = ImageDraw.Draw(sheet)
for i, name in enumerate(names):
    frame = Image.open(preview / (name + ".png")).resize((384, 192), Image.NEAREST)
    x, y = (i % 3) * 420 + 18, (i // 3) * 260 + 40
    sheet.paste(frame, (x, y))
    draw.text((x, y - 24), name, fill="#171c17")
sheet.save(preview / "overview.png")
debug_names = ["debug-0", "debug-4", "debug-8", "debug-12", "debug-16",
               "debug-20", "debug-24", "home-max-values", "notice-14-unlock"]
debug_sheet = Image.new("RGB", (1260, 780), "#e9ece8")
debug_draw = ImageDraw.Draw(debug_sheet)
for i, name in enumerate(debug_names):
    frame = Image.open(preview / (name + ".png")).resize((384, 192), Image.NEAREST)
    x, y = (i % 3) * 420 + 18, (i // 3) * 260 + 40
    debug_sheet.paste(frame, (x, y))
    debug_draw.text((x, y - 24), name, fill="#171c17")
debug_sheet.save(preview / "debug-overview.png")
print(f"Rendered {len(list(preview.glob('*.pbm')))} actual framebuffers and overview.png")
