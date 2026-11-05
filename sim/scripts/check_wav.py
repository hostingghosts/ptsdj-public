"""Check the simulator's recorded WAV: 16-bit stereo, audible, and not clipped. Usage: check_wav.py file.wav"""
import struct
import sys
import wave

with wave.open(sys.argv[1], "rb") as w:
    assert w.getnchannels() == 2 and w.getsampwidth() == 2, "expect 16-bit stereo"
    data = w.readframes(w.getnframes())
s = struct.unpack("<%dh" % (len(data) // 2), data)
peak = max(abs(x) for x in s)
clipped = sum(1 for x in s if abs(x) >= 32767)
rms = (sum(x * x for x in s) / len(s)) ** 0.5
print(f"{sys.argv[1]}: {len(s)//2} frames, peak {peak}, rms {rms:.0f}, clipped {clipped}")
if rms < 100: sys.exit("FAIL: output is (nearly) silent")
if clipped > len(s) // 2000: sys.exit("FAIL: output clips")
print("ok")
