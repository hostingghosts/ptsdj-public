#include "gb/ui/lsdj.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

bool Lsdj::handDrawn(int syn) const { return (s_.at(syn < 8 ? off::kHandDrawn + 1 : off::kHandDrawn) >> (syn & 7)) & 1; }
void Lsdj::markHandDrawn(int wave) {
    const int syn = (wave >> 4) & 15;
    s_.at(syn < 8 ? off::kHandDrawn + 1 : off::kHandDrawn) |= uint8_t(1 << (syn & 7));
}

void Lsdj::key(Key k, bool down) {
    if (down) msg_ = "";
    if (prompt_ && down) {                              // the overwrite question takes the keys
        if (k == Key::Left) promptYes_ = true;
        else if (k == Key::Right) promptYes_ = false;
        else if (k == Key::A || k == Key::Start) {
            if (promptYes_) s_.at(synth < 8 ? off::kHandDrawn + 1 : off::kHandDrawn) &= uint8_t(~(1 << (synth & 7)));   // YES: the next edit regenerates
            prompt_ = false;
        } else if (k == Key::B) prompt_ = false;
        return;
    }
    if (nameOpen_) { if (down) nameKey(k); return; }    // the name keyboard takes every key until it is closed
    if (scr_ == Scr::Help) {                             // help pages: left / right turn the page, Delete leaves
        if (down) {
            if (k == Key::Left) helpPage = clampi(helpPage - 1, 0, helpPages() - 1);
            else if (k == Key::Right) helpPage = clampi(helpPage + 1, 0, helpPages() - 1);
            else if (k == Key::B) scr_ = helpFrom_;
        }
        return;
    }
    if (confirm_ >= 0 && down) {                         // cleaning asks for a second Insert; anything else cancels
        if (k == Key::A) {
            const int r = confirm_;
            confirm_ = -1;
            if (r == 8) cleanSong();
            else if (r == 9) cleanInstruments();
            else {                                       // 10: erase the song under the cursor from the bank
                status_ = tr_ && tr_->deleteSongFile(fileSel, console_) ? "ERASED" : "CAN'T ERASE";
                fileCount = tr_ ? tr_->listSongs(console_) : 0;
                fileSel = clampi(fileSel, 0, fileCount ? fileCount - 1 : 0);
            }
        }
        else { confirm_ = -1; status_ = ""; }
        return;
    }
    if (k == Key::A) {
        if (down && a_) return;
        if (down) {
            a_ = true; edited_ = false; selUsed_ = false;
            if (sel_ && scr_ == Scr::Wave) { if (hasClip_) { for (int i = 0; i < 16; ++i) s_.wave(waveNo)[i] = waveClip_[i]; markHandDrawn(waveNo); } edited_ = true; }
            else if (b_) { cut(); edited_ = true; }
        } else {
            if (a_ && sel_ && scr_ == Scr::Song && !selUsed_ && !edited_ && !b_) { p_.mute(songCh, !p_.muted(songCh)); }   // Shift + Insert alone mutes the channel
            else if (a_ && !edited_ && !b_ && !sel_) press(Key::A);
            a_ = false;
        }
        return;
    }
    if (k == Key::B) {
        if (down) {
            bUsed_ = false;
            if (sel_ && scr_ == Scr::File && fileCount) { confirm_ = 10; status_ = "ERASE? INSERT=YES"; bUsed_ = true; }
            else if (sel_ && scr_ == Scr::Song) {                  // Shift + Delete: mute this whole console, or bring it all back
                bool any = false;
                for (int c = 0; c < 4; ++c) any = any || !p_.muted(c);
                for (int c = 0; c < 4; ++c) p_.mute(c, any);
                bUsed_ = true;
            }
        }
        else if (b_ && !bUsed_ && !sel_ && scr_ == Scr::File) scr_ = Scr::Project;
        else if (b_ && !bUsed_ && !sel_ && scr_ == Scr::Library) scr_ = Scr::Instr;
        else if (b_ && !bUsed_ && !sel_ && scr_ == Scr::Wave) {          // a lone B tap copies the wave
            for (int i = 0; i < 16; ++i) waveClip_[i] = s_.wave(waveNo)[i];
            hasClip_ = true;
        }
        else if (b_ && !bUsed_ && !sel_ && !a_ && selectable()) clearCells();   // a lone Delete tap empties the cell (or every selected cell)
        b_ = down;
        return;
    }
    if (k == Key::Select) { sel_ = down; return; }
    if (!down) return;
    if (k == Key::Start) {
        if (playing()) {
            rec_ = false;
            if (tr_) tr_->stop(); else p_.stop();
        } else if (recMode_ && scr_ == Scr::Phrase) {      // Rec mode + Play: this phrase loops on the cursor channel, the song plays behind it, the pads record
            rec_ = true;
            if (tr_) tr_->startLoop(console_, songCh, phrase, songRow); else p_.start(songRow, songCh, phrase, true);
        } else if (scr_ == Scr::Synth || scr_ == Scr::Wave) {   // Play audits the wave instead of starting the song
            if (auditionHeld_) { p_.previewOff(2); auditionHeld_ = false; }
            else {
                uint8_t* sc = p_.scratchInstr();
                for (int i = 0; i < 16; ++i) sc[i] = 0;
                sc[0] = 1; sc[1] = 0xA8; sc[2] = 0x0F; sc[7] = 0x03;
                if (scr_ == Scr::Wave) sc[3] = uint8_t(waveNo); else { sc[3] = uint8_t(synth * 16); sc[9] = 2; }
                p_.previewOn(2, 25, Player::kScratch);
                auditionHeld_ = true;
            }
        } else if (tr_) tr_->start(console_, startRow());
        else p_.start(startRow());
        return;
    }
    if (sel_ && a_ && selectable()) { selUsed_ = true; selMove(k); return; }   // Shift + Insert + arrows selects cells
    if (sel_) { navigate(k); return; }
    if (a_) { edited_ = true; if (selOn_ && selectable()) adjustCells(k); else editKey(k); return; }
    if (b_) {
        bUsed_ = true;
        if (scr_ == Scr::Wave) { waveNo = clampi(waveNo + (k == Key::Left ? -1 : k == Key::Right ? 1 : k == Key::Up ? 16 : -16), 0, 255); return; }
        if (scr_ == Scr::Synth) { synth = clampi(synth + (k == Key::Left || k == Key::Up ? -1 : 1), 0, kSynths - 1); return; }
        changeId(k == Key::Left || k == Key::Up ? -1 : 1); if (k == Key::Up || k == Key::Down) changeId(0); return;
    }
    clearSel();
    move(k);
}

void Lsdj::encoder(int delta) {
    if (!delta) return;
    if (nameOpen_) { for (int i = 0, n = delta < 0 ? -delta : delta; i < n; ++i) nameKey(delta > 0 ? Key::Right : Key::Left); return; }
    if (scr_ == Scr::Help) { helpPage = clampi(helpPage + (delta > 0 ? 1 : -1), 0, helpPages() - 1); return; }
    if (popup_) { popupTurn(delta > 0 ? 1 : -1); return; }
    if (recMode_ && scr_ == Scr::Phrase) { instr = clampi(instr + (delta > 0 ? 1 : -1), 0, 63); return; }   // Rec mode: the jog wheel picks the pad instrument
    const Key k = sel_ ? (delta > 0 ? Key::Up : Key::Down) : (delta > 0 ? Key::Right : Key::Left);
    for (int i = 0, n = delta < 0 ? -delta : delta; i < n; ++i) { if (selOn_ && selectable()) adjustCells(k); else editKey(k); }
}

bool Lsdj::playing() const { return tr_ ? tr_->running() : p_.running(); }

int Lsdj::curInstr() const {
    if (scr_ == Scr::Phrase && !recMode_) { const uint8_t i = s_.phraseInstr(phrase, step); return i != 0xFF ? (i & 63) : lastInstr_; }
    return instr;
}

// The instrument decides the channel it can sound on: pulse on PU1/PU2 (the cursor channel when that is one of them), wave and kit on WAV, noise on NOI.
int Lsdj::previewChannel(int ins) const {
    if (scr_ == Scr::Wave || scr_ == Scr::Synth) return 2;
    switch (s_.instrument(ins & 63)[0] & 3) {
        case 0: return songCh < 2 ? songCh : 0;
        case 1: case 2: return 2;
        default: return 3;
    }
}

// Records a played note into the phrase the cursor channel is looping or playing: the step nearest to what the player is hearing. The
// sequencer runs ahead of the speakers (the audio is rendered into a queue first), so that lead is taken off the position before rounding.
void Lsdj::recordNote(int ch, int note) {
    const Seq& sq = p_.seq();
    if (!sq.active(ch)) return;
    const int ph = sq.phrase(ch);
    if (ph == 0xFF) return;
    const int len = std::max(1, sq.waitLen(ch));
    const float leadTicks = (tr_ ? tr_->audioLeadMs() : 0) * 0.4f * float(p_.tempo()) / 1000.f;
    const float x = float(len - sq.wait(ch)) - leadTicks;        // ticks since the previous step began, as heard
    int st = (sq.step(ch) - 1 + int(std::floor(x / float(len) + 0.5f))) % 16;
    if (st < 0) st += 16;
    s_.note(ph, st) = uint8_t(note);
    s_.phraseInstr(ph, st) = uint8_t(curInstr());
    s_.markPhrase(ph);
    s_.markInstrument(curInstr());
}

