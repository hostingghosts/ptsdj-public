"""Tiny SM83 (Game Boy CPU) disassembler for studying a ROM image the owner supplied. Usage: sm83.py rom.gb bank addr [count]"""
import sys
R8 = ['B', 'C', 'D', 'E', 'H', 'L', '(HL)', 'A']; R16 = ['BC', 'DE', 'HL', 'SP']; R16S = ['BC', 'DE', 'HL', 'AF']; CC = ['NZ', 'Z', 'NC', 'C']
ALU = ['ADD A,', 'ADC A,', 'SUB ', 'SBC A,', 'AND ', 'XOR ', 'OR ', 'CP ']
ROT = ['RLC', 'RRC', 'RL', 'RR', 'SLA', 'SRA', 'SWAP', 'SRL']
def dis(mem, pc):
    """mem: function(addr)->byte. returns (text, length)"""
    op = mem(pc); n8 = lambda: mem(pc + 1); n16 = lambda: mem(pc + 1) | mem(pc + 2) << 8
    s8 = lambda: (mem(pc + 1) ^ 128) - 128
    if op == 0xCB:
        c = mem(pc + 1); r = R8[c & 7]; b = (c >> 3) & 7
        if c < 0x40: return '%s %s' % (ROT[b], r), 2
        return '%s %d,%s' % (['BIT', 'RES', 'SET'][(c >> 6) - 1], b, r), 2
    if op < 0x40:
        lo = op & 15; hi = op >> 4
        if op == 0x00: return 'NOP', 1
        if op == 0x08: return 'LD ($%04X),SP' % n16(), 3
        if op == 0x10: return 'STOP', 2
        if op == 0x18: return 'JR $%04X' % (pc + 2 + s8()), 2
        if op in (0x20, 0x28, 0x30, 0x38): return 'JR %s,$%04X' % (CC[(op >> 3) & 3], pc + 2 + s8()), 2
        if lo == 1: return 'LD %s,$%04X' % (R16[hi], n16()), 3
        if lo == 9: return 'ADD HL,%s' % R16[hi], 1
        if lo == 2: return 'LD %s,A' % ['(BC)', '(DE)', '(HL+)', '(HL-)'][hi], 1
        if lo == 0xA: return 'LD A,%s' % ['(BC)', '(DE)', '(HL+)', '(HL-)'][hi], 1
        if lo == 3: return 'INC %s' % R16[hi], 1
        if lo == 0xB: return 'DEC %s' % R16[hi], 1
        if lo in (4, 0xC): return 'INC %s' % R8[(op >> 3) & 7], 1
        if lo in (5, 0xD): return 'DEC %s' % R8[(op >> 3) & 7], 1
        if lo in (6, 0xE): return 'LD %s,$%02X' % (R8[(op >> 3) & 7], n8()), 2
        return {0x07: 'RLCA', 0x0F: 'RRCA', 0x17: 'RLA', 0x1F: 'RRA', 0x27: 'DAA', 0x2F: 'CPL', 0x37: 'SCF', 0x3F: 'CCF'}[op], 1
    if op < 0x80:
        if op == 0x76: return 'HALT', 1
        return 'LD %s,%s' % (R8[(op >> 3) & 7], R8[op & 7]), 1
    if op < 0xC0: return '%s%s' % (ALU[(op >> 3) & 7], R8[op & 7]), 1
    if op in (0xC0, 0xC8, 0xD0, 0xD8): return 'RET %s' % CC[(op >> 3) & 3], 1
    if op in (0xC2, 0xCA, 0xD2, 0xDA): return 'JP %s,$%04X' % (CC[(op >> 3) & 3], n16()), 3
    if op in (0xC4, 0xCC, 0xD4, 0xDC): return 'CALL %s,$%04X' % (CC[(op >> 3) & 3], n16()), 3
    if op & 15 == 1 and op >= 0xC0 and op < 0xF0 or op == 0xF1: return 'POP %s' % R16S[(op >> 4) & 3], 1
    if op & 15 == 5 and op >= 0xC0: return 'PUSH %s' % R16S[(op >> 4) & 3], 1
    if op & 7 == 7: return 'RST $%02X' % (op & 0x38), 1
    if op & 7 == 6: return '%s$%02X' % (ALU[(op >> 3) & 7], n8()), 2
    t = {0xC3: ('JP $%04X' % n16() if True else '', 3), 0xC9: ('RET', 1), 0xCD: ('CALL $%04X' % n16(), 3), 0xD9: ('RETI', 1), 0xE0: ('LDH ($FF%02X),A' % n8(), 2),
         0xF0: ('LDH A,($FF%02X)' % n8(), 2), 0xE2: ('LD (C),A', 1), 0xF2: ('LD A,(C)', 1), 0xE8: ('ADD SP,%d' % s8(), 2), 0xE9: ('JP (HL)', 1),
         0xEA: ('LD ($%04X),A' % n16(), 3), 0xFA: ('LD A,($%04X)' % n16(), 3), 0xF3: ('DI', 1), 0xFB: ('EI', 1), 0xF8: ('LD HL,SP%+d' % s8(), 2), 0xF9: ('LD SP,HL', 1)}
    return t.get(op, ('DB $%02X' % op, 1))
def main():
    rom = open(sys.argv[1], 'rb').read(); bank = int(sys.argv[2], 0); pc = int(sys.argv[3], 0); cnt = int(sys.argv[4]) if len(sys.argv) > 4 else 40
    def mem(a):
        if a < 0x4000: return rom[a]
        return rom[bank * 0x4000 + (a - 0x4000)] if a < 0x8000 else 0
    for _ in range(cnt):
        t, l = dis(mem, pc); print('%02X:%04X  %-24s %s' % (bank if pc >= 0x4000 else 0, pc, ' '.join('%02X' % mem(pc + i) for i in range(l)), t)); pc += l
if __name__ == '__main__': main()
