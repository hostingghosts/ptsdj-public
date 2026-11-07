"""Simulator screenshot: only the four Game Boy shades inside the LCD area, and not blank. Usage: check_screen.py shot.png"""
import struct, sys, zlib
d = open(sys.argv[1], "rb").read(); pos, idat = 8, b""
while pos < len(d):
    n, k = struct.unpack(">I4s", d[pos:pos+8]); b = d[pos+8:pos+8+n]
    if k == b"IHDR": w, h = struct.unpack(">II", b[:8])
    if k == b"IDAT": idat += b
    pos += 12 + n
raw = zlib.decompress(idat); st = w * 3; cols = {}
for y in range(24, 456):
    row = raw[y*(st+1)+1:(y+1)*(st+1)]
    for x in range(100, 700): c = tuple(row[x*3:x*3+3]); cols[c] = cols.get(c, 0) + 1
print(sys.argv[1], len(cols), "colours", sorted(cols.items(), key=lambda a: -a[1])[:4])
if len(cols) > 4: sys.exit("FAIL: more than four shades in the LCD area")
if len(cols) < 2: sys.exit("FAIL: blank screen")
print("ok")
