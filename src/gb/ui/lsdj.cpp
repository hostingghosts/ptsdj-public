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
    if (k == Key::A) { if (down && a_) return; if (down) { a_ = true; edited_ = false; if (sel_ && scr_ == Scr::Song) { p_.mute(songCh, !p_.muted(songCh)); edited_ = true; } else if (sel_ && scr_ == Scr::Wave) { if (hasClip_) { for (int i = 0; i < 16; ++i) s_.wave(waveNo)[i] = waveClip_[i]; } edited_ = true; } else if (b_) { cut(); edited_ = true; } } else { if (a_ && !edited_ && !b_ && !sel_) press(Key::A); a_ = false; } return; }
    if (k == Key::B) {
        if (down) {
            bUsed_ = false;
            if (sel_ && scr_ == Scr::Song) {                  // Shift + Delete: mute this whole console, or bring it all back
                bool any = false;
                for (int c = 0; c < 4; ++c) any = any || !p_.muted(c);
                for (int c = 0; c < 4; ++c) p_.mute(c, any);
                bUsed_ = true;
            }
        }
        else if (b_ && !bUsed_ && !sel_ && scr_ == Scr::File) scr_ = Scr::Project;
        else if (b_ && !bUsed_ && !sel_ && scr_ == Scr::Wave) {          // a lone B tap copies the wave
            for (int i = 0; i < 16; ++i) waveClip_[i] = s_.wave(waveNo)[i];
            hasClip_ = true;
        }
        b_ = down;
        return;
    }
    if (k == Key::Select) { sel_ = down; return; }
    if (!down) return;
    if (k == Key::Start) {
        if (playing()) rec_ = false;
        if (tr_) { if (tr_->running()) tr_->stop(); else tr_->start(console_, startRow()); }
        else if (p_.running()) p_.stop(); else p_.start(startRow());
        return;
    }
    if (sel_) { navigate(k); return; }
    if (a_) { edited_ = true; editKey(k); return; }
    if (b_) {
        bUsed_ = true;
        if (scr_ == Scr::Wave) { waveNo = clampi(waveNo + (k == Key::Left ? -1 : k == Key::Right ? 1 : k == Key::Up ? 16 : -16), 0, 255); return; }
        if (scr_ == Scr::Synth) { synth = clampi(synth + (k == Key::Left || k == Key::Up ? -1 : 1), 0, kSynths - 1); return; }
        changeId(k == Key::Left || k == Key::Up ? -1 : 1); if (k == Key::Up || k == Key::Down) changeId(0); return;
    }
    move(k);
}

void Lsdj::encoder(int delta) {
    if (!delta) return;
    const Key k = sel_ ? (delta > 0 ? Key::Up : Key::Down) : (delta > 0 ? Key::Right : Key::Left);
    for (int i = 0, n = delta < 0 ? -delta : delta; i < n; ++i) editKey(k);
}

bool Lsdj::playing() const { return tr_ ? tr_->running() : p_.running(); }

int Lsdj::curInstr() const {
    if (scr_ == Scr::Phrase) { const uint8_t i = s_.phraseInstr(phrase, step); return i != 0xFF ? (i & 63) : lastInstr_; }
    return instr;
}

// Song, chain and phrase pads play the cursor channel; the instrument screens play the instrument's own channel type.
int Lsdj::previewChannel() const {
    if (scr_ == Scr::Wave || scr_ == Scr::Synth) return 2;
    if (scr_ == Scr::Song || scr_ == Scr::Chain || scr_ == Scr::Phrase) return songCh;
    switch (s_.instrument(curInstr())[0] & 3) {
        case 0: return songCh < 2 ? songCh : 0;
        case 1: case 2: return 2;
        default: return 3;
    }
}

