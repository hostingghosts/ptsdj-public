"""Fuzz one instrument byte; show the register writes (ch1 + pan + length-related) of a single note vs baseline."""
import sys
import lsdjsav as J

def writes(rec, note=25, frames=120, extra=None):
    s = J.S(); s.clear_song(); s.note(0, 0, note, 0); s.chain(0, [0]); s.row(0, [0]); s.instr(0, rec)
    if extra: extra(s)
    L = J.run(s.sav(), frames=frames)
    t0 = min(c for c, r, v in L if r == 0x14 and v & 0x80)
    return [((c - t0) // 1000, r, v) for c, r, v in L if c >= t0 - 2000 and (r in (0x10,0x11,0x12,0x13,0x14,0x25,0x24))]

BASE = [0x00, 0x88, 0x00, 0x00, 0x7F, 0x01, 0x00, 0x03, 0, 0, 0, 0, 0, 0, 0, 0]
if __name__ == '__main__':
    base = writes(BASE)
    print('base first 12:', [(r, hex(v)) for t, r, v in base[:12]], 'n=', len(base))
    for i in range(1, 12):
        for v in ([0x00, 0x01, 0x20, 0x40, 0x80, 0xFF] if i != 1 else [0x00, 0x11, 0xF0, 0xA8, 0xF7, 0x37]):
            rec = list(BASE); rec[i] = v
            w = writes(rec)
            if w != base:
                fw = [(r, hex(vv)) for t, r, vv in w[:8]]
                print('byte', i, hex(v), 'n=', len(w), fw)
