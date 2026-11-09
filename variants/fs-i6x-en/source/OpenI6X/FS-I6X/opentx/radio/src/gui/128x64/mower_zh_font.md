# Mower English UI Subset 11

This independent English snapshot retains the existing internal mowerZh helper
and file names to avoid changing rendering interfaces. The generated bitmap
contains only the ASCII used by the English home, maintenance, DEBUG and
confirmation views: 65 glyphs, 585 bytes. The original 11px type, 6px cell width
and 12px row height are retained. The Chinese original is unchanged.

The font lives in Flash; ordinary builds need neither Pillow nor a system font.

- Source: Noto Sans Mono CJK SC Regular, face 7 of NotoSansCJK-Regular.ttc.
- SHA256: b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a.
- License: SIL Open Font License 1.1; see mower_zh_font.LICENSE.
- Generation uses the original ASCII baseline 9 and threshold 80, checking
  glyph bounds and pixel counts against an uncropped reference before packing.

From this independent source root, verify the font with:

```sh
python3 tools/generate_mower_zh_font.py /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc --font-index 7 --check
```

After changing English text, run without --check, then run the actual UI harness.
It checks every drawn glyph, text bounds, visible text overlap and the complete
inverted grass-bag cell, including descenders.
