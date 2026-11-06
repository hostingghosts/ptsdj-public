"""synthcheck.py N [seed]: random synth parameter sets -> ROM frames (its own UI regenerates) vs gb::SynthGen (tools/oracle/synthgen_cli)."""
import random, subprocess, sys
import shots, lsdjsav as J, probe, synthlab as S
def rom_frames(params):
    s = J.S(); s.clear_song(); s.tempo(128); s.note(0, 0, 25, 0); s.chain(0, [0]); s.row(0, [0, None, None, None]); s.instr(0, [1, 0x60, 0, 0, 0xFF, 0, 0, 3, 0, 0, 0, 0])
    s.m[0x3EB2:0x3EB2 + 16] = bytes(params)
    L = ['wait 600'] + S.nav('RIGHT') + S.nav('RIGHT') + S.nav('UP') + ['wait 8', 'press A+RIGHT 6', 'wait 40', 'press A+LEFT 6', 'wait 60']
    sav = shots.run(s.sav(), L, '/tmp/claude-1000/lab')
    return sav[0x3EB2:0x3EB2 + 16], [sav[0x6000 + f * 16:0x6000 + f * 16 + 16] for f in range(16)]
def mine(params, cli):
    out = subprocess.run([cli] + ['%02x' % p for p in params], capture_output=True, text=True).stdout.split('\n')
    return [bytes.fromhex(l.split()[1]) for l in out if l.startswith('  f')]
if __name__ == '__main__':
    n = int(sys.argv[1]); random.seed(int(sys.argv[2]) if len(sys.argv) > 2 else 1); cli = '/tmp/sg'
    bad = 0
    for t in range(n):
        p = [random.randint(0, 2), random.randint(0, 4), random.randint(0, 255), random.randint(0, 2), random.randint(0, 3), random.randint(0, 255), random.randint(0, 255),
             random.randint(0, 255), random.randint(0, 255), random.randint(0, 255), random.randint(0, 255), random.randint(0, 255), random.randint(0, 255), random.randint(0, 255), random.randint(0, 255), 0]
        got, frames = rom_frames(p)
        if list(got) != p: print('param readback differs', p, list(got)); continue
        m = mine(p, cli)
        diff = sum(1 for a, b in zip(frames, m) if a != b)
        print(t, ' '.join('%02x' % x for x in p), 'frames differing:', diff)
        bad += diff > 0
    print('bad sets', bad, 'of', n)
