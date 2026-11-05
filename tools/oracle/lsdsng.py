"""LSDJ .lsdsng / .lsdprj (8-byte name, version byte, 512-byte compressed blocks): put into a 128 KB .sav file table.
Block layout follows lsdmanager 1.0 (MIT): FAT at 0x8141, blocks from 0x8200; 'E0 nn' jump bytes are renumbered (+1)."""
def next_jump_ptr(blk, base):
    i = 0
    while i < 512:
        b = blk[i]
        if b == 0xC0:
            i += 1 if blk[i+1] == 0xC0 else 2
        elif b == 0xE0:
            n = blk[i+1]
            if n in (0xE0, 0xFF): i += 1
            elif n in (0xF0, 0xF1): i += 2
            else: return base + i + 1
        i += 1
    return None

def to_sav(sng, sav=None):
    sav = bytearray(sav if sav else b'\xff' * 0x20000)
    if not sav[0x813E:0x8140] == b'jk': 
        sav[0x8000:0x8140] = bytes(0x140); sav[0x813E:0x8140] = b'jk'; sav[0x8140:0x8200] = b'\xff' * 0xC0
    name, ver, data = sng[:8], sng[8], sng[9:]
    slot = next(i for i in range(32) if sav[0x8000 + 8*i] == 0)
    sav[0x8000+8*slot:0x8008+8*slot] = name; sav[0x8100+slot] = ver
    nblk = len(data) // 512
    free = [i for i in range(0xBF) if sav[0x8141+i] == 0xFF]
    prev = None
    for k in range(nblk):
        b = free[k]
        if prev is not None: sav[prev] = b + 1
        sav[0x8141+b] = slot
        sav[0x8200+512*b:0x8200+512*(b+1)] = data[512*k:512*(k+1)]
        prev = next_jump_ptr(sav[0x8200+512*b:0x8200+512*(b+1)], 0x8200+512*b)
    return bytes(sav), slot

LOAD = ['press SELECT 6','press SELECT+UP 6','wait 6','press DOWN 360','wait 6','press A 6','wait 6','press A 6','wait 6','press UP 600','wait 6']
def load_script(position=0):
    return LOAD + ['press DOWN 6', 'wait 6'] * position + ['press A 6','wait 6','press LEFT 6','wait 6','press A 6','wait 600']

DEF_WAVE = bytes.fromhex('71323344455566777889 99aaabbccdce'.replace(' ', ''))
DEF_INSTR = bytes([0x00, 0x88, 0, 0, 0x7F, 1, 0, 3] + [0] * 8)

def decompress(sng, def_instr=DEF_INSTR, def_wave=DEF_WAVE):
    """Decode the block stream (blocks in chain order) to the 0x8000-byte working song."""
    data, out, i = sng[9:], bytearray(), 0
    while i < len(data):
        b = data[i]
        if b == 0xC0:
            if data[i+1] == 0xC0: out.append(0xC0); i += 2
            else: out += bytes([data[i+1]]) * data[i+2]; i += 3
        elif b == 0xE0:
            n = data[i+1]
            if n == 0xE0: out.append(0xE0); i += 2
            elif n == 0xFF: break
            elif n == 0xF0: out += def_wave * data[i+2]; i += 3
            elif n == 0xF1: out += def_instr * data[i+2]; i += 3
            else:   # jump to another block: continue with the next block of the file
                i = (i // 512 + 1) * 512
        else: out.append(b); i += 1
    return bytes(out)

def sav_slots(sav):
    """Songs in a .sav file table -> [(slot, name, version, lsdsng bytes)]"""
    out = []
    for s in range(32):
        blocks = [i for i in range(0xBF) if sav[0x8141+i] == s]
        if not blocks: continue
        name = bytes(sav[0x8000+8*s:0x8008+8*s]); ver = sav[0x8100+s]
        out.append((s, name, ver, name + bytes([ver]) + b''.join(sav[0x8200+512*b:0x8200+512*(b+1)] for b in blocks)))
    return out
