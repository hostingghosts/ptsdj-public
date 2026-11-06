"""shots.py NAME 'script lines ; ...' -> PNG screenshots of the ROM at given points. Lines: normal drive lines, 'shot X' adds a screenshot."""
import sys, os, shutil, subprocess, tempfile, zlib, struct
import lsdjsav as J, probe
def png(ppm, out, scale=3):
    d = open(ppm, 'rb').read(); i = d.index(b'255\n') + 4; px = d[i:]
    w, h = 160, 144; rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w): row += px[(y*w+x)*3:(y*w+x)*3+3] * scale
        rows += [b'\0' + bytes(row)] * scale
    raw = b''.join(rows)
    def ch(t, b): c = struct.pack('>I', len(b)) + t + b; return c + struct.pack('>I', zlib.crc32(t + b))
    open(out, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w*scale, h*scale, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))
def run(sav, lines, outdir):
    d = tempfile.mkdtemp(dir=probe.ORA); shutil.copy(probe.ROM, d + '/lsdj.gb'); open(d + '/lsdj.sav', 'wb').write(sav)
    open(d + '/s.txt', 'w').write('\n'.join(lines) + '\n')
    subprocess.run([probe.ORA + '/drive', 'lsdj.gb', 's.txt'], cwd=d, check=True)
    os.makedirs(outdir, exist_ok=True)
    for f in os.listdir(d):
        if f.endswith('.ppm'): png(d + '/' + f, outdir + '/' + f[:-4] + '.png')
    sav2 = open(d + '/lsdj.sav', 'rb').read(); shutil.rmtree(d); return sav2
if __name__ == '__main__':
    s = J.S(); s.clear_song(); s.tempo(128)
    out = run(s.sav(), ['wait 600', 'press UP 700', 'press LEFT 200', 'press SELECT+UP 6', 'wait 10', 'shot proj.ppm',
        'press SELECT+RIGHT 6', 'wait 10', 'shot wave.ppm', 'press SELECT+RIGHT 6', 'wait 10', 'shot synth.ppm'], sys.argv[1] if len(sys.argv) > 1 else '/tmp/claude-1000/shots')
