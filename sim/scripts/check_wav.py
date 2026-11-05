"""Smoke check for the simulator's recorded WAV: 16-bit stereo, and silent (the app has no sound engine yet)."""
import struct
import sys
import wave

path = sys.argv[1]
with wave.open(path, "rb") as w:
    assert w.getnchannels() == 2 and w.getsampwidth() == 2, "expect 16-bit stereo"
    data = w.readframes(w.getnframes())
samples = struct.unpack("<%dh" % (len(data) // 2), data)
peak = max((abs(s) for s in samples), default=0)
print(f"{path}: {len(samples) // 2} frames, peak {peak}")
if not samples:
    sys.exit("FAIL: no audio was pulled")
if peak:
    sys.exit("FAIL: output is not silent")
print("ok")
