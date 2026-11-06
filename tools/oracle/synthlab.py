"""Drive the ROM's Synth screen and read back synth 0's parameter bytes and its 16 generated wave frames."""
import shots, lsdjsav as J, probe
def nav(k): return ['press SELECT 6', 'press SELECT+%s 6' % k, 'wait 8']
def lab(edits, shot=None):
    s = J.S(); s.clear_song(); s.tempo(128); s.note(0, 0, 25, 0); s.chain(0, [0]); s.row(0, [0, None, None, None]); s.instr(0, [1, 0x60, 0, 0, 0xFF, 0, 0, 3, 0, 0, 0, 0])
    L = ['wait 600'] + nav('RIGHT') + nav('RIGHT') + nav('UP') + ['wait 8'] + edits + ['wait 20'] + (['shot %s.ppm' % shot] if shot else [])
    sav = shots.run(s.sav(), L, '/tmp/claude-1000/lab')
    return sav
def view(sav):
    p = sav[0x3EB2:0x3EB2 + 16]
    print('params', ' '.join('%02x' % b for b in p))
    for f in range(16): print('  f%02d' % f, ''.join('%x%x' % (b >> 4, b & 15) for b in sav[0x6000 + f * 16:0x6000 + f * 16 + 16]))
if __name__ == '__main__':
    import sys
    view(lab(eval(sys.argv[1]) if len(sys.argv) > 1 else [], 'a'))