// Records a played note into the phrase the channel is playing: the nearest step (the upcoming one in the last half of a step).
void Lsdj::recordNote(int ch, int note) {
    const Seq& sq = p_.seq();
    if (!sq.active(ch)) return;
    const int ph = sq.phrase(ch);
    int st = sq.step(ch);
    if (st > 0 && sq.wait(ch) > 3) --st;
    if (ph == 0xFF) return;
    s_.note(ph, st) = uint8_t(note);
    s_.phraseInstr(ph, st) = uint8_t(curInstr());
    s_.markPhrase(ph);
    s_.markInstrument(curInstr());
}

void Lsdj::padDown(int pad) {
    if (pad < 0 || pad >= 48) return;
    const int ch = previewChannel();
    int ins = curInstr();
    int note;
    const bool scratch = scr_ == Scr::Wave || scr_ == Scr::Synth;
    if (scratch) {                                    // preview the wave being edited: a scratch wave instrument pointing at it
        uint8_t* sc = p_.scratchInstr();
        for (int i = 0; i < 16; ++i) sc[i] = 0;
        sc[0] = 1; sc[1] = 0xA8; sc[2] = 0x0F; sc[7] = 0x03;
        if (scr_ == Scr::Wave) sc[3] = uint8_t(waveNo); else { sc[3] = uint8_t(synth * 16); sc[9] = 2; }
        ins = Player::kScratch;
    }
    const bool kit = !scratch && (s_.instrument(ins & 63)[0] & 3) == 2 && ch == 2;
    note = kit ? 1 + pad % 15 : clampi(13 + pad + 12 * padOct_, 1, 127);
    if (scr_ == Scr::Phrase && !rec_) {                // the phrase cursor takes the note, as before
        s_.note(phrase, step) = uint8_t(note);
        lastNote_ = uint8_t(note);
        if (s_.phraseInstr(phrase, step) == 0xFF) s_.phraseInstr(phrase, step) = uint8_t(lastInstr_);
        s_.markPhrase(phrase); s_.markInstrument(s_.phraseInstr(phrase, step) & 63);
        step = clampi(step + 1, 0, 15);
    }
    if (rec_ && p_.running()) recordNote(ch, note);
    padCh_[pad] = int8_t(ch);
    p_.previewOn(ch, note, ins);
}

void Lsdj::padUp(int pad) {
    if (pad < 0 || pad >= 48 || padCh_[pad] < 0) return;
    p_.previewOff(padCh_[pad]);
    padCh_[pad] = -1;
}

// Direct page keys: between the main screens the cursor is carried as Select + arrow does.
void Lsdj::jump(Scr target) {
    static const Scr main[5] = {Scr::Song, Scr::Chain, Scr::Phrase, Scr::Instr, Scr::Table};
    auto idx = [&](Scr x) { for (int i = 0; i < 5; ++i) if (main[i] == x) return i; return -1; };
    int a = idx(scr_), b = idx(target);
    if (a >= 0 && b >= 0) {
        while (a < b) { navigate(Key::Right); ++a; }
        while (a > b) { navigate(Key::Left); --a; }
        return;
    }
    scr_ = target;
}

void Lsdj::openFiles() {
    if (!tr_) return;
    fileCount = tr_->listSongs();
    fileSel = 0;
    status_ = "";
    scr_ = Scr::File;
}

