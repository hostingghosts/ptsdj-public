"""Check a simulator screenshot (8-bit RGB PNG from sim/src/png.cpp): every pixel must be the given background colour.
Usage: check_blank.py shot.png 0xRRGGBB"""
import struct
import sys
import zlib

path, want = sys.argv[1], int(sys.argv[2], 16)
data = open(path, "rb").read()
assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
pos, idat, w = 8, b"", 0
while pos < len(data):
    n, kind = struct.unpack(">I4s", data[pos:pos + 8])
    body = data[pos + 8:pos + 8 + n]
    if kind == b"IHDR":
        w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
        assert (depth, ctype) == (8, 2), "expect 8-bit RGB"
    elif kind == b"IDAT":
        idat += body
    pos += 12 + n
raw = zlib.decompress(idat)
stride = w * 3
rgb = bytes([(want >> 16) & 255, (want >> 8) & 255, want & 255])
bad = 0
for y in range(h):
    row = raw[y * (stride + 1):(y + 1) * (stride + 1)]
    assert row[0] == 0, "expect filter type 0"
    px = row[1:]
    bad += sum(1 for x in range(w) if px[x * 3:x * 3 + 3] != rgb)
print(f"{path}: {w}x{h}, {bad} pixels differ from 0x{want:06X}")
if bad:
    sys.exit("FAIL: first screen is not blank")
print("ok")
