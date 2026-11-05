"""Run a button script in the oracle from a base .sav; return SRAM. Usage: import probe; probe.run(lines, base)."""
import os, shutil, subprocess, tempfile
ORA = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'reference', 'oracle')
ROM = os.path.join(ORA, 'run', 'lsdj.gb')
BOOT = ['wait 600', 'press START 4', 'wait 300', 'press A 4', 'wait 600']   # first boot: init cart

def run(lines, base=None, boot=True, reglog=None):
    d = tempfile.mkdtemp(dir=ORA)
    try:
        shutil.copy(ROM, d + '/lsdj.gb')
        if base: open(d + '/lsdj.sav', 'wb').write(base)
        open(d + '/s.txt', 'w').write('\n'.join((BOOT if boot and not base else ['wait 600']) + list(lines) + ['wait 30']))
        env = dict(os.environ)
        if reglog: env['REGLOG'] = reglog
        subprocess.run([ORA + '/drive', 'lsdj.gb', 's.txt'], cwd=d, env=env, check=True)
        return open(d + '/lsdj.sav', 'rb').read()
    finally:
        shutil.rmtree(d)

def diff(a, b, lim=0x8000):
    return [(i, a[i], b[i]) for i in range(lim) if a[i] != b[i]]

def press(*keys, n=4):
    return ['press %s %d' % (k, n) for k in keys]