void Lsdj::padDown(int pad) {
    if (pad < 0 || pad >= 48 || nameOpen_ || scr_ == Scr::Help) return;
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
    const int ch = previewChannel(ins);
    const bool kit = !scratch && (s_.instrument(ins & 63)[0] & 3) == 2;
    note = kit ? 1 + pad % 15 : clampi(13 + pad + 12 * padOct_, 1, 127);
    if (scr_ == Scr::Phrase && !recMode_) {            // Rec mode off: the phrase cursor takes the note (step mode), as before
        s_.note(phrase, step) = uint8_t(note);
        lastNote_ = uint8_t(note);
        if (s_.phraseInstr(phrase, step) == 0xFF) s_.phraseInstr(phrase, step) = uint8_t(lastInstr_);
        s_.markPhrase(phrase); s_.markInstrument(s_.phraseInstr(phrase, step) & 63);
        step = clampi(step + 1, 0, 15);
    }
    if (recMode_ && scr_ == Scr::Phrase && p_.running()) recordNote(songCh, note);   // armed and the song (or the phrase loop) is running: record
    if (!prelisten_ && !(recMode_ && scr_ == Scr::Phrase)) return;                  // PRELISTEN off: pads stay silent outside Rec mode
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
    clearSel();
    if (scr_ == Scr::Help) scr_ = helpFrom_;
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

void Lsdj::openLibrary() {
    if (!tr_) return;
    libCount = tr_->listInstruments();
    libSel = 0;
    status_ = "";
    scr_ = Scr::Library;
}

void Lsdj::openHelp() {
    if (scr_ == Scr::Help) { scr_ = helpFrom_; return; }
    helpFrom_ = scr_;
    helpPage = 0;
    scr_ = Scr::Help;
}

void Lsdj::openFiles() {
    if (!tr_) return;
    fileCount = tr_->listSongs(console_);
    fileSel = 0;
    status_ = "";
    scr_ = Scr::File;
}

void Lsdj::navigate(Key k) {
    clearSel();
    // screen map: Project Wave Synth over Song Chain Phrase Instr Table over Groove
    int col = -1, row = 1;
    switch (scr_) {
        case Scr::Project: row = 0; col = 0; break; case Scr::Synth: row = 0; col = 2; break; case Scr::Wave: row = 0; col = 3; break;
        case Scr::Song: col = 0; break; case Scr::Chain: col = 1; break; case Scr::Phrase: col = 2; break;
        case Scr::Instr: col = 3; break; case Scr::Table: col = 4; break; case Scr::Groove: row = 2; col = 2; break;
        case Scr::File: scr_ = Scr::Project; return;                  // the song list is a sub-page of Project
        case Scr::Help: return;
        case Scr::Library: scr_ = Scr::Instr; return;
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
        case Scr::Instr:
            instrRow = clampi(instrRow + dy, -1, instrRows() - 1);
            if (instrRow < 0 && dx) { instr = clampi(instr + dx, 0, kInstruments - 1); instrRow = -1; }   // the number at the top: Left / Right scroll through the instruments
            break;
        case Scr::Table: tableCol = clampi(tableCol + dx, 0, 5); tableRow = clampi(tableRow + dy, 0, 15); break;
        case Scr::Groove: grooveStep = clampi(grooveStep + dy, 0, 15); break;
        case Scr::Project: projRow = clampi(projRow + dy, 0, 13); break;
        case Scr::File: fileSel = clampi(fileSel + dy, 0, fileCount ? fileCount - 1 : 0); break;
        case Scr::Library: libSel = clampi(libSel + dy, 0, libCount); break;
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
        case Scr::Project: projectAct(); break;
        case Scr::Instr: if (instrRow == 0) openName(0); break;
        case Scr::Library:
            if (libSel == 0) openName(2);
            else if (tr_) {
                uint8_t blob[128];
                const int n = tr_->loadInstrumentFile(libSel - 1, blob, sizeof blob);
                status_ = n > 0 && unpackInstrument(blob, n) ? "LOADED" : "LOAD FAILED";
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
            if (instrRow < 0) { instr = clampi(instr + d, 0, kInstruments - 1); instrRow = -1; break; }   // the number at the top
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
        case Scr::Project: {
            if (projRow == 1) {
                if (tr_ && tr_->syncMode(console_) == 2 && tr_->linked()) { status_ = "SLAVE FOLLOWS MASTER"; break; }   // the master sets the tempo
                uint8_t& tp = s_.at(off::kTempo); tp = uint8_t(clampi(tp + d, 40, 255)); p_.setTempo(tp);
                if (tr_) tr_->tempoChanged(console_);
            }
            else if (projRow == 2) s_.at(off::kTranspose) = uint8_t(int8_t(s_.at(off::kTranspose)) + d);
            else if (projRow == 3) {                                                          // SYNC: OFF / MASTER / SLAVE
                if (tr_) tr_->setSyncMode(console_, (tr_->syncMode(console_) + (sgn > 0 ? 1 : 2)) % 3);
                else s_.at(off::kSync) = uint8_t(s_.at(off::kSync) ? 0 : 1);
            }
            else if (projRow == 4) cloneDeep_ = !cloneDeep_;
            else if (projRow == 5 && tr_) tr_->setLook((tr_->look() + (sgn > 0 ? 1 : 3)) & 3);
            else if (projRow == 6) prelisten_ = !prelisten_;
            else if (projRow == 13) exportSel = (exportSel + (sgn > 0 ? 1 : 2)) % 3;
            break;
        }
        default: break;
    }
}

void Lsdj::cut() { clearCells(); }

bool Lsdj::hasData(int row, int col) const {
    const uint8_t v = *cellPtr(row, col);
    switch (scr_) {
        case Scr::Song: return v != 0xFF;
        case Scr::Chain: return col == 1 || v != 0xFF;
        case Scr::Phrase: return col == 0 ? v != 0 : col == 1 ? v != 0xFF : col == 2 ? v != 0 : true;
        case Scr::Table: return (col == 2 || col == 4) ? v != 0 : true;
        default: return false;
    }
}

// Delete: empties the selected cells, or the one under the cursor (a command cell takes its value with it).
void Lsdj::clearCells() {
    int r0 = curRow(), r1 = r0, c0 = curCol(), c1 = c0;
    if (selOn_) { r0 = std::min(selRow_, r1); r1 = std::max(selRow_, curRow()); c0 = std::min(selCol_, c1); c1 = std::max(selCol_, curCol()); }
    const bool single = r0 == r1 && c0 == c1;
    clearSel();
    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c) {
            uint8_t* p = cellPtr(r, c);
            switch (scr_) {
                case Scr::Song: if (single && *p != 0xFF) lastChain_ = *p; *p = 0xFF; break;
                case Scr::Chain: *p = c == 0 ? 0xFF : 0; break;
                case Scr::Phrase:
                    if (c == 1) *p = 0xFF; else *p = 0;
                    if (c == 2) *cellPtr(r, 3) = 0;
                    break;
                case Scr::Table:
                    *p = 0;
                    if (c == 2 || c == 4) *cellPtr(r, c + 1) = 0;
                    break;
                default: break;
            }
        }
    if (scr_ == Scr::Phrase) s_.markPhrase(phrase);
    msg_ = single ? "CLR" : "CLRS";
}

// The jog wheel (or Insert + arrows) with a selection changes every cell that holds something, not only the one under the cursor.
void Lsdj::adjustCells(Key k) {
    const int r0 = std::min(selRow_, curRow()), r1 = std::max(selRow_, curRow()), c0 = std::min(selCol_, curCol()), c1 = std::max(selCol_, curCol());
    const int cr = curRow(), cc = curCol();
    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c) {
            if (!hasData(r, c)) continue;
            setCursor(r, c);
            editKey(k);
        }
    setCursor(cr, cc);
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

// ---- project actions, names, cleaning ---------------------------------------------------------------------------------------

void Lsdj::setName(const char* n) {
    int i = 0;
    for (; n && n[i] && i < 8; ++i) name_[i] = (n[i] >= 'a' && n[i] <= 'z') ? char(n[i] - 32) : n[i];
    name_[i] = 0;
}

void Lsdj::projectAct() {
    auto act = [&](Transport::Action a, int arg, const char* ok, const char* bad) {
        if (!tr_) return;
        status_ = tr_->action(a, arg) ? (tr_->doneText() ? tr_->doneText() : ok) : (tr_->errorText() ? tr_->errorText() : bad);
    };
    switch (projRow) {
        case 0: openName(1); break;
        case 7: openHelp(); break;
        case 8: confirm_ = 8; status_ = "CLEAN SONG? INSERT=YES"; break;
        case 9: confirm_ = 9; status_ = "CLEAN INSTR? INSERT=YES"; break;
        case 10: act(Transport::kSaveSong, console_, "SAVED", "SAVE FAILED"); break;
        case 11: if (tr_) openFiles(); break;
        case 12: act(Transport::kExportWav, 0, "WAV DONE", "WAV FAILED"); break;
        case 13: act(Transport::kExportFiles, exportSel, "EXPORT DONE", "EXPORT FAILED"); break;
        default: break;
    }
}

namespace {
void reachable(Song& s, bool* chainUsed, bool* phraseUsed) {
    for (int r = 0; r < kSongRows; ++r)
        for (int c = 0; c < 4; ++c) { const uint8_t v = s.songCell(r, c); if (v < kChains) chainUsed[v] = true; }
    for (int c = 0; c < kChains; ++c)
        if (chainUsed[c])
            for (int i = 0; i < 16; ++i) { const uint8_t p = s.chainPhrase(c, i); if (p < kPhrases) phraseUsed[p] = true; }
}
bool bit(const Song& s, uint16_t base, int i) { return (s.at(uint16_t(base + i / 8)) >> (i & 7)) & 1; }
void clearBit(Song& s, uint16_t base, int i) { s.at(uint16_t(base + i / 8)) &= uint8_t(~(1 << (i & 7))); }
void resetBytes(Song& s, uint16_t o, int n) { for (int i = 0; i < n; ++i) s.at(uint16_t(o + i)) = Song::defaultAt(uint16_t(o + i)); }
}  // namespace

// Frees chains and phrases that no song row reaches (what LSDJ's CLEAN SONG DATA does).
void Lsdj::cleanSong() {
    bool chainUsed[kChains] = {}, phraseUsed[kPhrases] = {};
    reachable(s_, chainUsed, phraseUsed);
    int nc = 0, np = 0;
    for (int c = 0; c < kChains; ++c)
        if (!chainUsed[c]) {
            if (bit(s_, off::kChainAlloc, c)) ++nc;
            resetBytes(s_, uint16_t(off::kChainPhrase + c * 16), 16); resetBytes(s_, uint16_t(off::kChainTrans + c * 16), 16);
            clearBit(s_, off::kChainAlloc, c);
        }
    for (int p = 0; p < kPhrases; ++p)
        if (!phraseUsed[p]) {
            if (bit(s_, off::kPhraseAlloc, p)) ++np;
            resetBytes(s_, uint16_t(off::kNotes + p * 16), 16); resetBytes(s_, uint16_t(off::kPhraseInstr + p * 16), 16);
            resetBytes(s_, uint16_t(off::kPhraseCmd + p * 16), 16); resetBytes(s_, uint16_t(off::kPhraseVal + p * 16), 16);
            clearBit(s_, off::kPhraseAlloc, p);
        }
    std::snprintf(statusBuf_, sizeof statusBuf_, "CLEANED %d CH %d PHR", nc, np);
    status_ = statusBuf_;
}

// Resets instruments that no phrase plays.
void Lsdj::cleanInstruments() {
    bool chainUsed[kChains] = {}, phraseUsed[kPhrases] = {}, instrUsed[kInstruments] = {};
    reachable(s_, chainUsed, phraseUsed);
    for (int p = 0; p < kPhrases; ++p)
        if (phraseUsed[p])
            for (int i = 0; i < 16; ++i) { const uint8_t v = s_.phraseInstr(p, i); if (v < kInstruments) instrUsed[v] = true; }
    int n = 0;
    for (int i = 0; i < kInstruments; ++i)
        if (!instrUsed[i]) {
            if (s_.at(uint16_t(off::kInstrAlloc + i))) ++n;
            resetBytes(s_, uint16_t(off::kInstr + i * 16), 16);
            for (int k = 0; k < 5; ++k) s_.at(uint16_t(off::kInstrNames + i * 5 + k)) = 0;
            s_.at(uint16_t(off::kInstrAlloc + i)) = 0;
        }
    std::snprintf(statusBuf_, sizeof statusBuf_, "CLEANED %d INSTR", n);
    status_ = statusBuf_;
}

const char* Lsdj::instrName(int i, char out[6]) const {
    for (int k = 0; k < 5; ++k) { const uint8_t c = s_.at(uint16_t(off::kInstrNames + i * 5 + k)); out[k] = c >= 32 && c < 127 ? char(c) : ' '; }
    out[5] = 0;
    return out;
}

namespace {
const char* const kKeyRows[4] = {"0123456789", "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ"};
int rowWidth(int cy) { return cy < 3 ? 10 : cy == 3 ? 6 : 9; }   // the last row: six letters, space, backspace, OK
}  // namespace

void Lsdj::openName(int target) {
    nameTarget_ = target;
    nameMax_ = target ? 8 : 5;
    nameCx_ = nameCy_ = 0;
    std::memset(nameBuf_, 0, sizeof nameBuf_);
    if (target == 1) std::strncpy(nameBuf_, name_, 8);
    else { char tmp[6]; instrName(instr, tmp); std::strncpy(nameBuf_, tmp, 5); }
    for (int i = int(std::strlen(nameBuf_)) - 1; i >= 0 && nameBuf_[i] == ' '; --i) nameBuf_[i] = 0;
    if (target == 2 && !nameBuf_[0]) std::snprintf(nameBuf_, sizeof nameBuf_, "INST%02X", instr);
    nameOpen_ = true;
}

void Lsdj::nameCommit() {
    int n = int(std::strlen(nameBuf_));
    while (n > 0 && nameBuf_[n - 1] == ' ') nameBuf_[--n] = 0;
    if (nameTarget_ == 0) {
        for (int k = 0; k < 5; ++k) s_.at(uint16_t(off::kInstrNames + instr * 5 + k)) = k < n ? uint8_t(nameBuf_[k]) : 0;
        s_.markInstrument(instr);
    } else if (nameTarget_ == 1) setName(nameBuf_);
    else if (n > 0 && tr_) {                                          // library: save the current instrument under this name
        uint8_t blob[128];
        const int len = packInstrument(blob);
        status_ = tr_->saveInstrumentFile(nameBuf_, blob, len) ? "SAVED" : "SAVE FAILED";
        libCount = tr_->listInstruments();
    }
    nameOpen_ = false;
}

void Lsdj::nameKey(Key k) {
    auto append = [&](char c) { const int n = int(std::strlen(nameBuf_)); if (n < nameMax_) { nameBuf_[n] = c; nameBuf_[n + 1] = 0; } };
    auto back = [&]() { const int n = int(std::strlen(nameBuf_)); if (n > 0) nameBuf_[n - 1] = 0; };
    switch (k) {
        case Key::Left: nameCx_ = nameCx_ > 0 ? nameCx_ - 1 : rowWidth(nameCy_) - 1; break;
        case Key::Right: nameCx_ = nameCx_ + 1 < rowWidth(nameCy_) ? nameCx_ + 1 : 0; break;
        case Key::Up: nameCy_ = nameCy_ > 0 ? nameCy_ - 1 : 4; nameCx_ = std::min(nameCx_, rowWidth(nameCy_) - 1); break;
        case Key::Down: nameCy_ = nameCy_ < 4 ? nameCy_ + 1 : 0; nameCx_ = std::min(nameCx_, rowWidth(nameCy_) - 1); break;
        case Key::A:
            if (nameCy_ < 4) append(kKeyRows[nameCy_][nameCx_]);
            else if (nameCx_ < 6) append(kKeyRows[3][nameCx_]);
            else if (nameCx_ == 6) append(' ');
            else if (nameCx_ == 7) back();
            else nameCommit();
            break;
        case Key::B: nameOpen_ = false; break;                // Delete = NO: close without saving
        case Key::Start: nameCommit(); break;
        case Key::Select: break;
    }
}

void Lsdj::soloChannel(int ch) {
    bool othersMuted = true;
    for (int c = 0; c < 4; ++c) if (c != ch) othersMuted = othersMuted && p_.muted(c);
    if (othersMuted && !p_.muted(ch)) { for (int c = 0; c < 4; ++c) p_.mute(c, false); return; }   // soloed already: bring everything back
    for (int c = 0; c < 4; ++c) p_.mute(c, c != ch);
}

// ---- instrument library --------------------------------------------------------------------------------------------------

// File: 'G','B','I','1', flags (bit 0: a table follows), name[5], instrument[16], and the instrument's table (6 columns x 16 rows) if it uses one.
int Lsdj::packInstrument(uint8_t* out) const {
    const uint8_t* in = s_.instrument(instr);
    const bool tbl = (in[6] & 0x20) != 0;
    out[0] = 'G'; out[1] = 'B'; out[2] = 'I'; out[3] = '1';
    out[4] = tbl ? 1 : 0;
    char nm[6]; instrName(instr, nm);
    for (int k = 0; k < 5; ++k) out[5 + k] = uint8_t(nm[k]);
    std::memcpy(out + 10, in, 16);
    int n = 26;
    if (tbl) {
        static const uint16_t o[6] = {off::kTblEnv, off::kTblTrans, off::kTblCmd1, off::kTblVal1, off::kTblCmd2, off::kTblVal2};
        const int t = in[6] & 0x1F;
        for (int c = 0; c < 6; ++c) { std::memcpy(out + n, s_.mem() + o[c] + t * 16, 16); n += 16; }
    }
    return n;
}

// Loads into the current instrument. A table goes into a free table slot (no instrument uses it and it is empty); without one the instrument loads without it.
bool Lsdj::unpackInstrument(const uint8_t* in, int n) {
    if (n < 26 || in[0] != 'G' || in[1] != 'B' || in[2] != 'I' || in[3] != '1') return false;
    const bool tbl = (in[4] & 1) && n >= 26 + 96;
    uint8_t* dst = s_.instrument(instr);
    std::memcpy(dst, in + 10, 16);
    for (int k = 0; k < 5; ++k) s_.at(uint16_t(off::kInstrNames + instr * 5 + k)) = in[5 + k] >= 32 ? in[5 + k] : 0;
    if (dst[6] & 0x20) {
        int slot = -1;
        if (tbl) {
            static const uint16_t o[6] = {off::kTblEnv, off::kTblTrans, off::kTblCmd1, off::kTblVal1, off::kTblCmd2, off::kTblVal2};
            bool used[kTables] = {};
            for (int i = 0; i < kInstruments; ++i) if (i != instr && (s_.instrument(i)[6] & 0x20)) used[s_.instrument(i)[6] & 0x1F] = true;
            for (int t2 = 0; t2 < kTables && slot < 0; ++t2) {
                if (used[t2]) continue;
                bool empty = true;
                for (int c = 0; c < 6 && empty; ++c) for (int r = 0; r < 16; ++r) if (s_.at(uint16_t(o[c] + t2 * 16 + r))) { empty = false; break; }
                if (empty) slot = t2;
            }
            if (slot < 0 && (s_.instrument(instr)[6] & 0x20)) slot = -1;
            if (slot >= 0) for (int c = 0; c < 6; ++c) std::memcpy(s_.mem() + o[c] + slot * 16, in + 26 + c * 16, 16);
        }
        dst[6] = slot >= 0 ? uint8_t(0x20 | slot) : uint8_t(dst[6] & ~0x3F);   // no table, or no room for it: play without
    }
    s_.markInstrument(instr);
    return true;
}

void Lsdj::drawLibrary(Lcd& l) const {
    l.text(0, 0, "INSTRUMENT LIBRARY");
    char b[28];
    char nm[6]; instrName(instr, nm);
    std::snprintf(b, sizeof b, "SAVE I%02X %s AS...", instr, nm);
    l.text(0, 2, b, libSel == 0);
    if (!libCount) l.text(0, 4, "NO .GBI FILES SAVED YET");
    const int rows = 13;
    const int top = std::max(0, std::min(libSel - 1 - rows / 2, libCount - rows));
    for (int r = 0; r < rows && top + r < libCount; ++r) {
        std::snprintf(b, sizeof b, "%-24.24s", tr_ ? tr_->instrumentFile(top + r) : "");
        l.text(0, 4 + r, b, libSel == top + r + 1);
    }
    l.text(0, 17, status_[0] ? status_ : "INSERT=SAVE/LOAD B=BACK");
}

// ---- selection and clipboard --------------------------------------------------------------------------------------------------

bool Lsdj::selectable() const { return scr_ == Scr::Song || scr_ == Scr::Chain || scr_ == Scr::Phrase || scr_ == Scr::Table || scr_ == Scr::Groove; }
int Lsdj::rowsOnPage() const { return scr_ == Scr::Song ? kSongRows : 16; }
int Lsdj::colsOnPage() const { return scr_ == Scr::Song ? 4 : scr_ == Scr::Chain ? 2 : scr_ == Scr::Phrase ? 4 : scr_ == Scr::Table ? 6 : 1; }
int Lsdj::curRow() const {
    switch (scr_) { case Scr::Song: return songRow; case Scr::Chain: return chainPos; case Scr::Phrase: return step; case Scr::Table: return tableRow; default: return grooveStep; }
}
int Lsdj::curCol() const {
    switch (scr_) { case Scr::Song: return songCh; case Scr::Chain: return chainCol; case Scr::Phrase: return phraseCol; case Scr::Table: return tableCol; default: return 0; }
}
void Lsdj::setCursor(int row, int col) {
    switch (scr_) {
        case Scr::Song: songRow = row; songCh = col; break;
        case Scr::Chain: chainPos = row; chainCol = col; break;
        case Scr::Phrase: step = row; phraseCol = col; break;
        case Scr::Table: tableRow = row; tableCol = col; break;
        default: grooveStep = row; break;
    }
}

uint8_t* Lsdj::cellPtr(int row, int col) const {
    Song& s = s_;
    switch (scr_) {
        case Scr::Song: return &s.songCell(row, col);
        case Scr::Chain: return col == 0 ? &s.chainPhrase(chain, row) : &s.chainTrans(chain, row);
        case Scr::Phrase: return col == 0 ? &s.note(phrase, row) : col == 1 ? &s.phraseInstr(phrase, row) : col == 2 ? &s.phraseCmd(phrase, row) : &s.phraseVal(phrase, row);
        case Scr::Table: {
            static const uint16_t o[6] = {off::kTblEnv, off::kTblTrans, off::kTblCmd1, off::kTblVal1, off::kTblCmd2, off::kTblVal2};
            return &s.at(uint16_t(o[col] + table * 16 + row));
        }
        default: return s.groove(groove) + row;
    }
}

void Lsdj::markCell(int row, int col) {
    const uint8_t v = *cellPtr(row, col);
    if (scr_ == Scr::Song && v < kChains) s_.markChain(v);
    else if (scr_ == Scr::Chain && col == 0 && v < kPhrases) s_.markPhrase(v);
    else if (scr_ == Scr::Phrase) { s_.markPhrase(phrase); if (col == 1 && v < kInstruments) s_.markInstrument(v); }
}

void Lsdj::selMove(Key k) {
    if (!selOn_) { selOn_ = true; selRow_ = curRow(); selCol_ = curCol(); }
    const int r0 = curRow(), c0 = curCol();
    move(k);
    if (std::abs(curRow() - selRow_) >= kClipMaxRows) setCursor(r0, c0);      // the clipboard holds 64 rows
}

bool Lsdj::selected(int row, int col) const {
    if (!selOn_) return false;
    const int r0 = std::min(selRow_, curRow()), r1 = std::max(selRow_, curRow()), c0 = std::min(selCol_, curCol()), c1 = std::max(selCol_, curCol());
    return row >= r0 && row <= r1 && col >= c0 && col <= c1;
}

void Lsdj::copyKey(bool paste) {
    if (nameOpen_ || prompt_ || !selectable()) return;
    if (paste) doPaste(); else doCopy();
}

void Lsdj::doCopy() {
    int r0 = curRow(), r1 = r0, c0 = curCol(), c1 = c0;
    if (selOn_) { r0 = std::min(selRow_, r1); r1 = std::max(selRow_, curRow()); c0 = std::min(selCol_, c1); c1 = std::max(selCol_, curCol()); }
    clipPage_ = scr_;
    clipRows_ = r1 - r0 + 1;
    clipCols_ = c1 - c0 + 1;
    clipCol0_ = c0;
    for (int r = 0; r < clipRows_; ++r)
        for (int c = 0; c < clipCols_; ++c) clip_[r * kClipMaxCols + c] = *cellPtr(r0 + r, c0 + c);
    clearSel();
    msg_ = "COPY";
}

namespace {
bool phraseFree(Song& s, int p) {
    if (bit(s, off::kPhraseAlloc, p)) return false;
    for (int i = 0; i < 16; ++i) if (s.note(p, i) || s.phraseInstr(p, i) != 0xFF || s.phraseCmd(p, i)) return false;
    return true;
}
bool chainFree(Song& s, int c) {
    if (bit(s, off::kChainAlloc, c)) return false;
    for (int i = 0; i < 16; ++i) if (s.chainPhrase(c, i) != 0xFF) return false;
    return true;
}
}  // namespace

int Lsdj::clonePhrase(int src) {
    for (int p = 0; p < kPhrases; ++p) {
        if (p == src || !phraseFree(s_, p)) continue;
        for (int i = 0; i < 16; ++i) {
            s_.note(p, i) = s_.note(src, i); s_.phraseInstr(p, i) = s_.phraseInstr(src, i);
            s_.phraseCmd(p, i) = s_.phraseCmd(src, i); s_.phraseVal(p, i) = s_.phraseVal(src, i);
        }
        s_.markPhrase(p);
        return p;
    }
    return src;                                                     // none free: share the original
}

int Lsdj::cloneChain(int src) {
    for (int c = 0; c < kChains; ++c) {
        if (c == src || !chainFree(s_, c)) continue;
        s_.markChain(c);
        for (int i = 0; i < 16; ++i) {
            const uint8_t p = s_.chainPhrase(src, i);
            s_.chainPhrase(c, i) = (p < kPhrases && cloneDeep_) ? uint8_t(clonePhrase(p)) : p;
            s_.chainTrans(c, i) = s_.chainTrans(src, i);
        }
        return c;
    }
    return src;
}

void Lsdj::doPaste() {
    if (!clipRows_) { msg_ = "NONE"; return; }
    if (clipPage_ != scr_) { msg_ = "NOPE"; return; }                // data from another page would not make sense here
    const int cols = colsOnPage();
    const int c0 = scr_ == Scr::Song ? std::min(curCol(), cols - clipCols_) : clipCol0_;   // chains move between channels; other pages keep their columns
    const int r0 = curRow();
    for (int r = 0; r < clipRows_ && r0 + r < rowsOnPage(); ++r)
        for (int c = 0; c < clipCols_ && c0 + c < cols; ++c) {
            uint8_t v = clip_[r * kClipMaxCols + c];
            if (cloneDeep_ && scr_ == Scr::Song && v < kChains) v = uint8_t(cloneChain(v));
            else if (cloneDeep_ && scr_ == Scr::Chain && c0 + c == 0 && v < kPhrases) v = uint8_t(clonePhrase(v));
            *cellPtr(r0 + r, c0 + c) = v;
            markCell(r0 + r, c0 + c);
        }
    clearSel();
    msg_ = "PSTE";
}

// ---- drawing --------------------------------------------------------------------------------------------------------

namespace {
constexpr int kPx = 168;            // the side panel (channel, tempo, screen map) sits at the right edge of the 200 px display
constexpr int kPc = kPx / 8;        // its first text column
const char hx16[] = "0123456789ABCDEF";
}  // namespace

namespace {
void ptext(Lcd& l, int col, int row, const char* s, bool inv = false) { for (; *s; ++s, ++col) l.glyphAt(col * 8, row * 8, *s, inv, 3, inv ? 3 : 1); }
}  // namespace

void Lsdj::drawPanel(Lcd& l, bool full) const {
    if (full) l.rect(kPx, 0, 32, 112, 1);
    static const char* ch[4] = {"PU1", "PU2", "WAV", "NOI"};
    l.rect(kPx, 0, 32, 8, 3);
    l.text(kPc, 0, ch[scr_ == Scr::Song ? songCh : (scr_ == Scr::Project ? 0 : songCh)], true);
    char b[8];
    if (full) {
        l.glyphAt(kPx, 24, '\x01', false, 3, 1);
        std::snprintf(b, sizeof b, "%d", s_.at(off::kTempo));
        for (int i = 0; b[i]; ++i) l.glyphAt(kPx + 8 + i * 8, 24, b[i], false, 3, 1);
    }
    static const char lab[2][4] = {{'1', '2', '3', '4'}, {'5', '6', '7', '8'}};
    for (int c = 0; full && c < 4; ++c) {
        const bool on = p_.voiceOn(c) && !p_.muted(c);
        l.glyphAt(kPx + 8, 40 + c * 8, lab[console_][c], on, on ? 0 : 3, on ? 3 : (p_.muted(c) ? 0 : 1));
        if (p_.muted(c)) l.glyphAt(kPx + 16, 40 + c * 8, 'M', false, 3, 1);
    }
    // screen map
    auto mark = [&](int col, int row, char c, Scr s) { l.glyph(col, row, c, scr_ == s); };
    if (full) l.rect(160, 112, 40, 32, 0);
    mark(20, 14, 'P', Scr::Project); mark(22, 14, 'Y', Scr::Synth); mark(23, 14, 'W', Scr::Wave);
    mark(20, 16, 'S', Scr::Song); mark(21, 16, 'C', Scr::Chain); mark(22, 16, 'P', Scr::Phrase); mark(23, 16, 'I', Scr::Instr); mark(24, 16, 'T', Scr::Table);
    mark(22, 17, 'G', Scr::Groove);
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
        case Scr::Help: drawHelp(l); return;
        case Scr::Library: drawLibrary(l); break;
    }
    if (scr_ != Scr::File && scr_ != Scr::Library) drawPanel(l, scr_ != Scr::Wave);
    if (msg_[0] && scr_ != Scr::Wave) ptext(l, kPc, 4, msg_, true);
    else if (selOn_) ptext(l, kPc, 4, "SEL", true);
    if (recMode_ && scr_ == Scr::Phrase) {
        const uint8_t* in = s_.instrument(instr);
        static const char* const ty[4] = {"PUL", "WAV", "KIT", "NOI"};
        char ii[8]; std::snprintf(ii, sizeof ii, "I%02X", instr);
        char nm[6]; instrName(instr, nm); nm[4] = 0;
        ptext(l, kPc, 10, p_.running() ? "REC" : "ARM", p_.running());
        ptext(l, kPc, 11, ii); ptext(l, kPc, 12, ty[in[0] & 3]); ptext(l, kPc, 13, nm);
    }
    if (padOct_ != 0 && scr_ != Scr::File && scr_ != Scr::Library) { char o[8]; std::snprintf(o, sizeof o, "O%+d", padOct_); ptext(l, kPc, 9, o); }
    if (banner_[0]) { l.rect(0, 136, 200, 8, 0); l.text(0, 17, banner_); }
    if (prompt_) {
        l.rect(32, 32, 120, 56, 3);
        l.rect(34, 34, 116, 52, 0);
        l.text(5, 5, "OVERWRITE"); l.text(5, 6, "HAND-DRAWN"); l.text(5, 7, "WAVES?");
        l.text(5, 9, "YES", promptYes_); l.text(12, 9, "NO", !promptYes_);
    }
    if (popup_) drawPopup(l);
    if (nameOpen_) drawNameEntry(l);
    if (progLabel_[0]) drawProgress(l);
}

