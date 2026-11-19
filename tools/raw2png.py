#!/usr/bin/env python3
"""raw RGB565-LE canvas -> PNG. Usage: raw2png.py in.raw out.png [w h]
Without w h it reads the size screenshot.py saved next to the raw file (in.raw.size), else assumes 480x320.
Landscape and portrait frames are the same number of bytes, so the size cannot be guessed from the file."""
import os, sys
from PIL import Image
src, out = sys.argv[1], sys.argv[2]
if len(sys.argv) >= 5: w, h = int(sys.argv[3]), int(sys.argv[4])
elif os.path.exists(src + '.size'): w, h = map(int, open(src + '.size').read().split())
else: w, h = 480, 320
px = open(src, 'rb').read()
im = Image.new('RGB', (w, h))
im.putdata([(((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
            for v in (px[i] | px[i + 1] << 8 for i in range(0, w * h * 2, 2))])
im.save(out); print(out, w, h)
