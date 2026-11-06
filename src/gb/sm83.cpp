#include "gb/sm83.h"

namespace gb {

uint8_t Sm83::getR(int r) {
    switch (r) {
        case 0: return b; case 1: return c; case 2: return d; case 3: return e;
        case 4: return h; case 5: return l; case 6: return rd(hl()); default: return a;
    }
}

void Sm83::setR(int r, uint8_t v) {
    switch (r) {
        case 0: b = v; break; case 1: c = v; break; case 2: d = v; break; case 3: e = v; break;
        case 4: h = v; break; case 5: l = v; break; case 6: wr(hl(), v); break; default: a = v; break;
    }
}

uint16_t Sm83::getRp(int p, bool useAf) {
    switch (p) { case 0: return bc(); case 1: return de(); case 2: return hl(); default: return useAf ? af() : sp; }
}

void Sm83::setRp(int p, uint16_t v, bool useAf) {
    switch (p) { case 0: setBc(v); break; case 1: setDe(v); break; case 2: setHl(v); break; default: if (useAf) setAf(v); else sp = v; break; }
}

bool Sm83::cond(int cc) const {
    switch (cc) { case 0: return !(f & kZ); case 1: return (f & kZ) != 0; case 2: return !(f & kC); default: return (f & kC) != 0; }
}

void Sm83::alu(int op, uint8_t v) {
    const int cy = (f & kC) ? 1 : 0;
    int r;
    switch (op) {
        case 0: r = a + v; f = ((a & 15) + (v & 15) > 15 ? kH : 0) | (r > 255 ? kC : 0); a = uint8_t(r); f |= a ? 0 : kZ; break;                   // ADD
        case 1: r = a + v + cy; f = ((a & 15) + (v & 15) + cy > 15 ? kH : 0) | (r > 255 ? kC : 0); a = uint8_t(r); f |= a ? 0 : kZ; break;        // ADC
        case 2: r = a - v; f = kN | ((a & 15) < (v & 15) ? kH : 0) | (r < 0 ? kC : 0); a = uint8_t(r); f |= a ? 0 : kZ; break;                    // SUB
        case 3: r = a - v - cy; f = kN | ((a & 15) < (v & 15) + cy ? kH : 0) | (r < 0 ? kC : 0); a = uint8_t(r); f |= a ? 0 : kZ; break;         // SBC
        case 4: a &= v; f = kH | (a ? 0 : kZ); break;                                                                                              // AND
        case 5: a ^= v; f = a ? 0 : kZ; break;                                                                                                     // XOR
        case 6: a |= v; f = a ? 0 : kZ; break;                                                                                                     // OR
        default: r = a - v; f = kN | ((a & 15) < (v & 15) ? kH : 0) | (r < 0 ? kC : 0) | (uint8_t(r) ? 0 : kZ); break;                            // CP
    }
}

uint8_t Sm83::cb(uint8_t op, uint8_t v) {
    const int x = op >> 6, y = (op >> 3) & 7;
    if (x == 1) { f = (f & kC) | kH | ((v >> y) & 1 ? 0 : kZ); return v; }   // BIT
    if (x == 2) return uint8_t(v & ~(1 << y));                                 // RES
    if (x == 3) return uint8_t(v | (1 << y));                                  // SET
    uint8_t r, cy;
    switch (y) {
        case 0: cy = v >> 7; r = uint8_t(v << 1 | cy); break;                  // RLC
        case 1: cy = v & 1; r = uint8_t(v >> 1 | cy << 7); break;              // RRC
        case 2: cy = v >> 7; r = uint8_t(v << 1 | ((f & kC) ? 1 : 0)); break;  // RL
        case 3: cy = v & 1; r = uint8_t(v >> 1 | ((f & kC) ? 0x80 : 0)); break;// RR
        case 4: cy = v >> 7; r = uint8_t(v << 1); break;                       // SLA
        case 5: cy = v & 1; r = uint8_t(v >> 1 | (v & 0x80)); break;           // SRA
        case 6: cy = 0; r = uint8_t(v << 4 | v >> 4); break;                   // SWAP
        default: cy = v & 1; r = uint8_t(v >> 1); break;                       // SRL
    }
    f = (r ? 0 : kZ) | (cy ? kC : 0);
    return r;
}

void Sm83::step() {
    const uint8_t op = fetch();
    const int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
    ++n_;
    if (x == 1) {
        if (op == 0x76) return;                               // HALT: nothing to wait for here
        setR(y, getR(z));
        return;
    }
    if (x == 2) { alu(y, getR(z)); return; }
    if (x == 0) {
        switch (z) {
            case 0:
                if (y == 0) return;
                if (y == 1) { const uint16_t n = fetch16(); wr(n, uint8_t(sp)); wr(uint16_t(n + 1), uint8_t(sp >> 8)); return; }
                if (y == 2) { fetch(); return; }
                { const int8_t d8 = int8_t(fetch()); if (y == 3 || cond(y - 4)) pc = uint16_t(pc + d8); }
                return;
            case 1:
                if (q == 0) { setRp(p, fetch16(), false); return; }
                { const uint32_t r = uint32_t(hl()) + getRp(p, false);
                  f = (f & kZ) | ((hl() & 0xFFF) + (getRp(p, false) & 0xFFF) > 0xFFF ? kH : 0) | (r > 0xFFFF ? kC : 0); setHl(uint16_t(r)); }
                return;
            case 2: {
                uint16_t addr = p == 0 ? bc() : p == 1 ? de() : hl();
                if (q == 0) wr(addr, a); else a = rd(addr);
                if (p == 2) setHl(uint16_t(hl() + 1)); else if (p == 3) setHl(uint16_t(hl() - 1));
                return;
            }
            case 3: setRp(p, uint16_t(getRp(p, false) + (q ? -1 : 1)), false); return;
            case 4: { const uint8_t v = uint8_t(getR(y) + 1); f = (f & kC) | (v ? 0 : kZ) | ((v & 15) == 0 ? kH : 0); setR(y, v); return; }
            case 5: { const uint8_t v = uint8_t(getR(y) - 1); f = (f & kC) | kN | (v ? 0 : kZ) | ((v & 15) == 15 ? kH : 0); setR(y, v); return; }
            case 6: setR(y, fetch()); return;
            default:
                switch (y) {
                    case 0: { const uint8_t cy = a >> 7; a = uint8_t(a << 1 | cy); f = cy ? kC : 0; return; }
                    case 1: { const uint8_t cy = a & 1; a = uint8_t(a >> 1 | cy << 7); f = cy ? kC : 0; return; }
                    case 2: { const uint8_t cy = a >> 7; a = uint8_t(a << 1 | ((f & kC) ? 1 : 0)); f = cy ? kC : 0; return; }
                    case 3: { const uint8_t cy = a & 1; a = uint8_t(a >> 1 | ((f & kC) ? 0x80 : 0)); f = cy ? kC : 0; return; }
                    case 4: {                                                                   // DAA
                        int r = a;
                        if (!(f & kN)) { if ((f & kC) || r > 0x99) { r += 0x60; f |= kC; } if ((f & kH) || (r & 15) > 9) r += 6; }
                        else { if (f & kC) r -= 0x60; if (f & kH) r -= 6; }
                        a = uint8_t(r); f = (f & (kN | kC)) | (a ? 0 : kZ); return;
                    }
                    case 5: a = uint8_t(~a); f |= kN | kH; return;
                    case 6: f = (f & kZ) | kC; return;
                    default: f = (f & kZ) | ((f & kC) ? 0 : kC); return;
                }
        }
    }
    // x == 3
    switch (z) {
        case 0:
            if (y < 4) { if (cond(y)) pc = pop(); return; }
            if (y == 4) { wr(uint16_t(0xFF00 + fetch()), a); return; }
            if (y == 5) {
                const int8_t d8 = int8_t(fetch());
                f = ((sp & 15) + (uint8_t(d8) & 15) > 15 ? kH : 0) | ((sp & 0xFF) + uint8_t(d8) > 0xFF ? kC : 0); sp = uint16_t(sp + d8); return;
            }
            if (y == 6) { a = rd(uint16_t(0xFF00 + fetch())); return; }
            { const int8_t d8 = int8_t(fetch());
              f = ((sp & 15) + (uint8_t(d8) & 15) > 15 ? kH : 0) | ((sp & 0xFF) + uint8_t(d8) > 0xFF ? kC : 0); setHl(uint16_t(sp + d8)); }
            return;
        case 1:
            if (q == 0) { setRp(p, pop(), true); return; }
            if (p == 0 || p == 1) { pc = pop(); return; }
            if (p == 2) { pc = hl(); return; }
            sp = hl();
            return;
        case 2:
            if (y < 4) { const uint16_t n = fetch16(); if (cond(y)) pc = n; return; }
            if (y == 4) { wr(uint16_t(0xFF00 + c), a); return; }
            if (y == 5) { wr(fetch16(), a); return; }
            if (y == 6) { a = rd(uint16_t(0xFF00 + c)); return; }
            a = rd(fetch16());
            return;
        case 3:
            if (y == 0) { pc = fetch16(); return; }
            if (y == 1) {
                const uint8_t o = fetch();
                if ((o & 0xC0) == 0x40) { cb(o, getR(o & 7)); return; }      // BIT leaves the register alone
                setR(o & 7, cb(o, getR(o & 7)));
                return;
            }
            return;                                                          // DI / EI and the unused codes
        case 4: { const uint16_t n = fetch16(); if (y < 4 && cond(y)) { push(pc); pc = n; } return; }
        case 5:
            if (q == 0) { push(getRp(p, true)); return; }
            if (p == 0) { const uint16_t n = fetch16(); push(pc); pc = n; }
            return;
        case 6: alu(y, fetch()); return;
        default: push(pc); pc = uint16_t(y * 8); return;
    }
}

bool Sm83::run(uint16_t stopAt, uint32_t maxInstr) {
    for (uint32_t i = 0; i < maxInstr; ++i) {
        if (pc == stopAt) return true;
        step();
    }
    return pc == stopAt;
}

}  // namespace gb