void Lsdj::navigate(Key k) {
    // screen map: Project Wave Synth over Song Chain Phrase Instr Table over Groove
    int col = -1, row = 1;
    switch (scr_) {
        case Scr::Project: row = 0; col = 0; break; case Scr::Synth: row = 0; col = 2; break; case Scr::Wave: row = 0; col = 3; break;
        case Scr::Song: col = 0; break; case Scr::Chain: col = 1; break; case Scr::Phrase: col = 2; break;
        case Scr::Instr: col = 3; break; case Scr::Table: col = 4; break; case Scr::Groove: row = 2; col = 2; break;
        case Scr::File: scr_ = Scr::Project; return;                  // the song list is a sub-page of Project
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
        else if (k == Key::Up) scr_ = col <= 1 ? Scr::Project : col == 2 ? Scr::Synth : Scr::Wave;
        else if (k == Key::Down) scr_ = Scr::Groove;
    } else if (row == 0) {
        if (k == Key::Down) scr_ = col == 0 ? Scr::Song : col == 2 ? Scr::Phrase : Scr::Instr;
        else if (k == Key::Right) scr_ = col == 0 ? Scr::Synth : Scr::Wave;
        else if (k == Key::Left && col > 0) scr_ = col == 3 ? Scr::Synth : Scr::Project;
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
        case Scr::Project: projRow = clampi(projRow + dy, 0, 13); break;
        case Scr::File: fileSel = clampi(fileSel + dy, 0, fileCount ? fileCount - 1 : 0); break;
        case Scr::Synth: synthRow = clampi(synthRow + dy, 0, 9); if (dx) synthCol = clampi(synthCol + dx, 0, 1); break;
        case Scr::Wave:
            waveCol = clampi(waveCol + dx, 0, 31);
            if (dy) setWaveSample(waveCol + 1, clampi(waveSample(waveCol + 1) + dy, 0, 15));   // Up raises the dot: the screen runs 0 at the top
            break;
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
        case Scr::Project:
            if (tr_ && projRow == 13) { openFiles(); break; }
            if (tr_ && projRow >= 9) {
                const bool ok = tr_->action(projRow == 9 ? Transport::kSavePair : projRow == 10 ? Transport::kLoadPair : projRow == 11 ? Transport::kExportWav : Transport::kExportFiles, session);
                status_ = ok ? "DONE" : "FAILED";
            }
            break;
        case Scr::File:
            if (tr_ && fileCount) { status_ = tr_->loadSongFile(fileSel, console_) ? "LOADED" : "LOAD FAILED"; scr_ = Scr::Project; }
            break;
        default: break;
    }
}

void Lsdj::editKey(Key k) {
    if (scr_ == Scr::Synth) { synthEdit(k); return; }
    if (scr_ == Scr::Wave) { waveEdit(k); return; }
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
            if (projRow >= 9 && projRow <= 12) session = clampi(session + d, 0, 99);
            else if (projRow == 2) s_.at(off::kSync) = uint8_t(s_.at(off::kSync) ? 0 : 1);     // OFF / LSDJ link cable
            else if (projRow == 0) { uint8_t& t = s_.at(off::kTempo); t = uint8_t(clampi(t + d, 40, 255)); p_.setTempo(t); }
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

void Lsdj::drawPanel(Lcd& l, bool full) const {
    if (full) l.rect(128, 0, 32, 112, 1);
    static const char* ch[4] = {"PU1", "PU2", "WAV", "NOI"};
    l.rect(128, 0, 32, 8, 3);
    l.text(16, 0, ch[scr_ == Scr::Song ? songCh : (scr_ == Scr::Project ? 0 : songCh)], true);
    char b[8];
    if (full) {
        l.glyphAt(128, 24, '\x01', false, 3, 1);
        std::snprintf(b, sizeof b, "%d", s_.at(off::kTempo));
        for (int i = 0; b[i]; ++i) l.glyphAt(136 + i * 8, 24, b[i], false, 3, 1);
    }
    static const char lab[2][4] = {{'1', '2', '3', '4'}, {'5', '6', '7', '8'}};
    for (int c = 0; full && c < 4; ++c) l.glyphAt(136, 40 + c * 8, lab[console_][c], p_.voiceOn(c), p_.voiceOn(c) ? 0 : 3, p_.voiceOn(c) ? 3 : 1);
    // screen map
    auto mark = [&](int col, int row, char c, Scr s) { l.glyph(col, row, c, scr_ == s); };
    if (full) l.rect(120, 112, 40, 32, 0);
    mark(15, 14, 'P', Scr::Project); mark(17, 14, 'Y', Scr::Synth); mark(18, 14, 'W', Scr::Wave);
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
        case Scr::File: drawFile(l); break;
        case Scr::Wave: drawWave(l); break;
        case Scr::Synth: drawSynth(l); break;
    }
    if (scr_ != Scr::File) drawPanel(l, scr_ != Scr::Wave);
    if (rec_) l.text(16, 10, "REC", true);
    if (padOct_ != 0 && scr_ != Scr::File) { char o[8]; std::snprintf(o, sizeof o, "O%+d", padOct_); l.text(16, 11, o); }
    if (banner_[0]) { l.rect(0, 136, 160, 8, 0); l.text(0, 17, banner_); }
}

