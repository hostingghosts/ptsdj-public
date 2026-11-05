import os, shutil, subprocess, tempfile
import lsdjsav as J, probe
def run(sav, frames=60, pre=('press UP 700','press LEFT 200')):
    d = tempfile.mkdtemp(dir=probe.ORA); shutil.copy(probe.ROM, d + '/lsdj.gb'); open(d + '/lsdj.sav', 'wb').write(sav)
    open(d + '/s.txt', 'w').write('wait 600\n' + '\n'.join(pre) + '\npress START 4\ncov x\nwait %d\ncovdump c.bin\n' % frames)
    subprocess.run([probe.ORA + '/drive', 'lsdj.gb', 's.txt'], cwd=d, check=True)
    c = open(d + '/c.bin', 'rb').read(); shutil.rmtree(d)
    return {i for i in range(len(c)) if c[i]}
def clusters(addrs):
    out = []
    for a in sorted(addrs):
        if out and a - out[-1][1] <= 3: out[-1][1] = a
        else: out.append([a, a])
    return [('%02X:%04X' % (a // 0x4000, 0x4000 + a % 0x4000 if a >= 0x4000 else a), b - a + 1) for a, b in out]
def song(cmd, val, ch=0, note=25, instr=None):
    s = J.S(); s.clear_song(); s.note(0, 0, note, 0)
    if cmd: s.cmd(0, 0, cmd, val)
    s.chain(0, [0]); s.row(0, [0 if c == ch else None for c in range(4)])
    s.instr(0, instr or [[0, 0xF0, 0, 0, 0x7F, 1, 0, 3], [1, 0x20, 0x0F, 0, 0xFF, 1, 0, 3], [2, 0xA8, 4, 0, 0xFF, 1, 0, 3], [3, 0xF0, 0, 0, 0x7F, 1, 0, 3]][ch] + [0] * 8)
    s.tempo(128); return s
