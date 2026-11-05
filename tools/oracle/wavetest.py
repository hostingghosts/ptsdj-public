import lsdjsav as J
def seq(rec, ticks=60, note=25, tempo=128):
    s = J.S(); s.clear_song()
    for f in range(256):
        for i in range(16): s.m[0x6000 + f * 16 + i] = 0x11 * (f & 15) if i else f
    s.note(0, 0, note, 0); s.chain(0, [0]); s.row(0, [None, None, 0, None]); s.instr(0, rec); s.tempo(tempo)
    L = J.run(s.sav(), frames=int(ticks / (tempo * .4) * 60) + 100)
    t0 = min(c for c, r, v in L if r == 0x1E and v & 0x80)
    return [(round((c - t0) / (10485760 / tempo), 1), v) for c, r, v in L if r == 0x30 and c >= t0 - 3000]
BASE = [1, 0x20, 0x0f, 0x00, 0xff, 0x01, 0x00, 0x43, 0x00, 0x02, 0x08, 0xfe] + [0] * 4
