import subprocess, tempfile, os
import lsdjsav as J, probe
def rom_states(sav, tempo, ticks, frames=400):
    L = J.run(sav, frames=frames)
    d = tempfile.mktemp(dir=probe.ORA)
    open(d, 'w').write('\n'.join('0 %d %02X %02X' % x for x in L))
    out = subprocess.run([probe.ORA + '/regreplay', d, str(tempo), str(ticks)], capture_output=True, text=True).stdout
    os.remove(d)
    st = {}
    for l in out.split('\n'):
        if l:
            k, c, en, vol, fr, du, pan = l.split(); st[(int(k), int(c))] = dict(en=int(en), vol=int(vol), freq=int(fr), duty=int(du), pan=int(pan, 16))
    return st