void Lsdj::drawSong(Lcd& l) const {
    l.text(0, 0, "SONG");
    l.text(5, 0, console_ ? "B 5-8" : "A 1-4");
    static const char* ch[4] = {"PU1", "PU2", "WAV", "NOI"};
    for (int c = 0; c < 4; ++c) for (int i = 0; i < 3; ++i) l.glyphAt(26 + c * 36 + i * 8, 8, ch[c][i], p_.muted(c), 2, 0);
    const int base = std::max(0, std::min(songRow - 8, 256 - 16));
    for (int r = 0; r < 16; ++r) {
        const int row = base + r;
        l.hex2(0, 2 + r, row);
        for (int c = 0; c < 4; ++c) {
            const uint8_t v = s_.songCell(row, c);
            const bool cur = row == songRow && c == songCh;
            char t[3];
            if (v == 0xFF) std::strcpy(t, "--"); else { t[0] = hx16[v >> 4]; t[1] = hx16[v & 15]; t[2] = 0; }
            for (int i = 0; i < 2; ++i) l.glyphAt(30 + c * 36 + i * 8, 16 + r * 8, t[i], cur, 3, selected(row, c) ? 1 : 0);
        }
        for (int c = 0; c < 4; ++c) if (p_.running() && p_.seq().active(c) && p_.seq().row(c) == row) l.glyphAt(20 + c * 36, 16 + r * 8, '\x05', false, 3, 0);
    }
}

