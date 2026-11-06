import sys, shutil, subprocess, tempfile, collections
import lsdjsav as J, probe, synthlab as S
s = J.S(); s.clear_song(); s.tempo(128); s.note(0, 0, 25, 0); s.chain(0, [0]); s.row(0, [0, None, None, None]); s.instr(0, [1, 0x60, 0, 0, 0xFF, 0, 0, 3, 0, 0, 0, 0])
L = ['wait 600'] + S.nav('RIGHT') + S.nav('RIGHT') + S.nav('UP') + ['wait 8', 'trace t.txt', 'press A+RIGHT 6', 'wait 30', 'trace off']
d = tempfile.mkdtemp(dir=probe.ORA); shutil.copy(probe.ROM, d + '/lsdj.gb'); open(d + '/lsdj.sav', 'wb').write(s.sav()); open(d + '/s.txt', 'w').write('\n'.join(L) + '\n')
subprocess.run([probe.ORA + '/drive', 'lsdj.gb', 's.txt'], cwd=d, check=True)
rows = [l.split() for l in open(d + '/t.txt')]; shutil.rmtree(d)
c = collections.Counter()
for r in rows:
    a = int(r[2], 16)
    if 0xA000 <= a <= 0xBFFF: c[(r[0], r[1], 'sram' if a < 0xB000 else 'sram+')] += 1
print(len(rows), 'writes'); 
pcs = collections.Counter((r[0], r[1]) for r in rows)
for k, v in pcs.most_common(25): print(k, v)
