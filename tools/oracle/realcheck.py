"""Compare gb::Seq note ticks with the ROM on real songs (every .lsdprj and every .sav slot under reference/samples)."""
import glob, os, subprocess, sys, tempfile
import lsdsng, lsdjsav as J, probe
R = os.path.join(probe.ORA, '..', 'samples')
def songs():
    for f in sorted(glob.glob(R + '/*/*.lsdprj')): yield os.path.basename(f), lsdsng.decompress(open(f, 'rb').read())
    for f in sorted(glob.glob(R + '/*/*.sav')):
        for s, n, v, sng in lsdsng.sav_slots(open(f, 'rb').read()): yield n.rstrip(b'\0').decode('latin1'), lsdsng.decompress(sng)
def check(name, m, ticks=500):
    sav = m + J.BASE[0x8000:]; tempo = m[0x3FB4]
    L = J.run(sav, frames=int(ticks / (tempo * 0.4) * 60) + 120)
    d = tempfile.mktemp(suffix='.sav', dir=probe.ORA); open(d, 'wb').write(sav)
    out = subprocess.run([probe.ORA + '/seqdump', d, str(ticks + 60)], capture_output=True, text=True).stdout.split('\n'); os.remove(d)
    mine = {}
    for l in out:
        if l: c, k = map(int, l.split()); mine.setdefault(c, []).append(k)
    res = []
    for c in (0, 1):
        trig = [x for x in L if x[1] == J.TRIG[c] and x[2] & 0x80]
        if not trig or c not in mine: res.append('-'); continue
        allt = [x[0] for x in L if x[1] in J.TRIG.values() and x[2] & 0x80]
        t0 = min(allt); tk = 10485760.0 / tempo
        rom = [round((x[0] - t0) / tk) for x in trig]
        first = min(mine[k][0] for k in mine)
        me = [k - first for k in mine[c]]
        n = min(len(rom), len(me), 40)
        res.append('=' if rom[:n] == me[:n] and n > 3 else 'x(%d)' % n)
    return res
if __name__ == '__main__':
    for name, m in songs():
        print(name.ljust(32), check(name, m), flush=True)
