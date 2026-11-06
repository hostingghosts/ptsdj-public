#include "gb/upgrade.h"

#include <cstring>

namespace gb {

namespace {
// Release digit of the pulse/noise envelope byte: old -> new (8 = none in the old format, 9-F were the "increase" half).
uint8_t releaseNibble(uint8_t n) {
    static const uint8_t m[8] = {0, 5, 7, 8, 9, 10, 11, 11};
    if (n == 8 || n == 0) return 0;
    return m[n & 7];
}
// Old noise notes. LSDJ's upgrade shifts the instrument's old shape byte (b4) by the note's octave: the high nibble moves by
// (note - 1) / 12 - 3, stopping at 0 and F, and the result is looked up in a 256-entry table giving the new noise note. The semitone
// within the octave plays no part. Table values measured by loading old songs through LSDJ 9.4.2.
const uint8_t kOldNoise[256] = {68,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,61,62,63,64,66,68,72,76,1,2,3,4,6,8,12,16,65,66,67,68,70,72,76,80,5,6,7,8,10,12,16,20,69,70,71,72,74,76,80,84,9,10,11,12,14,16,20,24,73,74,75,76,78,80,84,88,13,14,15,16,18,20,24,28,77,78,79,80,82,84,88,92,17,18,19,20,22,24,28,32,81,82,83,84,86,88,92,96,21,22,23,24,26,28,32,36,85,86,87,88,90,92,96,100,25,26,27,28,30,32,36,40,89,90,91,92,94,96,100,104,29,30,31,32,34,36,40,44,93,94,95,96,98,100,104,108,33,34,35,36,38,40,44,48,97,98,99,100,102,104,108,112,37,38,39,40,42,44,48,52,101,102,103,104,106,108,112,116,41,42,43,44,46,48,52,56,105,106,107,108,110,112,116,118,45,46,47,48,50,52,56,58,109,110,111,112,114,116,118,119,49,50,51,52,54,56,58,59,113,114,115,116,117,118,119,120,53,54,55,56,57,58,59,60};
int newNoiseNote(int oldNote, uint8_t b4) {
    int t = (oldNote - 1) / 12 - 3;
    uint8_t v = b4;
    for (; t < 0; ++t) if (v & 0xF0) v = uint8_t(v - 0x10);
    for (; t > 0; --t) if ((v & 0xF0) != 0xF0) v = uint8_t(v + 0x10);
    return kOldNoise[v];
}
bool retrigShifts(uint8_t v) { return (v & 15) != 15 && (v >> 4) != 8; }   // R8y is the fast mode and stays
bool isOldDefaultPulse(const uint8_t* r) {
    return r[0] == 0 && r[2] == 0x88 && r[4] == 0 && r[5] == 0x7F && r[6] == 1 && r[8] == 3;
}
}  // namespace

bool upgradeSong(Song& s) {
    uint8_t* m = s.mem();
    const uint8_t ver = m[0x7FFF];
    if (ver == kFormatVersion) return true;
    if (ver < 7 || ver > kFormatVersion) return false;

    if (ver < 9) {   // version 7: no B command yet, every command from B on moved up one letter
        for (int i = 0; i < 255 * 16; ++i) if (m[off::kPhraseCmd + i] >= 2) ++m[off::kPhraseCmd + i];
        for (int i = 0; i < 32 * 16; ++i) {
            if (m[off::kTblCmd1 + i] >= 2) ++m[off::kTblCmd1 + i];
            if (m[off::kTblCmd2 + i] >= 2) ++m[off::kTblCmd2 + i];
        }
    }
    // noise notes first (they need the instrument bytes before anything else changes)
    for (int p = 0; p < kPhrases; ++p)
        for (int st = 0; st < 16; ++st) {
            const uint8_t ins = s.phraseInstr(p, st);
            uint8_t& note = s.note(p, st);
            if (!note || ins == 0xFF || (s.instrument(ins & 63)[0] & 3) != 3) continue;
            note = uint8_t(newNoiseNote(note, s.instrument(ins & 63)[4]));
        }
    // R (retrigger): the period digit counts from 1 now (15 = once stays)
    for (int i = 0; i < 255 * 16; ++i)
        if (m[off::kPhraseCmd + i] == 14 && retrigShifts(m[off::kPhraseVal + i])) ++m[off::kPhraseVal + i];
    for (int i = 0; i < 32 * 16; ++i) {
        if (m[off::kTblCmd1 + i] == 14 && retrigShifts(m[off::kTblVal1 + i])) ++m[off::kTblVal1 + i];
        if (m[off::kTblCmd2 + i] == 14 && retrigShifts(m[off::kTblVal2 + i])) ++m[off::kTblVal2 + i];
    }
    for (int j = 0; j < kInstruments; ++j) {
        uint8_t* in = s.instrument(j);
        const int type = in[0] & 3;
        if (type == 0 && isOldDefaultPulse(in)) {
            static const uint8_t def[16] = {0x00, 0xA0, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x03, 0x00, 0x00, 0xD0, 0x00, 0x00, 0x00, 0xF3, 0x00};
            std::memcpy(in, def, 16);
            continue;
        }
        if (type == 0 || type == 3) {
            if ((in[1] & 15) >= 12) { in[1] = uint8_t((in[1] & 0xF0) | ((in[1] & 15) - 3)); if (in[9] == 0) in[9] = 0xF0; }   // the slowest old rates: rate - 3 and the second stage held (measured, byte 9 = F0)
            else in[1] = uint8_t((in[1] & 0xF0) | releaseNibble(in[1] & 15));
            if (in[9] != 0xF0) in[9] = uint8_t((in[9] & 0xF0) | releaseNibble(in[9] & 15));     // the second release digits (bytes 9 and 10) convert alike
            in[10] = uint8_t((in[10] & 0xF0) | releaseNibble(in[10] & 15));
        }
        if (type == 0) in[11] = uint8_t((in[7] & 0x3C) << 1);               // fine tune used to hide in the pan/duty byte
        if (type == 1) {                                   // wave: first frame = synth x 16, play modes renumbered
            const uint8_t oldB2 = in[2];
            if (ver < 9) { const uint8_t lo = in[2] & 15; in[2] = uint8_t((in[2] & 0xF0) | (15 - lo)); }   // version 7: the low nibble is inverted (0<->F, 1<->E ... measured on 4 values)
            in[3] = uint8_t(oldB2 & 0xF0);
            if (ver < 0xA) in[9] = uint8_t((in[9] + 1) & 3);
        }
    }
    // 0x1D00-0x1E77 holds text tables that moved between versions (LSDJ's own upgrade leaves junk there): use a fresh song's.
    Song fresh;
    std::memcpy(m + 0x1D00, fresh.mem() + 0x1D00, 0x178);
    m[0x3FB6] = m[0x3FB7] = m[0x3FB8] = 0;   // old work-time bytes
    m[0x7FFF] = kFormatVersion;
    return true;
}

}  // namespace gb