void Lsdj::drawChain(Lcd& l) const {
    char t[16]; std::snprintf(t, sizeof t, "CHAIN %02X", chain); l.text(0, 0, t);
    const char* hdr1 = "PHR"; const char* hdr2 = "TSP";
    for (int i = 0; i < 3; ++i) { l.glyphAt(52 + i * 8, 8, hdr1[i], false, 2, 0); l.glyphAt(112 + i * 8, 8, hdr2[i], false, 2, 0); }
    for (int r = 0; r < 16; ++r) {
        l.glyph(0, 2 + r, hx16[r]);
        const uint8_t p = s_.chainPhrase(chain, r);
        char a[3] = {p == 0xFF ? '-' : hx16[p >> 4], p == 0xFF ? '-' : hx16[p & 15], 0};
        const uint8_t tr = s_.chainTrans(chain, r);
        char b[3] = {hx16[tr >> 4], hx16[tr & 15], 0};
        for (int i = 0; i < 2; ++i) {
            l.glyphAt(56 + i * 8, 16 + r * 8, a[i], r == chainPos && chainCol == 0, 3, selected(r, 0) ? 1 : 0);
            l.glyphAt(116 + i * 8, 16 + r * 8, b[i], r == chainPos && chainCol == 1, 3, selected(r, 1) ? 1 : 0);
        }
    }
    for (int c = 0; c < 4; ++c) if (p_.running() && p_.seq().active(c) && p_.seq().chain(c) == chain) l.glyphAt(36, 16 + p_.seq().chainPos(c) * 8, '\x05', false, 3, 0);
}

