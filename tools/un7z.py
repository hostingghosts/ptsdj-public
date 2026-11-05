"""Minimal .7z extractor (LZMA / LZMA2 / copy only, no encryption) using the stdlib lzma module. Usage: un7z.py a.7z outdir"""
import lzma, os, struct, sys, zlib

class R:
    def __init__(s, b): s.b, s.i = b, 0
    def u8(s): v = s.b[s.i]; s.i += 1; return v
    def num(s):
        f = s.u8(); m = 0x80; v = 0
        for k in range(8):
            if not f & m: return v | ((f & (m - 1)) << (8 * k))
            v |= s.u8() << (8 * k); m >>= 1
        return v
    def bits(s, n):
        out = []; 
        for k in range((n + 7) // 8):
            x = s.u8(); out += [(x >> (7 - j)) & 1 for j in range(8)]
        return out[:n]
    def bitvec(s, n):
        if s.u8(): return [1] * n
        return s.bits(n)

def unpack_folder(data, packpos, packsizes, folder):
    coders, bind, packed_in, unsz = folder
    if len(coders) != 1: raise SystemExit('unsupported: multi-coder folder (BCJ etc.)')
    cid, props = coders[0]
    raw = data[packpos:packpos + packsizes[0]]
    if cid == b'\x00': return raw
    if cid == b'\x21':
        flt = [{'id': lzma.FILTER_LZMA2, 'dict_size': ((2 | (props[0] & 1)) << (props[0] // 2 + 11)) if props[0] < 40 else 0xFFFFFFFF}]
    elif cid == b'\x03\x01\x01':
        lc = props[0] % 9; r = props[0] // 9; flt = [{'id': lzma.FILTER_LZMA1, 'lc': lc, 'lp': r % 5, 'pb': r // 5, 'dict_size': struct.unpack('<I', props[1:5])[0]}]
    else: raise SystemExit('unsupported coder %r' % cid)
    return lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=flt).decompress(raw, max_length=unsz[-1] if unsz else -1)

def streams_info(r):
    packpos = 0; packsizes = []; folders = []; substreams = None
    while True:
        t = r.u8()
        if t == 0: break
        if t == 6:
            packpos = r.num(); n = r.num()
            while True:
                t2 = r.u8()
                if t2 == 0: break
                if t2 == 9: packsizes = [r.num() for _ in range(n)]
                elif t2 == 10:
                    d = r.bitvec(n); [r.b.__getitem__(0) for _ in ()]; [r.i.__add__(0)]; r.i += 4 * sum(d)
        elif t == 7:
            assert r.u8() == 11; nf = r.num(); assert r.u8() == 0
            for _ in range(nf):
                nc = r.num(); coders = []; nin = nout = 0
                for _ in range(nc):
                    fl = r.u8(); idl = fl & 15; cid = bytes(r.b[r.i:r.i + idl]); r.i += idl
                    ni = no = 1
                    if fl & 0x10: ni = r.num(); no = r.num()
                    props = b''
                    if fl & 0x20: n2 = r.num(); props = bytes(r.b[r.i:r.i + n2]); r.i += n2
                    coders.append((cid, props)); nin += ni; nout += no
                bind = [(r.num(), r.num()) for _ in range(nout - 1)]
                pk = [r.num() for _ in range(nin - (nout - 1))] if nin - (nout - 1) > 1 else [0]
                folders.append([coders, bind, pk, []])
            assert r.u8() == 12
            for f in folders: f[3] = [r.num() for _ in range(sum(1 for _ in f[0]))]
            while True:
                t2 = r.u8()
                if t2 == 0: break
                if t2 == 10:
                    d = r.bitvec(nf); r.i += 4 * sum(1 for x in d if x)
                    for k, x in enumerate(d): folders[k].append(bool(x))
        elif t == 8:
            nsub = [1] * len(folders); sizes = []
            while True:
                t2 = r.u8()
                if t2 == 0: break
                if t2 == 13: nsub = [r.num() for _ in folders]
                elif t2 == 9:
                    for k, f in enumerate(folders):
                        tot = 0
                        for _ in range(nsub[k] - 1): s = r.num(); sizes.append(s); tot += s
                        if nsub[k]: sizes.append(f[3][-1] - tot)
                elif t2 == 10:
                    cnt = sum(n for k, n in enumerate(nsub) if not (n == 1 and len(folders[k]) > 4 and folders[k][4]))
                    d = r.bitvec(cnt); r.i += 4 * sum(1 for x in d if x)
            if not sizes: sizes = [f[3][-1] for f in folders]
            substreams = (nsub, sizes)
    if substreams is None: substreams = ([1] * len(folders), [f[3][-1] for f in folders])
    return packpos, packsizes, folders, substreams

def extract(path, out):
    data = open(path, 'rb').read()
    assert data[:6] == b"7z\xbc\xaf\x27\x1c"
    off, size = struct.unpack('<QQ', data[12:28])
    hdr = data[32 + off:32 + off + size]
    r = R(hdr)
    if r.u8() == 0x17:
        pp, ps, fo, _ = streams_info(r)
        hdr = unpack_folder(data, 32 + pp, ps, fo[0]); r = R(hdr); assert r.u8() == 1
    names = []; empty = []; sinfo = None
    while True:
        t = r.u8()
        if t == 0: break
        if t == 4: sinfo = streams_info(r)
        elif t == 5:
            nfiles = r.num()
            while True:
                t2 = r.u8()
                if t2 == 0: break
                sz = r.num(); end = r.i + sz
                if t2 == 25: pass
                elif t2 == 14: empty = r.bits(nfiles)
                elif t2 == 17:
                    assert r.u8() == 0; s = bytes(r.b[r.i:end]); names = s.decode('utf-16-le').split('\0')[:nfiles]
                r.i = end
    if not names: names = ['f%d' % i for i in range(nfiles)]
    if not empty: empty = [0] * len(names)
    pp, ps, folders, (nsub, sizes) = sinfo
    pos = 32 + pp; blobs = []; si = 0; pk = 0
    for k, f in enumerate(folders):
        blob = unpack_folder(data, pos, ps[pk:pk + 1], f); pos += ps[pk]; pk += 1
        o = 0
        for _ in range(nsub[k]): blobs.append(blob[o:o + sizes[si]]); o += sizes[si]; si += 1
    bi = 0
    for n, e in zip(names, empty):
        dst = os.path.join(out, n.replace('\\', '/'))
        if e and n.endswith('/'): os.makedirs(dst, exist_ok=True); continue
        os.makedirs(os.path.dirname(dst) or out, exist_ok=True)
        if e: open(dst, 'wb').close(); continue
        open(dst, 'wb').write(blobs[bi]); bi += 1; print(dst, len(blobs[bi - 1]))

if __name__ == '__main__':
    extract(sys.argv[1], sys.argv[2])
