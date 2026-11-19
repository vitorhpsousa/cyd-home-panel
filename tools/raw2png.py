#!/usr/bin/env python3
"""raw RGB565-LE 480x320 -> PNG. Usage: raw2png.py in.raw out.png"""
import sys
from PIL import Image
px = open(sys.argv[1], 'rb').read(); w, h = 480, 320
im = Image.new('RGB', (w, h))
im.putdata([(((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
            for v in (px[i] | px[i + 1] << 8 for i in range(0, w * h * 2, 2))])
im.save(sys.argv[2])