void Lsdj::drawPhrase(Lcd& l) const {
    char t[16]; std::snprintf(t, sizeof t, "PHRASE %02X", phrase); l.text(0, 0, t);
    const char* hd[3] = {"NOTE", "INSTR", "CMD"}; const int hx0[3] = {24, 76, 128};
    for (int h = 0; h < 3; ++h) for (int i = 0; hd[h][i]; ++i) l.glyphAt(hx0[h] + i * 6 - 2, 8, hd[h][i], false, 2, 0);
    for (int r = 0; r < 16; ++r) {
        l.glyph(0, 2 + r, hx16[r]);
        char nn[4]; noteName(s_.note(phrase, r), nn);
        for (int i = 0; i < 3; ++i) l.glyphAt(24 + i * 8, 16 + r * 8, nn[i], r == step && phraseCol == 0, 3, selected(r, 0) ? 1 : 0);
        const uint8_t in = s_.phraseInstr(phrase, r);
        char ib[3] = {in == 0xFF ? '-' : hx16[(in >> 4) & 15], in == 0xFF ? '-' : hx16[in & 15], 0};
        for (int i = 0; i < 2; ++i) l.glyphAt(80 + i * 8, 16 + r * 8, ib[i], r == step && phraseCol == 1, 3, selected(r, 1) ? 1 : 0);
        const uint8_t c = s_.phraseCmd(phrase, r), v = s_.phraseVal(phrase, r);
        l.glyphAt(128, 16 + r * 8, cmdLetter(c), r == step && phraseCol == 2, 3, selected(r, 2) ? 1 : 0);
        l.glyphAt(140, 16 + r * 8, hx16[v >> 4], r == step && phraseCol == 3, 3, selected(r, 3) ? 1 : 0);
        l.glyphAt(148, 16 + r * 8, hx16[v & 15], r == step && phraseCol == 3, 3, selected(r, 3) ? 1 : 0);
    }
    for (int c = 0; c < 4; ++c) if (p_.running() && p_.seq().active(c) && p_.seq().phrase(c) == phrase) l.glyphAt(12, 16 + p_.seq().step(c) * 8, '\x05', false, 3, 0);
}

void Lsdj::drawTable(Lcd& l) const {
    char t[16]; std::snprintf(t, sizeof t, "TABLE %02X", table); l.text(0, 0, t);
    const char* hd[4] = {"VOL", "TSP", "CMD", "CMD"}; const int hx0[4] = {16, 48, 88, 128};
    for (int h = 0; h < 4; ++h) for (int i = 0; hd[h][i]; ++i) l.glyphAt(hx0[h] + i * 6 - 2, 8, hd[h][i], false, 2, 0);
    const int o[6] = {off::kTblEnv, off::kTblTrans, off::kTblCmd1, off::kTblVal1, off::kTblCmd2, off::kTblVal2};
    for (int r = 0; r < 16; ++r) {
        l.glyph(0, 2 + r, hx16[r]);
        auto cell = [&](int col) { return s_.at(uint16_t(o[col] + table * 16 + r)); };
        auto two = [&](int x, int col) { const uint8_t v = cell(col); for (int i = 0; i < 2; ++i) l.glyphAt(x + i * 8, 16 + r * 8, hx16[i ? v & 15 : v >> 4], r == tableRow && tableCol == col, 3, selected(r, col) ? 1 : 0); };
        two(16, 0); two(48, 1);
        l.glyphAt(88, 16 + r * 8, cmdLetter(cell(2)), r == tableRow && tableCol == 2, 3, selected(r, 2) ? 1 : 0); two(100, 3);
        l.glyphAt(128, 16 + r * 8, cmdLetter(cell(4)), r == tableRow && tableCol == 4, 3, selected(r, 4) ? 1 : 0); two(140, 5);
    }
}

void Lsdj::drawGroove(Lcd& l) const {
    char t[16]; std::snprintf(t, sizeof t, "GROOVE %02X", groove); l.text(0, 0, t);
    for (int r = 0; r < 16; ++r) {
        l.glyph(0, 2 + r, hx16[r]);
        const uint8_t v = s_.groove(groove)[r];
        for (int i = 0; i < 2; ++i) l.glyphAt(24 + i * 8, 16 + r * 8, hx16[i ? v & 15 : v >> 4], r == grooveStep, 3, selected(r, 0) ? 1 : 0);
    }
}

