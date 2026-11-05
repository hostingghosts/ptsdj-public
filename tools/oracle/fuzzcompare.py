import random, sys
import lsdjsav as J, compare
def make(seed):
    r = random.Random(seed); s = J.S(); s.clear_song()
    for p in range(3):
        for st in r.sample(range(16), r.randint(2, 6)): s.note(p, st, r.randint(8, 70), r.randint(0, 3))
    for c in range(3): s.chain(c, [r.randrange(3) for _ in range(r.randint(1, 3))], [r.randint(-3, 3) & 255 for _ in range(3)])
    s.row(0, [0, 1, 2, 2])
    for i in range(4):
        env = r.choice([0xF0, 0xA8, 0x88, 0xF3, 0xF7, 0x5C, 0x8F, 0xC1]); ln = r.choice([0, 0, 0x40 | r.randint(1, 30)])
        pan = r.choice([1, 2, 3, 3]); duty = r.choice([0, 0x40, 0x80, 0xC0])
        s.instr(i, [i if i < 2 else (3 if i == 3 else 0), env, 0, ln, r.choice([0x7F, 0x7F, 0x5E]), 1, 0, duty | pan])
    s.groove(0, r.choice([[6], [4, 8], [5, 7]])); t = r.choice([110, 128, 140]); s.tempo(t)
    return s, t
bad = 0
for seed in [int(x) for x in sys.argv[1:]] or range(8):
    s, t = make(seed); b = compare.compare(s.sav(), t, 70, chans=(0, 1, 3))
    print(seed, len(b), b[:4]); bad += len(b) > 6