void Lsdj::drawSong(Lcd& l) const {
    l.text(0, 0, "SONG");
    l.text(5, 0, console_ ? "B 5-8" : "A 1-4");
    static const char* ch[4] = {"PU1", "PU2", "WAV", "NOI"};
    for (int c = 0; c < 4; ++c) l.text(0, 1, "", false), [&] { for (int i = 0; i < 3; ++i) l.glyphAt(18 + c * 26 + i * 8 - 2, 8, ch[c][i], p_.muted(c), 2, 0); }();
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


// ---- Wave and Synth screens ---------------------------------------------------------------------------------------------

int Lsdj::waveSample(int i) const { const uint8_t b = s_.wave(waveNo)[(i & 31) >> 1]; return (i & 1) ? (b & 15) : (b >> 4); }

void Lsdj::setWaveSample(int i, int v) {
    uint8_t& b = s_.wave(waveNo)[(i & 31) >> 1];
    b = (i & 1) ? uint8_t((b & 0xF0) | v) : uint8_t((b & 0x0F) | (v << 4));
}

void Lsdj::waveEdit(Key k) {
    const int sample = waveCol + 1;
    if (k == Key::Up || k == Key::Down) { setWaveSample(sample, 15 - waveSample(sample)); return; }   // A + up/down flips the sample
    // A + left/right draws: step sideways and copy the sample just left (right) behind
    const int d = k == Key::Right ? 1 : -1, v = waveSample(sample);
    waveCol = clampi(waveCol + d, 0, 31);
    setWaveSample(waveCol + 1, v);
}

void Lsdj::regenSynth() { if (sg_) sg_->regenerate(s_, synth); }

namespace {
struct SynthField { const char* label; int y; int b0, b1; int kind; int step; };   // kind: 0 signal, 1 filter, 2 dist, 3 phase mode, 4 byte, 5 Q nibbles, 6 phase (0-1F), 7 limit (wraps)
const SynthField kSynthFields[10] = {
    {"SIGNAL", 2, 0, 0, 0, 1}, {"FILTER", 3, 1, 1, 1, 1}, {"DIST", 4, 3, 3, 2, 1}, {"PHASE", 5, 4, 4, 3, 1},
    {"VOLUME", 9, 5, 9, 4, 16}, {"CUTOFF", 10, 6, 10, 4, 16}, {"Q", 11, 2, 2, 5, 2}, {"VSHIFT", 12, 8, 12, 4, 16},
    {"LIMIT", 13, 13, 14, 7, 16}, {"PHASE", 14, 7, 11, 6, 8},
};
const char* const kFilterNames[6] = {" LOWP", "HIGHP", "BANDP", " ALLP", "NOTCH", "-----"};
const char* const kDistNames[3] = {" CLIP", " FOLD", " WRAP"};
const char* const kPhaseNames[4] = {"PINCH", " WARP", "RESYNC", "RESYN2"};
}  // namespace

void Lsdj::synthEdit(Key k) {
    const SynthField& f = kSynthFields[synthRow];
    uint8_t* p = s_.synth(synth);
    const int d = (k == Key::Right || k == Key::Up) ? 1 : -1;
    const bool big = k == Key::Up || k == Key::Down;
    const int col = f.b0 == f.b1 ? 0 : synthCol;
    uint8_t& b = p[col ? f.b1 : f.b0];
    switch (f.kind) {
        case 0: b = uint8_t(clampi(b + d, 0, 3)); break;
        case 1: b = uint8_t(clampi(b + d, 0, 5)); break;
        case 2: b = uint8_t(clampi(b + d, 0, 2)); break;
        case 3: b = uint8_t(clampi(b + d, 0, 3)); break;
        case 4: b = uint8_t(clampi(b + d * (big ? f.step : 1), 0, 255)); break;
        case 5: {                                                   // Q: start in the high nibble, end in the low one
            const int v = col ? (b & 15) : (b >> 4), n = clampi(v + d * (big ? f.step : 1), 0, 15);
            b = col ? uint8_t((b & 0xF0) | n) : uint8_t((b & 0x0F) | (n << 4));
            break;
        }
        case 6: b = uint8_t(clampi(b + d * (big ? f.step : 1), 0, 0x1F)); break;
        default: b = uint8_t(b + d * (big ? f.step : 1)); break;   // limit wraps
    }
    regenSynth();
}

void Lsdj::drawSynth(Lcd& l) const {
    char t[24]; std::snprintf(t, sizeof t, "SYNTH %X", synth); l.text(0, 0, t);
    const uint8_t* p = s_.synth(synth);
    const char hx[] = "0123456789ABCDEF";
    auto cell = [&](int col, int row, const char* s, bool cur) {            // editable values sit on a grey cell, the cursor is inverted
        const int n = int(std::strlen(s));
        l.rect(col * 8, row * 8, n * 8, 8, 1);
        l.text(col, row, s, cur);
    };
    for (int i = 0; i < 10; ++i) {
        const SynthField& f = kSynthFields[i];
        l.text(0, f.y, f.label);
        const bool onRow = synthRow == i;
        if (f.kind < 4) {
            const uint8_t v = p[f.b0];
            if (f.kind == 0) {                                                // the signal is drawn as a little wave icon
                l.rect(72, f.y * 8, 32, 8, onRow ? 3 : 1);
                const uint8_t fg = onRow ? 0 : 3;
                for (int x = 0; x < 32; ++x) {
                    int y;
                    switch (v & 3) {
                        case 0: y = 7 - (x % 8); break;                                       // saw
                        case 1: y = (x % 16) < 8 ? 1 : 6; break;                              // square
                        case 2: { const int ph = x % 16; y = ph < 8 ? 1 + ph * 6 / 7 : 6 - (ph - 8) * 5 / 7; break; }   // triangle
                        default: y = 1 + ((x * 5) % 7); break;                                // W.FX input: a ragged line
                    }
                    l.px(72 + x, f.y * 8 + y, fg);
                }
            } else {
                const char* name = f.kind == 1 ? kFilterNames[v > 5 ? 5 : v] : f.kind == 2 ? kDistNames[v > 2 ? 2 : v] : kPhaseNames[v & 3];
                char s[8]; std::snprintf(s, sizeof s, "%6s", name);
                cell(7, f.y, s + (6 - int(std::strlen(name))) , onRow);
            }
            continue;
        }
        for (int c = 0; c < 2; ++c) {
            const uint8_t b = p[c ? f.b1 : f.b0];
            char s[4];
            switch (f.kind) {
                case 5: s[0] = ' '; s[1] = hx[c ? (b & 15) : (b >> 4)]; break;
                case 7: { const uint8_t v = uint8_t(b + 0x0F); s[0] = hx[v >> 4]; s[1] = hx[v & 15]; break; }   // limit shows its byte + 0F
                default: s[0] = hx[b >> 4]; s[1] = hx[b & 15]; break;
            }
            s[2] = 0;
            cell(c ? 11 : 8, f.y, s, onRow && synthCol == c);
        }
    }
    l.text(0, 7, "WAVE");
    char w[8]; std::snprintf(w, sizeof w, "%02X-%02X", synth * 16, synth * 16 + 15);
    l.text(8, 7, w);
    if (!sg_ || !sg_->available()) l.text(0, 16, "NO ROM: FRAMES NOT REBUILT");
    else if (sg_->lastCycles()) { char m[24]; std::snprintf(m, sizeof m, "REBUILD %uK CYC", unsigned(sg_->lastCycles() / 1000)); l.text(0, 16, m); }
}

void Lsdj::drawWave(Lcd& l) const {
    char t[16]; std::snprintf(t, sizeof t, "WAVE %02X", waveNo); l.text(0, 0, t);
    const char hx[] = "0123456789ABCDEF";
    // 32 columns x 4 px, value 0 at the top; column c shows sample (c + 1) & 31, the two hex rows show 15 - sample
    for (int c = 0; c < 32; ++c) {
        const int v = waveSample(c + 1);
        l.rect(16 + c * 4, 48 + v * 4, 4, 4, 3);
        const bool cur = c == waveCol;
        if (cur) for (int y = 48; y < 112; y += 2) l.px(16 + c * 4 + 1, y, 3);
        l.glyph(2 + (c & 15), c < 16 ? 5 : 14, hx[15 - v], cur);
    }
}

void Lsdj::drawFile(Lcd& l) const {
    l.text(0, 0, console_ ? "LOAD SONG B" : "LOAD SONG A");
    if (!fileCount) { l.text(0, 2, "NO SONGS IN GB/SONGS"); return; }
    const int top = std::max(0, std::min(fileSel - 8, fileCount - 16));
    for (int r = 0; r < 16 && top + r < fileCount; ++r) {
        char s[24]; std::snprintf(s, sizeof s, "%-19.19s", tr_ ? tr_->songName(top + r) : "");
        l.text(0, 2 + r, s, top + r == fileSel);
    }
}

void Lsdj::drawProject(Lcd& l) const {
    l.text(0, 0, "PROJECT (NEW)");
    char b[24];
    auto row = [&](int y, const char* name, const char* val, bool cur) { l.text(0, y, name, cur && !val[0]); l.text(9, y, val, cur); };   // a row with no value is selected by its name
    std::snprintf(b, sizeof b, "%02X", s_.at(off::kTempo)); row(2, "TEMPO", b, projRow == 0);
    std::snprintf(b, sizeof b, "%02X", s_.at(off::kTranspose)); row(3, "TRANSPOSE", b, projRow == 1);
    row(4, "SYNC", s_.at(off::kSync) == 1 ? "LSDJ" : s_.at(off::kSync) ? "OTHER" : "OFF", projRow == 2);
    row(5, "CLONE", "DEEP", projRow == 3);
    row(6, "LOOK", "92K/GRAY", projRow == 4);
    row(7, "PRELISTEN", "ON", projRow == 5);
    row(8, "HELP", "", projRow == 6);
    row(9, "CLEAN SONG DATA", "", projRow == 7);
    row(10, "CLEAN INSTR DATA", "", projRow == 8);
    row(11, "SAVE SESSION", "", projRow == 9);
    row(12, "LOAD SESSION", "", projRow == 10);
    row(13, "EXPORT WAV", "", projRow == 11);
    row(14, "EXPORT LSDJ", "", projRow == 12);
    row(15, console_ ? "LOAD SONG FILE B" : "LOAD SONG FILE A", "", projRow == 13);
    { char s[8]; std::snprintf(s, sizeof s, "S%02d", session); l.text(13, 11, s, projRow == 9); l.text(13, 12, s, projRow == 10); l.text(13, 13, s, projRow == 11); l.text(13, 14, s, projRow == 12); }
    l.text(0, 17, status_);
    l.text(0, 16, audio_[0] ? audio_ : "WORK TIME: 0:00");
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
