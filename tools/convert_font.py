#!/usr/bin/env python3
"""Convert bb_loading_font.ttf to a C header with variable-width column-major bitmaps."""
import sys, os
from PIL import Image, ImageDraw, ImageFont
import numpy as np

FONT_TTF    = os.path.join(os.path.dirname(__file__), '../assets/splash/bb_loading_font.ttf')
OUT_HEADER  = os.path.join(os.path.dirname(__file__), '../src/bb_font.h')
FONT_SIZE   = 12   # em-square px passed to Pillow
THRESHOLD   = 64   # binarise threshold

font = ImageFont.truetype(FONT_TTF, FONT_SIZE)
ascent, descent = font.getmetrics()
CELL_H = ascent + descent          # real rendered height (13 for size 12)
BYTES_PER_COL = (CELL_H + 7) // 8  # = 2

widths  = []
offsets = []
data    = []
offset  = 0

for code in range(32, 127):
    char = chr(code)
    offsets.append(offset)

    bbox = font.getbbox(char)
    if not bbox or bbox[2] <= 0:
        # non-printing / zero-width — use a space-sized empty glyph
        w = max(2, CELL_H // 4)
        widths.append(w)
        data.extend([0] * (w * BYTES_PER_COL))
        offset += w * BYTES_PER_COL
        continue

    w = bbox[2]   # advance width = right edge of bounding box
    widths.append(w)

    img = Image.new('L', (w, CELL_H), 0)
    ImageDraw.Draw(img).text((0, 0), char, fill=255, font=font)
    bitmap = np.array(img) > THRESHOLD   # shape: (CELL_H, w)

    for col in range(w):
        for byte_idx in range(BYTES_PER_COL):
            byte = 0
            for bit in range(8):
                row = byte_idx * 8 + bit
                if row < CELL_H and bitmap[row, col]:
                    byte |= (0x80 >> bit)   # MSB = top row
            data.append(byte)

    offset += w * BYTES_PER_COL

# ---- write header ----
with open(OUT_HEADER, 'w') as f:
    f.write('#pragma once\n')
    f.write('#include <stdint.h>\n\n')
    f.write(f'// bb_loading_font.ttf  size={FONT_SIZE}  cell_h={CELL_H}\n')
    f.write(f'#define BFONT_H {CELL_H}\n\n')

    # widths
    f.write('static const uint8_t bfont_widths[95] = {\n    ')
    f.write(', '.join(str(w) for w in widths))
    f.write('\n};\n\n')

    # offsets (uint16_t is enough — total data << 65535)
    f.write('static const uint16_t bfont_offsets[95] = {\n    ')
    f.write(', '.join(str(o) for o in offsets))
    f.write('\n};\n\n')

    # raw bitmap data
    f.write(f'static const uint8_t bfont_data[{len(data)}] = {{\n')
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        f.write('    ' + ', '.join(f'0x{b:02X}' for b in chunk) + ',\n')
    f.write('};\n')

print(f'Font: {CELL_H}px cell, {BYTES_PER_COL} bytes/col, {len(data)} bytes total, '
      f'widths {min(widths)}–{max(widths)}px')
print(f'Written: {OUT_HEADER}')
