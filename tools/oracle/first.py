"""First ROM-vs-player divergence of a demo: first.py DEMO [skipfields] -> mismatches, per-tick states of all channels, phrases and instruments in use."""
import sys, glob, os
import lsdsng, lsdjsav as J, compare, replay
here = os.path.dirname(os.path.abspath(__file__))
n = sys.argv[1]; skip = sys.argv[2].split(',') if len(sys.argv) > 2 else []
f = glob.glob(os.path.join(here, '../../demos/%s*.lsdsng' % n))[0]
m = lsdsng.decompress(open(f, 'rb').read()); sav = m + J.BASE[0x8000:]; tempo = m[0x3FB4]
cf = os.path.join(here, '../../reference/oracle/conv/%s.conv' % os.path.basename(f)[:-7])
romsav = (open(cf, 'rb').read() + J.BASE[0x8000:]) if m[0x7FFF] != 0x16 and os.path.exists(cf) else None
bad = [b for b in compare.compare(sav, tempo, 200, romsav=romsav) if b[2] not in skip]
k0 = min(b[0] for b in bad); print('first bad', bad[:6])
rom = replay.rom_states(romsav or sav, tempo, k0 + 14, frames=int((k0 + 14) / (tempo * 0.4) * 60) + 150)
me = compare.mine_states(sav, k0 + 40)
for k in range(max(0, k0 - 2), k0 + 10):
    print(k, 'ROM', [(rom[(k, c)]['en'], rom[(k, c)]['vol'], rom[(k, c)]['freq'], rom[(k, c)]['duty'], hex(rom[(k, c)]['pan'])) for c in range(4)],
          '\n   ME ', [(me[(k, c)]['en'], me[(k, c)]['vol'], me[(k, c)]['freq'], me[(k, c)]['duty'], hex(me[(k, c)]['pan'])) for c in range(4)])
for c in range(4):
    row = m[0x1290 + c]; ph = m[0x2080 + row * 16]
    ins = [m[0x7000 + ph * 16 + i] for i in range(16)]
    print('ch', c, 'phrase', ph, 'ins', ins, 'notes', [m[ph * 16 + i] for i in range(16)],
          'cmds', [m[0x4000 + ph * 16 + i] for i in range(16)], [hex(m[0x4FF0 + ph * 16 + i]) for i in range(16)])
    for i in sorted(set(x for x in ins if x != 255)): print('   instr', i, [hex(x) for x in m[0x3080 + i * 16:0x3080 + i * 16 + 16]])
