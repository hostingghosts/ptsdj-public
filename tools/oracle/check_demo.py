"""Load the demo .sav in LSDJ (oracle), press START, report which sound channels got note triggers."""
import os, shutil, subprocess, sys, tempfile
import probe
sav = open(sys.argv[1], 'rb').read()
d = tempfile.mkdtemp(dir=probe.ORA)
shutil.copy(probe.ROM, d + '/lsdj.gb'); open(d + '/lsdj.sav', 'wb').write(sav)
open(d + '/s.txt', 'w').write('wait 600\npress START 4\nwait 400\n')
subprocess.run([probe.ORA + '/drive', 'lsdj.gb', 's.txt'], cwd=d, env=dict(os.environ, REGLOG=d + '/reg.log'), check=True)
trig = {'NR14': 0x14, 'NR24': 0x19, 'NR34': 0x1E, 'NR44': 0x23}
n = {k: 0 for k in trig}
for l in open(d + '/reg.log'):
    f, r, v = l.split()
    for k, a in trig.items():
        if int(r, 16) == a and int(v, 16) & 0x80: n[k] += 1
print(n)
shutil.rmtree(d)
sys.exit(0 if all(n.values()) else 1)
