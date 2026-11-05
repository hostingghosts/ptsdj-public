#include "gb/ui/lsdj.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace gb {

const char* noteName(int n, char out[4]) {
    if (n <= 0) { std::strcpy(out, "---"); return out; }
    static const char* nm[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    const int k = n - 1;
    out[0] = nm[k % 12][0]; out[1] = nm[k % 12][1]; out[2] = char('0' + (k / 12 + 2) % 10); out[3] = 0;
    return out;
}

char cmdLetter(int c) {
    static const char L[] = "-ABCDEFGHKLMOPRSTVWZ";
    return c >= 0 && c < int(sizeof L) - 1 ? L[c] : '?';
}

namespace {
constexpr int kCmdCount = 20;
int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
struct Field { const char* label; int row, byte, mask, shift, kind; const char* const* names; };
const char* const kDuty[] = {"12.5", "25", "50", "75"};
const char* const kOut[] = {"OFF", "L", "R", "LR"};
const char* const kTune[] = {"12T", "FIX", "DRM", "12T"};
const char* const kOnOff[] = {"OFF", "ON"};
const char* const kTypes[] = {"PULSE", "WAVE", "KIT", "NOISE"};
// kind: 0 hex, 1 enum, 2 length, 3 table, 4 sweep (xor 80)
const Field kPulse[] = {{"ENVELOPE", 5, 1, 0xFF, 0, 0, nullptr}, {"WAVE", 6, 7, 0xC0, 6, 1, kDuty}, {"OUTPUT", 7, 7, 3, 0, 1, kOut},
                        {"LENGTH", 8, 3, 0x7F, 0, 2, nullptr}, {"SWEEP", 9, 4, 0xFF, 0, 4, nullptr}, {"P+L+VIB", 10, 5, 0xFF, 0, 0, nullptr},
                        {"TUNING", 11, 8, 3, 0, 1, kTune}, {"PU2 TUNE", 13, 2, 0xFF, 0, 0, nullptr}, {"PU FINE", 14, 11, 0xFF, 0, 0, nullptr},
                        {"AUTOMATE", 16, 9, 0x80, 7, 1, kOnOff}, {"TABLE", 17, 6, 0x3F, 0, 3, nullptr}};
const Field kWave[] = {{"VOLUME", 5, 1, 0xFF, 0, 0, nullptr}, {"SYNTH", 6, 2, 0x0F, 0, 0, nullptr}, {"OUTPUT", 7, 7, 3, 0, 1, kOut},
                       {"LENGTH", 8, 3, 0x7F, 0, 2, nullptr}, {"P+L+VIB", 10, 5, 0xFF, 0, 0, nullptr}, {"TUNING", 11, 8, 3, 0, 1, kTune},
                       {"AUTOMATE", 16, 9, 0x80, 7, 1, kOnOff}, {"TABLE", 17, 6, 0x3F, 0, 3, nullptr}};
const Field kKit[] = {{"KIT 1", 5, 1, 0xFF, 0, 0, nullptr}, {"KIT 2", 6, 2, 0xFF, 0, 0, nullptr}, {"OUTPUT", 7, 7, 3, 0, 1, kOut},
                      {"LENGTH", 8, 3, 0x7F, 0, 2, nullptr}, {"P+L+VIB", 10, 5, 0xFF, 0, 0, nullptr}, {"TUNING", 11, 8, 3, 0, 1, kTune},
                      {"AUTOMATE", 16, 9, 0x80, 7, 1, kOnOff}, {"TABLE", 17, 6, 0x3F, 0, 3, nullptr}};
const Field kNoise[] = {{"ENVELOPE", 5, 1, 0xFF, 0, 0, nullptr}, {"SHAPE", 6, 2, 0xFF, 0, 0, nullptr}, {"OUTPUT", 7, 7, 3, 0, 1, kOut},
                        {"LENGTH", 8, 3, 0x7F, 0, 2, nullptr}, {"AUTOMATE", 16, 9, 0x80, 7, 1, kOnOff}, {"TABLE", 17, 6, 0x3F, 0, 3, nullptr}};
struct FieldSet { const Field* f; int n; };
FieldSet fieldsFor(int type) {
    switch (type & 3) {
        case 0: return {kPulse, int(sizeof kPulse / sizeof *kPulse)};
        case 1: return {kWave, int(sizeof kWave / sizeof *kWave)};
        case 2: return {kKit, int(sizeof kKit / sizeof *kKit)};
        default: return {kNoise, int(sizeof kNoise / sizeof *kNoise)};
    }
}
}  // namespace

int Lsdj::instrRows() const { return 2 + fieldsFor(s_.instrument(instr)[0]).n; }   // name, type, fields

// ---- input ----------------------------------------------------------------------------------------------------------

void Lsdj::key(Key k, bool down) {
    if (k == Key::A) { if (down && a_) return; if (down) { a_ = true; edited_ = false; if (b_) { cut(); edited_ = true; } } else { if (a_ && !edited_ && !b_ && !sel_) press(Key::A); a_ = false; } return; }
    if (k == Key::B) { b_ = down; return; }
    if (k == Key::Select) { sel_ = down; return; }
    if (!down) return;
    if (k == Key::Start) {
        if (p_.running()) p_.stop(); else { p_.start(scr_ == Scr::Song ? songRow : 0); }
        return;
    }
    if (sel_) { navigate(k); return; }
    if (a_) { edited_ = true; editKey(k); return; }
    if (b_) { changeId(k == Key::Left || k == Key::Up ? -1 : 1); if (k == Key::Up || k == Key::Down) changeId(0); return; }
    move(k);
}

void Lsdj::encoder(int delta) {
    if (!delta) return;
    const Key k = sel_ ? (delta > 0 ? Key::Up : Key::Down) : (delta > 0 ? Key::Right : Key::Left);
    for (int i = 0, n = delta < 0 ? -delta : delta; i < n; ++i) editKey(k);
}

void Lsdj::padNote(int pad) {
    if (scr_ != Scr::Phrase) return;
    s_.note(phrase, step) = uint8_t(clampi(13 + pad, 1, 127));
    lastNote_ = s_.note(phrase, step);
    if (s_.phraseInstr(phrase, step) == 0xFF) s_.phraseInstr(phrase, step) = lastInstr_;
    s_.markPhrase(phrase); s_.markInstrument(s_.phraseInstr(phrase, step) & 63);
    step = clampi(step + 1, 0, 15);
}

void Lsdj::navigate(Key k) {
    // screen map: Project Wave Synth over Song Chain Phrase Instr Table over Groove
    int col = -1, row = 1;
    switch (scr_) {
        case Scr::Project: row = 0; col = 0; break; case Scr::Wave: row = 0; col = 2; break; case Scr::Synth: row = 0; col = 3; break;
        case Scr::Song: col = 0; break; case Scr::Chain: col = 1; break; case Scr::Phrase: col = 2; break;
        case Scr::Instr: col = 3; break; case Scr::Table: col = 4; break; case Scr::Groove: row = 2; col = 2; break;
    }
    static const Scr main[5] = {Scr::Song, Scr::Chain, Scr::Phrase, Scr::Instr, Scr::Table};
    if (row == 1) {
        if (k == Key::Right && col < 4) {
            if (scr_ == Scr::Song) { const int c = s_.songCell(songRow, songCh); if (c != 0xFF) { chain = c; } }
            if (scr_ == Scr::Chain) { const int ph = s_.chainPhrase(chain, chainPos); if (ph != 0xFF) phrase = ph; }
            if (scr_ == Scr::Phrase) { const int i = s_.phraseInstr(phrase, step); if (i != 0xFF) instr = i & 63; }
            if (scr_ == Scr::Instr) { const uint8_t t = s_.instrument(instr)[6]; if (t & 0x20) table = t & 0x1F; }
            scr_ = main[col + 1];
        } else if (k == Key::Left && col > 0) scr_ = main[col - 1];
        else if (k == Key::Up) scr_ = col <= 1 ? Scr::Project : col == 2 ? Scr::Wave : Scr::Synth;
        else if (k == Key::Down) scr_ = Scr::Groove;
    } else if (row == 0) {
        if (k == Key::Down) scr_ = col == 0 ? Scr::Song : col == 2 ? Scr::Phrase : Scr::Instr;
        else if (k == Key::Right) scr_ = col == 0 ? Scr::Wave : Scr::Synth;
        else if (k == Key::Left && col > 0) scr_ = col == 3 ? Scr::Wave : Scr::Project;
    } else if (k == Key::Up) scr_ = Scr::Phrase;
}

void Lsdj::move(Key k) {
    const int dx = k == Key::Right ? 1 : k == Key::Left ? -1 : 0, dy = k == Key::Down ? 1 : k == Key::Up ? -1 : 0;
    switch (scr_) {
        case Scr::Song: songCh = clampi(songCh + dx, 0, 3); songRow = clampi(songRow + dy, 0, 255); break;
        case Scr::Chain: chainCol = clampi(chainCol + dx, 0, 1); chainPos = clampi(chainPos + dy, 0, 15); break;
        case Scr::Phrase: phraseCol = clampi(phraseCol + dx, 0, 3); step = clampi(step + dy, 0, 15); break;
        case Scr::Instr: instrRow = clampi(instrRow + dy, 0, instrRows() - 1); break;
        case Scr::Table: tableCol = clampi(tableCol + dx, 0, 5); tableRow = clampi(tableRow + dy, 0, 15); break;
        case Scr::Groove: grooveStep = clampi(grooveStep + dy, 0, 15); break;
        case Scr::Project: projRow = clampi(projRow + dy, 0, 9); break;
        default: break;
    }
}

// Value editing: a plain A on an empty cell inserts the last value used; A with a direction changes it (+-1, +-16 / octave).
void Lsdj::press(Key) {
    switch (scr_) {
        case Scr::Song: { uint8_t& c = s_.songCell(songRow, songCh); if (c == 0xFF) { c = lastChain_; s_.markChain(c); } break; }
        case Scr::Chain:
            if (chainCol == 0) { uint8_t& p = s_.chainPhrase(chain, chainPos); if (p == 0xFF) { p = lastPhrase_; s_.markPhrase(p); } }
            break;
        case Scr::Phrase:
            if (phraseCol == 0) { uint8_t& n = s_.note(phrase, step); if (!n) { n = lastNote_; if (s_.phraseInstr(phrase, step) == 0xFF) s_.phraseInstr(phrase, step) = lastInstr_; s_.markPhrase(phrase); } }
            else if (phraseCol == 1) { uint8_t& i = s_.phraseInstr(phrase, step); if (i == 0xFF) i = lastInstr_; }
            else if (phraseCol == 2 && !s_.phraseCmd(phrase, step)) { s_.phraseCmd(phrase, step) = lastCmd_ ? lastCmd_ : 1; s_.phraseVal(phrase, step) = lastVal_; }
            break;
        default: break;
    }
}

void Lsdj::editKey(Key k) {
    const int big = (k == Key::Up || k == Key::Down), sgn = (k == Key::Right || k == Key::Up) ? 1 : -1;
    const int d = sgn * (big ? 16 : 1);
    switch (scr_) {
        case Scr::Song: {
            uint8_t& c = s_.songCell(songRow, songCh);
            if (c == 0xFF) c = lastChain_; else c = uint8_t(clampi(c + d, 0, kChains - 1));
            lastChain_ = c; s_.markChain(c); break;
        }
        case Scr::Chain:
            if (chainCol == 0) {
                uint8_t& p = s_.chainPhrase(chain, chainPos);
                if (p == 0xFF) p = lastPhrase_; else p = uint8_t(clampi(p + d, 0, kPhrases - 1));
                lastPhrase_ = p; s_.markPhrase(p);
            } else s_.chainTrans(chain, chainPos) = uint8_t(s_.chainTrans(chain, chainPos) + d);
            break;
        case Scr::Phrase: {
            if (phraseCol == 0) {
                uint8_t& n = s_.note(phrase, step);
                if (!n) n = lastNote_; else n = uint8_t(clampi(n + (big ? sgn * 12 : sgn), 1, 127));
                lastNote_ = n; if (s_.phraseInstr(phrase, step) == 0xFF) s_.phraseInstr(phrase, step) = lastInstr_; s_.markPhrase(phrase);
            } else if (phraseCol == 1) {
                uint8_t& i = s_.phraseInstr(phrase, step);
                if (i == 0xFF) i = lastInstr_; else i = uint8_t(clampi(i + d, 0, kInstruments - 1));
                lastInstr_ = i; s_.markInstrument(i);
            } else if (phraseCol == 2) {
                uint8_t& c = s_.phraseCmd(phrase, step);
                c = uint8_t(clampi(c + sgn, 0, kCmdCount - 1)); if (c) lastCmd_ = c;
            } else { uint8_t& v = s_.phraseVal(phrase, step); v = uint8_t(v + d); lastVal_ = v; }
            break;
        }
        case Scr::Instr: {
            uint8_t* in = s_.instrument(instr);
            if (instrRow == 0) break;
            if (instrRow == 1) { in[0] = uint8_t((in[0] + sgn) & 3); s_.markInstrument(instr); break; }
            const FieldSet fs = fieldsFor(in[0]);
            const Field& f = fs.f[instrRow - 2];
            int v = (in[f.byte] & f.mask) >> f.shift;
            const int maxv = f.mask >> f.shift;
            if (f.kind == 4) { v = ((in[f.byte] ^ 0x80) & 0xFF); v = (v + d) & 0xFF; in[f.byte] = uint8_t(v ^ 0x80); break; }
            if (f.kind == 3) {                         // table: off, or 0x20 | n
                int t = (in[f.byte] & 0x20) ? (in[f.byte] & 0x1F) : -1;
                t = clampi(t + d, -1, kTables - 1);
                in[f.byte] = t < 0 ? 0 : uint8_t(0x20 | t);
                break;
            }
            if (f.kind == 2) {                         // length: unlimited, or 0..3F with the enable bit
                int len = (in[f.byte] & 0x40) ? (in[f.byte] & 0x3F) : -1;
                len = clampi(len + d, -1, 0x3F);
                in[f.byte] = len < 0 ? uint8_t(in[f.byte] & ~0x7F) : uint8_t((in[f.byte] & ~0x7F) | 0x40 | len);
                break;
            }
            v = f.kind == 1 ? clampi(v + sgn, 0, maxv) : clampi(v + d, 0, maxv);
            in[f.byte] = uint8_t((in[f.byte] & ~f.mask) | (v << f.shift));
            break;
        }
        case Scr::Table: {
            const int off16[6] = {off::kTblEnv, off::kTblTrans, off::kTblCmd1, off::kTblVal1, off::kTblCmd2, off::kTblVal2};
            // columns: 0 vol, 1 transpose, 2 cmd1, 3 val1, 4 cmd2, 5 val2
            uint8_t& v = s_.at(uint16_t(off16[tableCol] + table * 16 + tableRow));
            if (tableCol == 2 || tableCol == 4) v = uint8_t(clampi(v + sgn, 0, kCmdCount - 1)); else v = uint8_t(v + d);
            break;
        }
        case Scr::Groove: { uint8_t& v = s_.groove(groove)[grooveStep]; v = uint8_t(clampi(v + d, 0, 255)); break; }
        case Scr::Project:
            if (projRow == 0) { uint8_t& t = s_.at(off::kTempo); t = uint8_t(clampi(t + d, 40, 255)); p_.setTempo(t); }
            else if (projRow == 1) s_.at(off::kTranspose) = uint8_t(int8_t(s_.at(off::kTranspose)) + d);
            break;
        default: break;
    }
}

void Lsdj::cut() {
    switch (scr_) {
        case Scr::Song: { uint8_t& c = s_.songCell(songRow, songCh); if (c != 0xFF) lastChain_ = c; c = 0xFF; break; }
        case Scr::Chain: s_.chainPhrase(chain, chainPos) = 0xFF; s_.chainTrans(chain, chainPos) = 0; break;
        case Scr::Phrase:
            if (phraseCol == 0) s_.note(phrase, step) = 0;
            else if (phraseCol == 1) s_.phraseInstr(phrase, step) = 0xFF;
            else { s_.phraseCmd(phrase, step) = 0; s_.phraseVal(phrase, step) = 0; }
            break;
        case Scr::Table: {
            const int off16[6] = {off::kTblEnv, off::kTblTrans, off::kTblCmd1, off::kTblVal1, off::kTblCmd2, off::kTblVal2};
            s_.at(uint16_t(off16[tableCol] + table * 16 + tableRow)) = 0;
            break;
        }
        default: break;
    }
}

void Lsdj::changeId(int d) {
    switch (scr_) {
        case Scr::Chain: chain = clampi(chain + d, 0, kChains - 1); break;
        case Scr::Phrase: if (d) { phrase = clampi(phrase + d, 0, kPhrases - 1); } break;
        case Scr::Instr: instr = clampi(instr + d, 0, kInstruments - 1); instrRow = std::min(instrRow, instrRows() - 1); break;
        case Scr::Table: table = clampi(table + d, 0, kTables - 1); break;
        case Scr::Groove: groove = clampi(groove + d, 0, kGrooves - 1); break;
        default: break;
    }
}

// ---- drawing --------------------------------------------------------------------------------------------------------

void Lsdj::drawPanel(Lcd& l) const {
    l.rect(128, 0, 32, 112, 1);
    static const char* ch[4] = {"PU1", "PU2", "WAV", "NOI"};
    l.rect(128, 0, 32, 8, 3);
    l.text(16, 0, ch[scr_ == Scr::Song ? songCh : (scr_ == Scr::Project ? 0 : songCh)], true);
    char b[8];
    l.glyphAt(128, 24, '\x01', false, 3, 1);
    std::snprintf(b, sizeof b, "%d", s_.at(off::kTempo));
    for (int i = 0; b[i]; ++i) l.glyphAt(136 + i * 8, 24, b[i], false, 3, 1);
    static const char lab[4] = {'1', '2', 'W', 'N'};
    for (int c = 0; c < 4; ++c) l.glyphAt(136, 40 + c * 8, lab[c], p_.voiceOn(c), p_.voiceOn(c) ? 0 : 3, p_.voiceOn(c) ? 3 : 1);
    // screen map
    auto mark = [&](int col, int row, char c, Scr s) { l.glyph(col, row, c, scr_ == s); };
    l.rect(120, 112, 40, 32, 0);
    mark(15, 14, 'P', Scr::Project); mark(17, 14, 'W', Scr::Wave); mark(18, 14, 'S', Scr::Synth);
    mark(15, 16, 'S', Scr::Song); mark(16, 16, 'C', Scr::Chain); mark(17, 16, 'P', Scr::Phrase); mark(18, 16, 'I', Scr::Instr); mark(19, 16, 'T', Scr::Table);
    mark(17, 17, 'G', Scr::Groove);
}

void Lsdj::draw(Lcd& l) const {
    l.clear(0);
    switch (scr_) {
        case Scr::Song: drawSong(l); break;
        case Scr::Chain: drawChain(l); break;
        case Scr::Phrase: drawPhrase(l); break;
        case Scr::Instr: drawInstr(l); break;
        case Scr::Table: drawTable(l); break;
        case Scr::Groove: drawGroove(l); break;
        case Scr::Project: drawProject(l); break;
        case Scr::Wave: l.text(0, 0, "WAVE"); l.text(2, 5, "NOT YET"); break;
        case Scr::Synth: l.text(0, 0, "SYNTH 00-0F"); l.text(2, 5, "NOT YET"); break;
    }
    if (scr_ != Scr::Project) drawPanel(l); else { drawPanel(l); }
}

void Lsdj::drawSong(Lcd& l) const {
    l.text(0, 0, "SONG");
    static const char* ch[4] = {"PU1", "PU2", "WAV", "NOI"};
    for (int c = 0; c < 4; ++c) l.text(0, 1, "", false), [&] { for (int i = 0; i < 3; ++i) l.glyphAt(18 + c * 26 + i * 8 - 2, 8, ch[c][i], false, 2, 0); }();
    const int base = std::max(0, std::min(songRow - 8, 256 - 16));
    for (int r = 0; r < 16; ++r) {
        const int row = base + r;
        l.hex2(0, 2 + r, row);
        for (int c = 0; c < 4; ++c) {
            const uint8_t v = s_.songCell(row, c);
            const bool cur = row == songRow && c == songCh;
            char t[3];
            if (v == 0xFF) std::strcpy(t, "--"); else { t[0] = "0123456789ABCDEF"[v >> 4]; t[1] = "0123456789ABCDEF"[v & 15]; t[2] = 0; }
            for (int i = 0; i < 2; ++i) l.glyphAt(22 + c * 26 + i * 8, 16 + r * 8, t[i], cur, 3, 0);
        }
        for (int c = 0; c < 4; ++c) if (p_.running() && p_.seq().active(c) && p_.seq().row(c) == row) l.glyphAt(14 + c * 26, 16 + r * 8, '\x05', false, 3, 0);
    }
}

void Lsdj::drawChain(Lcd& l) const {
    char t[16]; std::snprintf(t, sizeof t, "CHAIN %02X", chain); l.text(0, 0, t);
    l.glyphAt(40, 8, 'P', false, 2, 0); l.glyphAt(48, 8, 'A', false, 2, 0); l.glyphAt(56, 8, 'T', false, 2, 0);
    l.glyphAt(88, 8, 'T', false, 2, 0); l.glyphAt(96, 8, 'S', false, 2, 0); l.glyphAt(104, 8, 'P', false, 2, 0);
    for (int r = 0; r < 16; ++r) {
        l.glyph(0, 2 + r, "0123456789ABCDEF"[r]);
        const uint8_t p = s_.chainPhrase(chain, r);
        const char hx[] = "0123456789ABCDEF";
        char a[3] = {p == 0xFF ? '-' : hx[p >> 4], p == 0xFF ? '-' : hx[p & 15], 0};
        const uint8_t tr = s_.chainTrans(chain, r);
        char b[3] = {hx[tr >> 4], hx[tr & 15], 0};
        for (int i = 0; i < 2; ++i) {
            l.glyphAt(40 + i * 8, 16 + r * 8, a[i], r == chainPos && chainCol == 0, 3, 0);
            l.glyphAt(88 + i * 8, 16 + r * 8, b[i], r == chainPos && chainCol == 1, 3, 0);
        }
    }
    for (int c = 0; c < 4; ++c) if (p_.running() && p_.seq().active(c) && p_.seq().chain(c) == chain) l.glyphAt(24, 16 + p_.seq().chainPos(c) * 8, '\x05', false, 3, 0);
}

void Lsdj::drawPhrase(Lcd& l) const {
    char t[16]; std::snprintf(t, sizeof t, "PHRASE %02X", phrase); l.text(0, 0, t);
    const char* hd[3] = {"NOTE", "INSTR", "CMD"}; const int hx0[3] = {16, 56, 88};
    for (int h = 0; h < 3; ++h) for (int i = 0; hd[h][i]; ++i) l.glyphAt(hx0[h] + i * 6 - 2, 8, hd[h][i], false, 2, 0);
    const char hx[] = "0123456789ABCDEF";
    for (int r = 0; r < 16; ++r) {
        l.glyph(0, 2 + r, hx[r]);
        char nn[4]; noteName(s_.note(phrase, r), nn);
        for (int i = 0; i < 3; ++i) l.glyphAt(16 + i * 8, 16 + r * 8, nn[i], r == step && phraseCol == 0, 3, 0);
        const uint8_t in = s_.phraseInstr(phrase, r);
        char ib[3] = {in == 0xFF ? '-' : hx[(in >> 4) & 15], in == 0xFF ? '-' : hx[in & 15], 0};
        for (int i = 0; i < 2; ++i) l.glyphAt(56 + i * 8, 16 + r * 8, ib[i], r == step && phraseCol == 1, 3, 0);
        const uint8_t c = s_.phraseCmd(phrase, r), v = s_.phraseVal(phrase, r);
        l.glyphAt(88, 16 + r * 8, cmdLetter(c), r == step && phraseCol == 2, 3, 0);
        l.glyphAt(96, 16 + r * 8, hx[v >> 4], r == step && phraseCol == 3, 3, 0);
        l.glyphAt(104, 16 + r * 8, hx[v & 15], r == step && phraseCol == 3, 3, 0);
    }
    for (int c = 0; c < 4; ++c) if (p_.running() && p_.seq().active(c) && p_.seq().phrase(c) == phrase) l.glyphAt(8, 16 + p_.seq().step(c) * 8, '\x05', false, 3, 0);
}

void Lsdj::drawTable(Lcd& l) const {
    char t[16]; std::snprintf(t, sizeof t, "TABLE %02X", table); l.text(0, 0, t);
    const char* hd[4] = {"VOL", "TSP", "CMD", "CMD"}; const int hx0[4] = {12, 36, 60, 88};
    for (int h = 0; h < 4; ++h) for (int i = 0; hd[h][i]; ++i) l.glyphAt(hx0[h] + i * 6 - 2, 8, hd[h][i], false, 2, 0);
    const char hx[] = "0123456789ABCDEF";
    const int o[6] = {off::kTblEnv, off::kTblTrans, off::kTblCmd1, off::kTblVal1, off::kTblCmd2, off::kTblVal2};
    for (int r = 0; r < 16; ++r) {
        l.glyph(0, 2 + r, hx[r]);
        auto cell = [&](int col) { return s_.at(uint16_t(o[col] + table * 16 + r)); };
        auto two = [&](int x, int col) { const uint8_t v = cell(col); for (int i = 0; i < 2; ++i) l.glyphAt(x + i * 8, 16 + r * 8, hx[i ? v & 15 : v >> 4], r == tableRow && tableCol == col, 3, 0); };
        two(12, 0); two(36, 1);
        l.glyphAt(60, 16 + r * 8, cmdLetter(cell(2)), r == tableRow && tableCol == 2, 3, 0); two(68, 3);
        l.glyphAt(88, 16 + r * 8, cmdLetter(cell(4)), r == tableRow && tableCol == 4, 3, 0); two(96, 5);
    }
}

void Lsdj::drawGroove(Lcd& l) const {
    char t[16]; std::snprintf(t, sizeof t, "GROOVE %02X", groove); l.text(0, 0, t);
    for (int r = 0; r < 16; ++r) {
        l.glyph(0, 2 + r, "0123456789ABCDEF"[r]);
        l.hex2(3, 2 + r, s_.groove(groove)[r], r == grooveStep);
    }
}

void Lsdj::drawProject(Lcd& l) const {
    l.text(0, 0, "PROJECT (NEW)");
    char b[24];
    auto row = [&](int y, const char* name, const char* val, bool cur) { l.text(0, y, name); l.text(9, y, val, cur); };
    std::snprintf(b, sizeof b, "%02X", s_.at(off::kTempo)); row(2, "TEMPO", b, projRow == 0);
    std::snprintf(b, sizeof b, "%02X", s_.at(off::kTranspose)); row(3, "TRANSPOSE", b, projRow == 1);
    row(4, "SYNC", "OFF", projRow == 2);
    row(6, "CLONE", "DEEP", projRow == 3);
    row(7, "LOOK", "92K/GRAY", projRow == 4);
    row(8, "KEY DELAY/REPEAT", "", false);
    row(9, "PRELISTEN", "ON", projRow == 5);
    row(11, "HELP", "", projRow == 6);
    row(12, "CLEAN SONG DATA", "", projRow == 7);
    row(13, "CLEAN INSTR DATA", "", projRow == 8);
    row(14, "LOAD/SAVE SONG", "", projRow == 9);
    l.text(0, 16, "WORK TIME: 0:00");
    l.text(0, 17, "TOTAL: 0:00");
}

void Lsdj::drawInstr(Lcd& l) const {
    char t[24]; std::snprintf(t, sizeof t, "INSTRUMENT %02X", instr); l.text(0, 0, t);
    const uint8_t* in = s_.instrument(instr);
    l.text(0, 2, "NAME"); l.text(5, 2, "          ", instrRow == 0);
    l.text(0, 3, "TYPE"); l.text(9, 3, kTypes[in[0] & 3], instrRow == 1);
    const FieldSet fs = fieldsFor(in[0]);
    const char hx[] = "0123456789ABCDEF";
    for (int i = 0; i < fs.n; ++i) {
        const Field& f = fs.f[i];
        l.text(0, f.row, f.label);
        char v[12];
        const int raw = (in[f.byte] & f.mask) >> f.shift;
        switch (f.kind) {
            case 1: std::snprintf(v, sizeof v, "%s", f.names[raw]); break;
            case 2: if (in[f.byte] & 0x40) std::snprintf(v, sizeof v, "%02X", in[f.byte] & 0x3F); else std::strcpy(v, "UNLIM"); break;
            case 3: if (in[f.byte] & 0x20) std::snprintf(v, sizeof v, "%02X", in[f.byte] & 0x1F); else std::strcpy(v, "OFF"); break;
            case 4: std::snprintf(v, sizeof v, "%02X", in[f.byte] ^ 0x80); break;
            default: v[0] = hx[(raw >> 4) & 15]; v[1] = hx[raw & 15]; v[2] = 0; break;
        }
        l.text(9, f.row, v, instrRow == 2 + i);
    }
}

}  // namespace gb