// The name keyboard, in the layout of the ROM's own: digits, letters, then space, backspace and OK. Everything sits inside the border.
void Lsdj::drawNameEntry(Lcd& l) const {
    l.rect(8, 16, 184, 108, 3);
    l.rect(10, 18, 180, 104, 0);
    l.text(2, 3, "NAME");
    const int n = int(std::strlen(nameBuf_));
    for (int i = 0; i < nameMax_; ++i) l.glyphAt(64 + i * 8, 24, i < n ? nameBuf_[i] : ' ', i == n, 3, i == n ? 3 : 1);
    const int kx = 42;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; kKeyRows[r][c]; ++c) l.glyphAt(kx + c * 12, 44 + r * 12, kKeyRows[r][c], nameCy_ == r && nameCx_ == c, 3, 0);
    l.glyphAt(kx + 6 * 12, 44 + 3 * 12, '_', nameCy_ == 4 && nameCx_ == 6, 3, 0);
    l.glyphAt(kx + 7 * 12, 44 + 3 * 12, '<', nameCy_ == 4 && nameCx_ == 7, 3, 0);
    l.glyphAt(kx + 8 * 12, 44 + 3 * 12, 'O', nameCy_ == 4 && nameCx_ == 8, 3, 0);
    l.glyphAt(kx + 8 * 12 + 8, 44 + 3 * 12, 'K', nameCy_ == 4 && nameCx_ == 8, 3, 0);
    l.text(2, 12, "INSERT TYPE  PLAY OK");
    l.text(2, 13, "BKSP NO      < ERASE");
}

void Lsdj::drawProgress(Lcd& l) const {
    l.rect(20, 48, 160, 40, 3);
    l.rect(22, 50, 156, 36, 0);
    l.text(4, 7, progLabel_);
    l.frame(32, 72, 136, 10, 3);
    const int w = 132 * (progPct_ < 0 ? 0 : progPct_ > 100 ? 100 : progPct_) / 100;
    l.rect(34, 74, w, 6, 3);
}

