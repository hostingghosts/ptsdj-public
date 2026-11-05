"""ROM vs gb::Player channel state per tick. compare(sav, tempo, ticks) -> list of (tick, ch, field, rom, mine)."""
import subprocess, tempfile, os
import lsdjsav as J, probe, replay
def mine_states(sav, ticks):
    d = tempfile.mktemp(suffix='.sav', dir=probe.ORA); open(d, 'wb').write(sav)
    out = subprocess.run([probe.ORA + '/playdump', d, str(ticks)], capture_output=True, text=True, env=dict(os.environ, GB_ROM=probe.ROM)).stdout; os.remove(d)
    raw = {}
    for l in out.split('\n'):
        if l:
            k, c, en, vol, fr, du, pan = l.split(); raw[(int(k), int(c))] = dict(en=int(en), vol=int(vol), freq=int(fr), duty=int(du), pan=int(pan, 16))
    # the ROM's tick 0 is its first note trigger: shift ours the same way
    shift = next((k for k in range(10**6) if any(raw.get((k, c), {}).get('en') for c in range(4))), 0) if raw else 0
    return {(k - shift, c): v for (k, c), v in raw.items() if k >= shift}
def compare(sav, tempo, ticks, chans=(0, 1, 2, 3), fields=('en', 'vol', 'freq', 'duty', 'pan'), frames=None, romsav=None):
    rom = replay.rom_states(romsav or sav, tempo, ticks, frames=frames or int(ticks / (tempo * 0.4) * 60) + 150)
    me = mine_states(sav, ticks + 24)
    bad = []
    for k in range(ticks):
        for c in chans:
            for f in fields:
                if f == 'vol' and c == 2: continue
                if rom[(k, c)][f] != me[(k, c)][f]: bad.append((k, c, f, rom[(k, c)][f], me[(k, c)][f]))
    return bad
if __name__ == '__main__':
    s = J.S(); s.clear_song()
    for st, n in ((0, 25), (4, 30), (8, 20), (12, 27)): s.note(0, st, n, 0)
    for st, n in ((2, 20), (6, 22)): s.note(1, st, n, 1)
    s.chain(0, [0]); s.chain(1, [1]); s.row(0, [0, None, None, 1]); s.tempo(128)
    s.instr(0, [0, 0xF7, 0, 0, 0x7F, 1, 0, 0x43]); s.instr(1, [3, 0xF3, 0, 0x40, 0x7F, 1, 0, 3])
    bad = compare(s.sav(), 128, 40)
    print(len(bad), 'mismatches'); print(bad[:15])
