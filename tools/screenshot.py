#!/usr/bin/env python3
"""Grab the panel's canvas over USB serial -> PNG. Usage: screenshot.py PORT OUT.png [page-tap-x]"""
import sys, time, serial
port, out = sys.argv[1], sys.argv[2]
s = serial.Serial(port, 115200, timeout=5)
s.reset_input_buffer(); s.write(b's')
buf = b''; t = time.time()
while b'SHOT ' not in buf and time.time() - t < 10: buf += s.read(256)
head, rest = buf.split(b'SHOT ', 1)
while b'\n' not in rest: rest += s.read(64)
dims, data = rest.split(b'\n', 1)
w, h = map(int, dims.split())
need = w * h * 2
while len(data) < need: data += s.read(need - len(data))
px = data[:need]
open(out + '.raw', 'wb').write(px)
try:
    import PIL
except ImportError:
    print('raw saved', out + '.raw'); sys.exit(0)
from PIL import Image  # imported late: pyserial and Pillow may live in different Pythons
im = Image.new('RGB', (w, h))
im.putdata([(((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
            for v in (px[i] | px[i + 1] << 8 for i in range(0, need, 2))])
im.save(out); print(out, w, h)