namespace {
// Help text. A section is a title and entries; an entry is a bold name and a few words about it, wrapped to 23 characters and left aligned,
// with a blank line between entries. Sections turn into pages by themselves (13 lines each): an entry is never split across two pages.
// The same text feeds the Help screen, the F8 pop-up and the cheat sheets.
struct Entry { const char* term; const char* text; };
struct Section { const char* title; const Entry* e; int n; };
template <size_t N> constexpr Section sec(const char* t, const Entry (&e)[N]) { return {t, e, int(N)}; }

constexpr int kDocW = 23, kDocLines = 13;
struct DLine { const char* s; int len; bool bold; };

// The next row of wrapped text from p (which starts at a word): up to kDocW characters, broken at a space. Returns where the following row starts.
const char* wrapNext(const char* p, int& len) {
    const int n = int(std::strlen(p));
    if (n <= kDocW) { len = n; return p + n; }
    int cut = kDocW;
    while (cut > 0 && p[cut] != ' ') --cut;
    if (cut == 0) cut = kDocW;                          // one word longer than a row
    len = cut;
    const char* q = p + cut;
    while (*q == ' ') ++q;
    return q;
}

// The rows of one entry: its name (bold) and its wrapped text. Returns the count (at most cap).
int entryRows(const Entry& e, DLine* out, int cap) {
    int h = 0;
    if (e.term && e.term[0] && h < cap) out[h++] = {e.term, int(std::strlen(e.term)), true};
    for (const char* p = e.text; p && *p && h < cap;) {
        int len;
        const char* nx = wrapNext(p, len);
        out[h++] = {p, len, false};
        p = nx;
    }
    return h;
}

// Lays the section out in pages. Fills `out` with the rows of page `want` (when want >= 0); returns how many pages the section has.
int paginate(const Section& s, int want, DLine* out, int& count) {
    int page = 0, used = 0;
    count = 0;
    for (int i = 0; i < s.n; ++i) {
        DLine rows[kDocLines + 1];
        const int h = std::min(entryRows(s.e[i], rows, kDocLines + 1), kDocLines);
        if (used && used + 1 + h > kDocLines) { ++page; used = 0; }
        if (used) { if (page == want) out[count++] = {"", 0, false}; ++used; }
        for (int k = 0; k < h; ++k) { if (page == want) out[count++] = rows[k]; ++used; }
    }
    return page + 1;
}

// ---- the Help screen (these chapters follow one another) ---------------------------------------------------------------------

const Entry kWelcomeTxt[] = {
    {"", "PTSDJ makes music like a Game Boy."},
    {"", "You write small pieces and link them together."},
    {"PHRASE", "16 steps with notes in them."},
    {"CHAIN", "A list of phrases."},
    {"SONG", "Chains for all 4 tracks."},
    {"TWO SONGS", "PTSDJ has two Game Boys. Song A is tracks 1 to 4. Song B is tracks 5 to 8."},
};
const Entry kMovingTxt[] = {
    {"ARROWS", "Move the cursor."},
    {"SONG KEY", "Song page."},
    {"PATTERN KEY", "Chain page."},
    {"NOTE KEY", "Phrase page."},
    {"INSTR KEY", "Instrument page."},
    {"INSTR PARAMS KEY", "Table page."},
    {"FX1 AND FX2 KEYS", "Synth page and Wave page."},
    {"CONFIG KEY", "Project page."},
    {"PERFORMANCE KEY", "Groove page."},
    {"FILE KEY", "The song list."},
    {"SHIFT + ARROWS", "Move between the pages."},
};
const Entry kEditingTxt[] = {
    {"INSERT", "Puts in a value."},
    {"INSERT + LEFT/RIGHT", "Changes it by 1."},
    {"INSERT + UP/DOWN", "Changes it by 16."},
    {"JOG WHEEL", "Does the same as Insert + Left/Right."},
    {"BKSP", "Clears the cell."},
    {"SHIFT+INSERT+ARROWS", "Picks many cells. Then BKSP clears them all, or the jog wheel changes them all."},
    {"COPY", "Copies the cells."},
    {"SHIFT + COPY", "Pastes them. Same page only."},
};
const Entry kPlayingTxt[] = {
    {"PLAY", "Starts and stops the song."},
    {"PADS", "Play sounds. They never start the song."},
    {"SHIFT + A PAD ROW", "Sets the octave."},
    {"REC OFF", "On the Phrase page, the pads type notes at the cursor."},
    {"REC ON", "The pads only make sound. The jog wheel picks the sound."},
    {"REC + PLAY", "The phrase loops while the song plays. The pads write the notes. Play stops it."},
};
const Entry kNamesTxt[] = {
    {"", "To name a sound, go to the Instr page, move to NAME and press Insert."},
    {"INSERT", "Types a letter."},
    {"< KEY", "Erases a letter."},
    {"PLAY", "OK."},
    {"BKSP", "No."},
    {"SAMPLE LOADER KEY", "Saves a sound to the card, or loads one from it."},
};
const Entry kTwoSongsTxt[] = {
    {"SYNC", "On the Project page. MASTER sets the tempo. SLAVE follows it. Play starts both."},
    {"MASTER KEY", "Shows both songs side by side."},
    {"SHIFT + SONG KEY", "Switches the screen between song A and song B."},
    {"F1 AND F8", "In the side by side view, pick song A or B. Hold one to see what every F key does."},
    {"F2 / F7 + JOG", "Volume of song A / song B."},
    {"F3 TO F6", "Mute a track. Add Shift to play only that track."},
};
const Entry kSavingTxt[] = {
    {"", "Each Game Boy has its own save file on the SD card, in the GB/Saves folder. Song A is in A.SRM. Song B is in B.SRM."},
    {"", "A file holds up to 32 songs, like a real LSDJ cartridge."},
    {"SAVE SONG", "Puts the song on screen into its file. The same name replaces the old song. A new name adds a song."},
    {"LOAD SONG", "Lists the songs in the file. A + means a loose file from GB/Songs."},
    {"SHIFT + BKSP", "In the song list, erases the song under the cursor."},
    {"EXPORT WAV", "Makes an audio file."},
    {"EXPORT LSDJ", "Pick A, B or A+B with the jog wheel, then press Insert. It saves the song and copies the file to GB/Export as LSDJ_A.SRM or LSDJ_B.SRM."},
    {"KITS", "Drum kits live in the LSDJ ROM, not in the song. If you added kits in GB/Kits, EXPORT LSDJ also writes LSDJ_KITS.GB: your ROM with those kits in. Use it on the Game Boy."},
    {"ON A GAME BOY", "Give the copy the same name as your LSDJ ROM, like LSDJ.SRM."},
};
const Section kHelpDocs[] = {sec("WELCOME", kWelcomeTxt), sec("MOVING AROUND", kMovingTxt), sec("EDITING", kEditingTxt), sec("PLAYING", kPlayingTxt),
                             sec("NAMES, LIBRARY", kNamesTxt), sec("TWO SONGS", kTwoSongsTxt), sec("SAVING", kSavingTxt)};

// ---- the F8 pop-ups ------------------------------------------------------------------------------------------------------

const Entry kSongTxt[] = {
    {"", "The Song page is the big plan. Each row has 4 boxes, one for each track: PU1 PU2 WAV NOI."},
    {"A BOX", "Holds a chain number. -- means none. The tracks play down the rows together."},
    {"TO MAKE MUSIC", "Press Insert in a box. Then press > to open it."},
    {"PLAY", "Starts at the row you are on."},
    {"SHIFT + INSERT", "Mutes the track."},
    {"SHIFT + BKSP", "Mutes all tracks."},
    {"SHIFT + SONG KEY", "Swaps song A and song B."},
    {"BKSP", "Clears a box."},
    {"SHIFT+INSERT+ARROWS", "Pick many boxes."},
    {"COPY, SHIFT + COPY", "Copy and paste."},
};
const Entry kChainTxt[] = {
    {"", "A chain is a list of up to 16 phrases. They play one after another."},
    {"LEFT COLUMN", "The phrase."},
    {"RIGHT COLUMN", "Transpose. Higher or lower notes. 0C is one octave."},
    {"INSERT", "Fills a box."},
    {"> KEY", "Opens the phrase."},
    {"< KEY", "Goes back to the song."},
    {"BKSP + LEFT/RIGHT", "Switch chains."},
    {"BKSP", "Clears a box."},
    {"SHIFT+INSERT+ARROWS", "Pick many boxes."},
    {"COPY, SHIFT + COPY", "Copy and paste."},
    {"CLONE: DEEP", "On the Project page. Pasting makes new copies of the phrases."},
    {"CLONE: SLIM", "Pasting keeps the same phrases."},
};
const Entry kPhraseTxt[] = {
    {"", "A phrase has 16 steps. The notes live here."},
    {"NOTE", "The pitch."},
    {"INSTR", "Which sound."},
    {"CMD", "A special trick. Hold Shift + Note to see them all."},
    {"INSERT", "Puts in a note."},
    {"LEFT / RIGHT", "Moves the note by 1."},
    {"UP / DOWN", "Moves it by an octave."},
    {"PADS, REC OFF", "Press a pad to type the note at the cursor."},
    {"PADS, REC ON", "The pads make sound but write nothing. The jog wheel picks the sound."},
    {"REC + PLAY", "The phrase loops while the song plays. The pads write the notes. Play stops it."},
    {"BKSP", "Clears a step."},
    {"BKSP + UP/DOWN", "Go to another phrase."},
    {"SHIFT+INSERT+ARROWS", "Pick many steps. The jog wheel changes them all."},
    {"COPY, SHIFT + COPY", "Copy and paste."},
};
const Entry kInstrTxt[] = {
    {"", "An instrument is a sound you can play."},
    {"THE NUMBER", "Move up to the number at the top. Then use < > or the jog wheel to pick one. The pads play it as you go."},
    {"TYPE", "Pulse, wave, kit or noise. The rows below change with the type."},
    {"NAME", "Press Insert to type a name."},
    {"ENVELOPE", "How the volume goes up and down."},
    {"TABLE", "A list of extra steps for this sound."},
    {"OUTPUT", "Left, right or both speakers."},
    {"LENGTH", "How long a note lasts."},
    {"SAMPLE LOADER KEY", "Saves and loads sounds."},
};
const Entry kTableTxt[] = {
    {"", "A table is a tiny pattern that plays by itself with a sound."},
    {"VOL", "Loudness."},
    {"TSP", "Pitch change."},
    {"CMD", "A special trick. Two per row. Hold Shift + Instr Params to see them all."},
    {"TO USE IT", "Set TABLE on the Instr page."},
    {"BKSP + LEFT/RIGHT", "Pick another table."},
    {"BKSP", "Clears a cell."},
    {"SHIFT+INSERT+ARROWS", "Pick many cells. The jog wheel changes them all."},
    {"COPY, SHIFT + COPY", "Copy and paste."},
};
const Entry kGrooveTxt[] = {
    {"", "A groove sets how long each step lasts. The numbers are ticks."},
    {"6 AND 6", "An even beat."},
    {"7 AND 5", "Swing."},
    {"", "The list repeats."},
    {"COMMAND G", "In a phrase, picks a groove number."},
    {"BKSP + LEFT/RIGHT", "Pick another groove."},
};
const Entry kProjectTxt[] = {
    {"", "Settings for the song."},
    {"NAME", "Press Insert to type it."},
    {"TEMPO", "Bigger is faster."},
    {"TRANSPOSE", "All notes up or down."},
    {"SYNC", "Off, master or slave. For two songs."},
    {"CLONE", "Deep or slim."},
    {"LOOK", "Screen colors."},
    {"PRELISTEN", "The pads make sound when on."},
    {"CLEAN SONG DATA", "Frees chains and phrases you do not use. Press Insert twice."},
    {"CLEAN INSTR DATA", "Does the same for sounds."},
    {"SAVE SONG", "Puts the song in this Game Boy's save file on the SD card."},
    {"LOAD SONG", "Lists the songs in that file."},
    {"EXPORT WAV", "Makes an audio file."},
    {"EXPORT LSDJ", "Pick A, B or A+B with the jog wheel, then press Insert. Copies the save files to GB/Export."},
};
const Entry kSynthTxt[] = {
    {"", "Synths make the shapes the WAV track plays."},
    {"EACH ROW", "Shapes the sound: filter, distort, phase and volume."},
    {"LEFT / RIGHT", "Picks a digit."},
    {"INSERT + ARROWS", "Changes a value."},
    {"PLAY", "Hears the synth."},
    {"BKSP + LEFT/RIGHT", "Picks another synth."},
    {"", "Needs GB/LSDJ.GB on the card."},
};
const Entry kWaveTxt[] = {
    {"", "A wave is 32 dots that make the shape of a sound."},
    {"< >", "Moves along."},
    {"UP / DOWN", "Moves a dot."},
    {"PLAY", "Hears the wave."},
    {"BKSP + LEFT/RIGHT", "Picks another wave."},
    {"BKSP (TAP)", "Copies the wave."},
    {"SHIFT + INSERT", "Pastes it."},
};
const Entry kFileTxt[] = {
    {"", "These are the songs for this Game Boy."},
    {"UP / DOWN", "Choose a song."},
    {"INSERT", "Loads it into the song on screen."},
    {"- BEFORE A NAME", "A song in the save file."},
    {"+ BEFORE A NAME", "A loose file from GB/Songs. Saving files it in the save file."},
    {"SHIFT + BKSP", "Erases the song under the cursor. Press Insert to say yes."},
    {"BKSP", "Go back."},
};
const Entry kLibraryTxt[] = {
    {"", "The library keeps your sounds on the card."},
    {"TOP ROW", "Saves the sound on screen. Press Insert, type a name, then press Play."},
    {"OTHER ROWS", "Load a file into the sound on screen."},
    {"BKSP", "Go back."},
};

// ---- cheat sheets (Shift + Note, Shift + Instr Params) -------------------------------------------------------------------

const Entry kPhraseColsTxt[] = {
    {"NOTE", "The pitch."},
    {"INSTR", "Which sound."},
    {"CMD", "A special trick. It is a letter and a 2-digit number."},
    {"x AND y", "Each is one digit, 0 to F."},
    {"xx", "Both digits as one number, 00 to FF."},
    {"", "Turn the page with < > to see every command."},
};
const Entry kTableColsTxt[] = {
    {"VOL", "Loudness."},
    {"TSP", "Pitch change."},
    {"CMD", "A special trick. It is a letter and a 2-digit number. Two per row."},
    {"x AND y", "Each is one digit, 0 to F."},
    {"xx", "Both digits as one number, 00 to FF."},
    {"", "Turn the page with < > to see every command."},
};
const Entry kCmdAFTxt[] = {
    {"A xx", "Start table xx. A20 stops it."},
    {"B xy", "Maybe play the note. A big number means often."},
    {"C xy", "Chord: the note, then +x and +y steps."},
    {"D xx", "Delay the note by xx ticks."},
    {"E xy", "Volume x, then fade speed y."},
    {"F xy", "Fine tune (pulse)."},
};
const Entry kCmdGOTxt[] = {
    {"G xx", "Groove number."},
    {"H xx", "Hop to step x of the next phrase. HFF stops everything."},
    {"K xx", "Stop the note after xx ticks."},
    {"L xx", "Slide to the new note in xx ticks."},
    {"M xy", "Master volume."},
    {"O xx", "Output. 0 is off, 1 left, 2 right, 3 both."},
};
const Entry kCmdPZTxt[] = {
    {"P xx", "Pitch bend. FF bends down."},
    {"R xy", "Repeat the note every y ticks."},
    {"S xy", "Sweep (pulse 1) or shape (noise)."},
    {"T xx", "Tempo."},
    {"V xy", "Vibrato: x is speed, y is depth."},
    {"W xx", "Pulse width."},
    {"Z xy", "Randomize the last command."},
};
const Section kPhraseSheetDocs[] = {sec("PHRASE COLUMNS", kPhraseColsTxt), sec("COMMANDS A TO F", kCmdAFTxt), sec("COMMANDS G TO O", kCmdGOTxt), sec("COMMANDS P TO Z", kCmdPZTxt)};
const Section kTableSheetDocs[] = {sec("TABLE COLUMNS", kTableColsTxt), sec("COMMANDS A TO F", kCmdAFTxt), sec("COMMANDS G TO O", kCmdGOTxt), sec("COMMANDS P TO Z", kCmdPZTxt)};
const Section kSongDocs[] = {sec("SONG PAGE", kSongTxt)};
const Section kChainDocs[] = {sec("CHAIN PAGE", kChainTxt)};
const Section kPhraseDocs[] = {sec("PHRASE PAGE", kPhraseTxt)};
const Section kInstrDocs[] = {sec("INSTRUMENT PAGE", kInstrTxt)};
const Section kTableDocs[] = {sec("TABLE PAGE", kTableTxt)};
const Section kGrooveDocs[] = {sec("GROOVE PAGE", kGrooveTxt)};
const Section kProjectDocs[] = {sec("PROJECT PAGE", kProjectTxt)};
const Section kSynthDocs[] = {sec("SYNTH PAGE", kSynthTxt)};
const Section kWaveDocs[] = {sec("WAVE PAGE", kWaveTxt)};
const Section kFileDocs[] = {sec("SONG FILES", kFileTxt)};
const Section kLibraryDocs[] = {sec("LIBRARY", kLibraryTxt)};

#define DOC_SET(a) (n = int(sizeof a / sizeof *a), a)
const Section* docsFor(Lsdj::Popup p, Scr s, int& n) {
    n = 0;
    if (p == Lsdj::kPhraseSheet) return DOC_SET(kPhraseSheetDocs);
    if (p == Lsdj::kTableSheet) return DOC_SET(kTableSheetDocs);
    switch (s) {
        case Scr::Song: return DOC_SET(kSongDocs);
        case Scr::Chain: return DOC_SET(kChainDocs);
        case Scr::Phrase: return DOC_SET(kPhraseDocs);
        case Scr::Instr: return DOC_SET(kInstrDocs);
        case Scr::Table: return DOC_SET(kTableDocs);
        case Scr::Groove: return DOC_SET(kGrooveDocs);
        case Scr::Project: return DOC_SET(kProjectDocs);
        case Scr::Synth: return DOC_SET(kSynthDocs);
        case Scr::Wave: return DOC_SET(kWaveDocs);
        case Scr::File: return DOC_SET(kFileDocs);
        case Scr::Library: return DOC_SET(kLibraryDocs);
        default: return DOC_SET(kHelpDocs);
    }
}
#undef DOC_SET

int totalPages(const Section* d, int n) {
    int pages = 0, c;
    for (int i = 0; i < n; ++i) pages += paginate(d[i], -1, nullptr, c);
    return pages;
}

// A page of text, counted across the sections: a title bar with the page counter, up to 13 left aligned rows starting one column in from the edge
// (names in bold), and a hint at the bottom.
void drawDoc(Lcd& l, const Section* d, int n, int page, int x0, int w, const char* foot) {
    int total = totalPages(d, n), first = 0;
    page = clampi(page, 0, std::max(0, total - 1));
    int s = 0, c;
    for (; s < n - 1; ++s) {
        const int pages = paginate(d[s], -1, nullptr, c);
        if (page < first + pages) break;
        first += pages;
    }
    DLine rows[kDocLines + 1];
    paginate(d[s], page - first, rows, c);
    char t[24];
    std::snprintf(t, sizeof t, "%d/%d", page + 1, total);
    l.rect(x0, 8, w, 8, 3);
    l.text(1, 1, d[s].title, true);
    l.text(23 - int(std::strlen(t)), 1, t, true);                // the page counter sits at the right end of the title bar
    for (int i = 0; i < c && i < kDocLines; ++i) {
        if (!rows[i].len) continue;
        char row[kDocW + 1];
        std::memcpy(row, rows[i].s, size_t(rows[i].len));
        row[rows[i].len] = 0;
        if (rows[i].bold) l.boldText(1, 3 + i, row); else l.text(1, 3 + i, row);
    }
    l.text(1, 16, foot);
}
}  // namespace

