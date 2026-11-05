import sys, re, glob, os
import lsdsng, lsdjsav as J, compare, replay
tp = open(os.path.join(os.path.dirname(__file__), '../../src/gb/tables.inc')).read()
pf = [int(x) for x in re.search(r'kPulseFreq\[128\] = \{([^}]*)\}', tp).group(1).split(',')]
def nn(f):
    b = min(range(1, 128), key=lambda i: abs(pf[i] - f)); d = f - pf[b]
    return b if d == 0 else '%d%+d' % (b, d)
def go(name, ch, ticks=60, field='freq', start=0):
    f = glob.glob(os.path.join(os.path.dirname(__file__), '../../demos/%s*.lsdsng' % name))[0]
    m = lsdsng.decompress(open(f, 'rb').read()); sav = m + J.BASE[0x8000:]; tempo = m[0x3FB4]
    rom = replay.rom_states(sav, tempo, ticks, frames=int(ticks / (tempo * .4) * 60) + 150); me = compare.mine_states(sav, ticks + 30)
    conv = (lambda x: nn(x)) if field == 'freq' else (lambda x: x)
    print('ROM', [conv(rom[(k, ch)][field]) for k in range(start, ticks)])
    print('ME ', [conv(me[(k, ch)][field]) for k in range(start, ticks)])
    return m
if __name__ == '__main__': go(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]) if len(sys.argv) > 3 else 40, sys.argv[4] if len(sys.argv) > 4 else 'freq')
