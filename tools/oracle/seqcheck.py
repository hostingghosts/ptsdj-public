"""Random songs: compare note-trigger ticks of gb::Seq (seqdump) with the real ROM. Usage: seqcheck.py [seeds]"""
import os, random, subprocess, sys, tempfile
import lsdjsav as J, probe

def make(seed):
    r = random.Random(seed)
    s = J.S(); s.clear_song()
    nph = r.randint(2, 5)
    for p in range(nph):
        for st in r.sample(range(16), r.randint(1, 5)): s.note(p, st, r.randint(1, 40), 0 if r.random() < .8 else 1)
    nch = r.randint(2, 4)
    for c in range(nch):
        n = r.randint(1, 4); s.chain(c, [r.randrange(nph) for _ in range(n)])
    rows = r.randint(1, 4)
    for ro in range(rows):
        s.row(ro, [r.randrange(nch) if r.random() < .85 or ro == 0 else None for _ in range(4)])
    s.instr(0, [0, 0xA8, 0, 0, 0x7F, 1, 0, 3]); s.instr(1, [3, 0xA8, 0, 0, 0x7F, 1, 0, 3])
    g = r.choice([[6], [6, 6], [4, 8], [5, 7, 6], [3]])
    s.groove(0, g)
    t = r.choice([128, 100, 150, 200])
    s.tempo(t)
    return s, t

bad = 0
for seed in [int(x) for x in sys.argv[1:]] or range(6):
    s, t = make(seed)
    sav = s.sav()
    L = J.run(sav, frames=900)
    d = tempfile.mktemp(suffix='.sav', dir=probe.ORA); open(d, 'wb').write(sav)
    ticks = 330
    mine = {}
    for l in subprocess.run([probe.ORA + '/seqdump', d, str(ticks + 40)], capture_output=True, text=True).stdout.split('\n'):
        if l: c, k = map(int, l.split()); mine.setdefault(c, []).append(k)
    os.remove(d)
    ok = True
    first = min(mine[c][0] for c in mine) if mine else 0
    for c in (0, 1, 3):
        rom = [k for k in J.triggers(L, t, c) if k < ticks - 10]
        me = [k - first for k in mine.get(c, []) if k - first < ticks - 10]
        # ROM times are relative to its first trigger of any channel; align with mine the same way
        if rom != me:
            ok = False; print('seed', seed, 'ch', c, 'ROM', rom[:14], 'MINE', me[:14])
    print('seed', seed, 'tempo', t, 'OK' if ok else 'MISMATCH'); bad += not ok
sys.exit(1 if bad else 0)
