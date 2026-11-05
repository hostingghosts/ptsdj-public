"""Per-demo mismatch summary ROM vs player (first N ticks): counts per channel and field, ignoring volume +-1."""
import glob, os, sys
import lsdsng, lsdjsav as J, compare
def run(names=None, ticks=200):
    for f in sorted(glob.glob(os.path.join(os.path.dirname(__file__), '../../demos/*.lsdsng'))):
        n = os.path.basename(f)
        if names and not any(x in n for x in names): continue
        m = lsdsng.decompress(open(f, 'rb').read()); sav = m + J.BASE[0x8000:]; tempo = m[0x3FB4]
        try: bad = compare.compare(sav, tempo, ticks, chans=(0, 1, 2, 3))
        except Exception as e: print(n, 'ERR', e); continue
        bad = [b for b in bad if not (b[2] == 'vol' and abs(b[3] - b[4]) <= 1)]
        cnt = {}
        for k, c, fld, a, b in bad: cnt[(c, fld)] = cnt.get((c, fld), 0) + 1
        print(n[:26].ljust(27), 'tempo', tempo, 'bad', len(bad), dict(sorted(cnt.items())), flush=True)
if __name__ == '__main__': run(sys.argv[1:] or None)
