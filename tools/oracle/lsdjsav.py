"""Build LSDJ songs as raw memory in Python and read note triggers from the oracle (mirror of src/gb/song.h)."""
import os, shutil, subprocess, tempfile
import probe

BASE = open(os.path.join(probe.ORA, 'base0.sav'), 'rb').read()

class S:
    def __init__(self): self.m = bytearray(BASE[:0x8000])
    def note(self, p, st, n, ins=None):
        self.m[p*16+st] = n
        if ins is not None: self.m[0x7000+p*16+st] = ins
        self.m[0x3E82+p//8] |= 1 << (p & 7)
    def cmd(self, p, st, c, v): self.m[0x4000+p*16+st] = c; self.m[0x4FF0+p*16+st] = v
    def chain(self, c, phrases, trans=None):
        for i in range(16):
            self.m[0x2080+c*16+i] = phrases[i] if i < len(phrases) else 0xFF
            self.m[0x2880+c*16+i] = (trans[i] if trans and i < len(trans) else 0)
        self.m[0x3EA2+c//8] |= 1 << (c & 7)
    def row(self, r, chans):
        for ch in range(4): self.m[0x1290+r*4+ch] = chans[ch] if ch < len(chans) and chans[ch] is not None else 0xFF
    def clear_song(self):
        for i in range(0x1290, 0x1690): self.m[i] = 0xFF
    def instr(self, i, data):
        self.m[0x3080+i*16:0x3080+i*16+len(data)] = bytes(data); self.m[0x2040+i] = 1
    def tempo(self, t): self.m[0x3FB4] = t
    def groove(self, g, ticks):
        for i in range(16): self.m[0x1090+g*16+i] = ticks[i] if i < len(ticks) else 0
    def sav(self): return bytes(self.m) + BASE[0x8000:]

def run(sav, frames=600, start='START', extra=None, aud=None):
    d = tempfile.mkdtemp(dir=probe.ORA); shutil.copy(probe.ROM, d + '/lsdj.gb'); open(d + '/lsdj.sav', 'wb').write(sav)
    open(d + '/s.txt', 'w').write('wait 600\npress %s 4\nwait %d\n' % (start, frames))
    env = dict(os.environ, REGLOG=d + '/reg.log')
    if aud: env['AUDLOG'] = aud
    subprocess.run([probe.ORA + '/drive', 'lsdj.gb', 's.txt'], cwd=d, env=env, check=True)
    L = [(int(c), int(r, 16), int(v, 16)) for f, c, r, v in (l.split() for l in open(d + '/reg.log'))]
    shutil.rmtree(d)
    return L

TRIG = {0: 0x14, 1: 0x19, 2: 0x1E, 3: 0x23}
def triggers(L, tempo, ch):
    """Trigger times in ticks since the first trigger of any channel (rounded)."""
    tk = 10485760.0 / tempo
    first = min(c for c, r, v in L if r in TRIG.values() and v & 0x80)
    return [round((c - first) / tk) for c, r, v in L if r == TRIG[ch] and v & 0x80]