bool Lsdj::docsFit() {
    auto fits = [](const Section* d, int n) {
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < d[i].n; ++k) {
                const Entry& e = d[i].e[k];
                if (e.term && std::strlen(e.term) > size_t(kDocW)) return false;
                DLine rows[kDocLines + 2];
                if (entryRows(e, rows, kDocLines + 2) > kDocLines) return false;          // an entry has to fit on a page
                for (const char* p = e.text; p && *p;) {                                   // no word longer than a row
                    int len;
                    const char* nx = wrapNext(p, len);
                    if (len == kDocW && p[len] != ' ' && p[len] != 0) return false;
                    p = nx;
                }
            }
        return true;
    };
    int n;
    bool ok = true;
    for (Popup p : {kPhraseSheet, kTableSheet}) { const Section* d = docsFor(p, Scr::Song, n); ok = ok && fits(d, n); }
    for (Scr s : {Scr::Song, Scr::Chain, Scr::Phrase, Scr::Instr, Scr::Table, Scr::Groove, Scr::Project, Scr::Synth, Scr::Wave, Scr::File, Scr::Library, Scr::Help}) {
        const Section* d = docsFor(kPageHelp, s, n);
        ok = ok && fits(d, n);
    }
    return ok;
}

int Lsdj::popupPages() const { int n; const Section* d = docsFor(popup_, scr_, n); return totalPages(d, n); }
void Lsdj::popupTurn(int d) { popupPage_ = clampi(popupPage_ + d, 0, std::max(0, popupPages() - 1)); }
int Lsdj::helpPages() const { int n; const Section* d = docsFor(kPageHelp, Scr::Help, n); return totalPages(d, n); }

void Lsdj::drawPopup(Lcd& l) const {
    int n;
    const Section* d = docsFor(popup_, scr_, n);
    const int pages = totalPages(d, n);
    l.rect(0, 0, 200, 144, 3);                       // covers the page and the side panel; a border of the dark shade
    l.rect(2, 2, 196, 140, 0);
    drawDoc(l, d, n, clampi(popupPage_, 0, pages - 1), 2, 196, pages > 1 ? "PAGE: < > OR F1 F2" : "");
}

void Lsdj::drawHelp(Lcd& l) const {
    int n;
    const Section* d = docsFor(kPageHelp, Scr::Help, n);
    drawDoc(l, d, n, clampi(helpPage, 0, helpPages() - 1), 0, 200, "<> PAGE  BKSP EXIT");
}

// ---- Wave and Synth screens ---------------------------------------------------------------------------------------------

int Lsdj::waveSample(int i) const { const uint8_t b = s_.wave(waveNo)[(i & 31) >> 1]; return (i & 1) ? (b & 15) : (b >> 4); }

void Lsdj::setWaveSample(int i, int v) {
    uint8_t& b = s_.wave(waveNo)[(i & 31) >> 1];
    b = (i & 1) ? uint8_t((b & 0xF0) | v) : uint8_t((b & 0x0F) | (v << 4));
}

void Lsdj::waveEdit(Key k) {
    markHandDrawn(waveNo);
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
    if (handDrawn(synth)) { prompt_ = true; promptYes_ = false; return; }     // a hand-drawn wave of this synth would be overwritten: ask first, apply nothing
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
    l.boldText(0, 0, console_ ? "LOAD SONG B" : "LOAD SONG A");
    if (!fileCount) { l.text(0, 2, "NO SONGS YET"); l.text(0, 3, "SAVE SONG ADDS ONE"); return; }
    l.text(0, 1, status_[0] ? status_ : "SHIFT+BKSP=ERASE");
    // a blank row, then the songs: a space and a dash before each title (a plus for a loose file from GB/Songs)
    const int rows = 15, top = std::max(0, std::min(fileSel - rows / 2, fileCount - rows));
    for (int r = 0; r < rows && top + r < fileCount; ++r) {
        const char* nm = tr_ ? tr_->songName(top + r) : "";
        char s[32];
        if (nm[0] == '+') std::snprintf(s, sizeof s, " +%-17.17s", nm + 1);
        else std::snprintf(s, sizeof s, " -%-17.17s", nm);
        l.text(0, 3 + r, s, top + r == fileSel);
    }
}

void Lsdj::drawProject(Lcd& l) const {
    char b[24];
    std::snprintf(b, sizeof b, "PROJECT %s", name_[0] ? name_ : "(NEW)");
    l.text(0, 0, b);
    auto row = [&](int y, const char* nm, const char* val, bool cur) { l.text(0, y, nm, cur && !val[0]); if (val[0]) l.text(14, y, val, cur); };   // a row with no value is selected by its name
    const int mode = tr_ ? tr_->syncMode(console_) : (s_.at(off::kSync) ? 1 : 0);
    row(2, "NAME", name_[0] ? name_ : "-", projRow == 0);
    std::snprintf(b, sizeof b, "%02X", s_.at(off::kTempo)); row(3, "TEMPO", b, projRow == 1);
    std::snprintf(b, sizeof b, "%02X", s_.at(off::kTranspose)); row(4, "TRANSPOSE", b, projRow == 2);
    row(5, "SYNC", mode == 1 ? "MASTER" : mode == 2 ? "SLAVE" : "OFF", projRow == 3);
    row(6, "CLONE", cloneDeep_ ? "DEEP" : "SLIM", projRow == 4);
    row(7, "LOOK", Lcd::lookName(tr_ ? tr_->look() : 0), projRow == 5);
    row(8, "PRELISTEN", prelisten_ ? "ON" : "OFF", projRow == 6);
    row(9, "HELP", "", projRow == 7);
    row(10, "CLEAN SONG DATA", "", projRow == 8);
    row(11, "CLEAN INSTR DATA", "", projRow == 9);
    row(12, console_ ? "SAVE SONG B" : "SAVE SONG A", "", projRow == 10);
    row(13, console_ ? "LOAD SONG B" : "LOAD SONG A", "", projRow == 11);
    row(14, "EXPORT WAV", "", projRow == 12);
    row(15, "EXPORT LSDJ", exportSel == 0 ? "A" : exportSel == 1 ? "B" : "A+B", projRow == 13);
    l.text(0, 17, status_);
    l.text(0, 16, audio_[0] ? audio_ : "WORK TIME: 0:00");
}

void Lsdj::drawInstr(Lcd& l) const {
    l.text(0, 0, "INSTRUMENT");
    { char t[4]; std::snprintf(t, sizeof t, "%02X", instr); l.text(11, 0, t, instrRow < 0); }
    const uint8_t* in = s_.instrument(instr);
    char nm[6]; instrName(instr, nm);
    l.text(0, 2, "NAME"); l.rect(72, 16, 40, 8, 1); l.text(9, 2, nm, instrRow == 0);
    l.text(0, 3, "TYPE"); l.text(9, 3, kTypes[in[0] & 3], instrRow == 1);
    const FieldSet fs = fieldsFor(in[0]);
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
            default: v[0] = hx16[(raw >> 4) & 15]; v[1] = hx16[raw & 15]; v[2] = 0; break;
        }
        l.text(9, f.row, v, instrRow == 2 + i);
    }
}

}  // namespace gb
