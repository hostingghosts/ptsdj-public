import lsdjsav as J, compare
def tsong(tbl, rec=None, notes=((0,25),), ticks=40, tempo=128, extra=None, ch=0):
    """tbl: list of rows (env, trans, cmd1, val1, cmd2, val2); table 0 attached to instrument 0 (pulse)."""
    s = J.S(); s.clear_song()
    for st, n in notes: s.note(0, st, n, 0)
    s.chain(0, [0]); s.row(0, [0 if ch == 0 else None, None, None, None]); s.tempo(tempo)
    r = rec or [0, 0xF0, 0, 0, 0x7F, 1, 0x20, 3]
    s.instr(0, r)
    for i, row in enumerate(tbl):
        e, t, c1, v1, c2, v2 = (list(row) + [0] * 6)[:6]
        s.m[0x1690 + i] = e; s.m[0x3480 + i] = t; s.m[0x3680 + i] = c1; s.m[0x3880 + i] = v1; s.m[0x3A80 + i] = c2; s.m[0x3C80 + i] = v2
    if extra: extra(s)
    return s
def rom(s, ticks=40, tempo=128, chans=(0,)):
    import replay
    st = replay.rom_states(s.sav(), tempo, ticks, frames=int(ticks / (tempo * 0.4) * 60) + 120)
    return st
def show(s, ticks=24, field='freq', ch=0):
    st = rom(s, ticks); return [st[(k, ch)][field] for k in range(ticks)]

import subprocess as _sp, tempfile as _tf, os as _os, probe as _probe
def fine(s, ch=0, n=40, step=11651, frames=200):
    """Channel state every `step` cycles from the first trigger: list of (freq, vol, duty, en)."""
    L = J.run(s.sav(), frames=frames)
    d = _tf.mktemp(dir=_probe.ORA); open(d, 'w').write('\n'.join('0 %d %02X %02X' % x for x in L))
    out = _sp.run([_probe.ORA + '/regfine', d, str(ch), str(n), str(step)], capture_output=True, text=True).stdout; _os.remove(d)
    return [tuple(map(int, l.split())) for l in out.split('\n') if l]
