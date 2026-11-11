#include "gb/kitbank.h"
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
constexpr int kMaxNote = 108;                 // the highest note the ROM lets you enter (0x6C)
// One editable value, and a row of the instrument page with up to three of them side by side (Left / Right move between them, as in the ROM).
struct Sub { int byte, mask, shift, kind; const char* const* names; };
struct Row { const char* label; int row, n; char sep; Sub s[3]; };
const char* const kDuty[] = {"12.5", "25", "50", "75"};
const char* const kOut[] = {"OFF", "L", "R", "LR"};
const char* const kOnOff[] = {"OFF", "ON"};
const char* const kTranspose[] = {"ON", "OFF"};                 // byte 5 bit 5 set = plays untransposed
const char* const kTypes[] = {"PULSE", "WAVE", "KIT", "NOISE"};
const char* const kPlay[] = {"MANUAL", "ONCE", "LOOP", "PINGPONG"};
const char* const kTableMode[] = {"OFF", "ON", "STEP"};
const char* const kFreeSafe[] = {"FREE", "SAFE"};
const char* const kKitSpeed[] = {"1X", "0.5X"};
const char* const kClip[] = {"HARD", "SOFT", "FOLD", "WRAP"};
// The rows and the bytes behind them, measured by driving the LSDJ 9.4.2 ROM's instrument pages (docs/LSDJ_FORMAT.md, "Instrument pages in the ROM").
// kind: 0 hex, 1 enum, 2 length (unlimited or 0..3F with the enable bit), 4 sweep (xor 80), 5 volume 0-3 (the NR32 code in bits 6-5: 3 = 20, 2 = 40, 1 = 60, 0 = 00),
//       6 wave frame: all of byte 3 while PLAY is MANUAL, its high nibble (the synth) otherwise, 7 frame speed (byte 11 as a signed number, shown + 4),
//       8 frame count (15 minus the low nibble), 9 loop position (byte 2's low nibble counted from byte 10's, F = none), 10 the table's mode (OFF, ON, STEP: byte 6
//       bit 5 and byte 5 bit 3), 11 the table number (byte 6, -- while OFF), 12 hex where 0 reads ALL, 14 FREE / SAFE (byte non-zero), 15 pitch: the low three bits,
//       a big step flips bit 4 (bits 3 and 5 belong to STEP and TRANSP, and a kit's bits 5 and 6 to LOOP), 16 a kit number (hex, shown one higher: the ROM counts from 01).
#define ENV3 {1, 0xFF, 0, 0, nullptr}, {9, 0xFF, 0, 0, nullptr}, {10, 0xFF, 0, 0, nullptr}
#define TABLE_ROW {"TABLE", 17, 2, ' ', {{6, 0, 0, 10, kTableMode}, {6, 0x1F, 0, 11, nullptr}}}
const Row kPulse[] = {{"ENV.", 5, 3, '/', {ENV3}},
                      {"WAVE", 6, 1, '/', {{7, 0xC0, 6, 1, kDuty}}}, {"OUTPUT", 7, 1, '/', {{7, 3, 0, 1, kOut}}}, {"LENGTH", 8, 1, '/', {{3, 0x7F, 0, 2, nullptr}}},
                      {"SWEEP", 9, 1, '/', {{4, 0xFF, 0, 4, nullptr}}}, {"PITCH", 10, 1, '/', {{5, 0x17, 0, 15, nullptr}}},
                      {"TRANSP.", 11, 1, '/', {{5, 0x20, 5, 1, kTranspose}}}, {"PU2 TSP", 12, 1, '/', {{2, 0xFF, 0, 0, nullptr}}},
                      {"FINETUNE", 13, 1, '/', {{11, 0xFF, 0, 0, nullptr}}}, {"CMD/RATE", 14, 1, '/', {{8, 0xFF, 0, 0, nullptr}}}, TABLE_ROW};
const Row kWave[] = {{"VOLUME", 5, 2, '/', {{1, 0xFF, 0, 5, nullptr}, {7, 3, 0, 1, kOut}}}, {"PITCH", 6, 1, '/', {{5, 0x17, 0, 15, nullptr}}},
                     {"TRANSP.", 7, 1, '/', {{5, 0x20, 5, 1, kTranspose}}}, {"FINETUNE", 8, 1, '/', {{12, 0xFF, 0, 0, nullptr}}},
                     {"CMD/RATE", 9, 1, '/', {{8, 0xFF, 0, 0, nullptr}}}, {"PLAY", 11, 1, '/', {{9, 3, 0, 1, kPlay}}},
                     {"WAVE", 12, 1, '/', {{3, 0xFF, 0, 6, nullptr}}}, {"SPEED", 13, 1, '/', {{11, 0xFF, 0, 7, nullptr}}},
                     {"LENGTH", 14, 1, '/', {{10, 0x0F, 0, 8, nullptr}}}, {"LOOP POS", 15, 1, '/', {{2, 0x0F, 0, 9, nullptr}}}, TABLE_ROW};
const Row kKit[] = {{"KIT 1", 5, 1, '/', {{9, 0x7F, 0, 16, nullptr}}}, {"KIT 2", 6, 1, '/', {{2, 0x3F, 0, 16, nullptr}}},
                    {"VOLUME", 7, 2, '/', {{1, 0xFF, 0, 5, nullptr}, {7, 3, 0, 1, kOut}}}, {"PITCH", 8, 1, '/', {{5, 0x17, 0, 15, nullptr}}},
                    {"FINETUNE", 9, 1, '/', {{8, 0xFF, 0, 0, nullptr}}}, {"OFFSET", 10, 2, '/', {{12, 0xFF, 0, 0, nullptr}, {13, 0xFF, 0, 0, nullptr}}},
                    {"LEN", 11, 2, '/', {{3, 0xFF, 0, 12, nullptr}, {11, 0xFF, 0, 12, nullptr}}},
                    {"LOOP", 12, 2, '/', {{5, 0x40, 6, 1, kOnOff}, {5, 0x20, 5, 1, kOnOff}}}, {"SPEED", 13, 1, '/', {{2, 0x40, 6, 1, kKitSpeed}}},
                    {"CLIP", 14, 1, '/', {{10, 3, 0, 1, kClip}}}, TABLE_ROW};
const Row kNoise[] = {{"ENV.", 5, 3, '/', {ENV3}}, {"OUTPUT", 6, 1, '/', {{7, 3, 0, 1, kOut}}}, {"LENGTH", 7, 1, '/', {{3, 0x7F, 0, 2, nullptr}}},
                      {"PITCH", 8, 1, '/', {{2, 0xFF, 0, 14, kFreeSafe}}}, {"VIBRATO", 9, 1, '/', {{5, 0x17, 0, 15, nullptr}}},
                      {"TRANSP.", 10, 1, '/', {{5, 0x20, 5, 1, kTranspose}}}, {"CMD/RATE", 11, 1, '/', {{8, 0xFF, 0, 0, nullptr}}}, TABLE_ROW};
#undef ENV3
#undef TABLE_ROW
struct RowSet { const Row* r; int n; };
RowSet rowsFor(int type) {
    switch (type & 3) {
        case 0: return {kPulse, int(sizeof kPulse / sizeof *kPulse)};
        case 1: return {kWave, int(sizeof kWave / sizeof *kWave)};
        case 2: return {kKit, int(sizeof kKit / sizeof *kKit)};
        default: return {kNoise, int(sizeof kNoise / sizeof *kNoise)};
    }
}

int tableMode(const uint8_t* in) { return !(in[6] & 0x20) ? 0 : (in[5] & 8) ? 2 : 1; }

// What a value looks like on the page.
void subText(const uint8_t* in, const Sub& f, char* v) {
    const int b = in[f.byte], raw = (b & f.mask) >> f.shift;
    switch (f.kind) {
        case 1: std::snprintf(v, 12, "%s", f.names[raw]); break;
        case 2: if (b & 0x40) std::snprintf(v, 12, "%02X", b & 0x3F); else std::strcpy(v, "UNLIM"); break;
        case 4: std::snprintf(v, 12, "%02X", b ^ 0x80); break;
        case 5: std::snprintf(v, 12, "%d", (4 - ((b >> 5) & 3)) & 3); break;
        case 6: if (in[9] & 3) std::snprintf(v, 12, "%X", b >> 4); else std::snprintf(v, 12, "%02X", b); break;
        case 7: std::snprintf(v, 12, "%02X", (int8_t(b) + 4) & 0xFF); break;
        case 8: std::snprintf(v, 12, "%X", 15 - (b & 15)); break;
        case 9: if ((b & 15) == 15) std::strcpy(v, "-"); else std::snprintf(v, 12, "%X", std::max(0, (b & 15) - (in[10] & 15))); break;
        case 10: std::snprintf(v, 12, "%s", kTableMode[tableMode(in)]); break;
        case 11: if (in[6] & 0x20) std::snprintf(v, 12, "%02X", b & 0x1F); else std::strcpy(v, "--"); break;
        case 12: if (b) std::snprintf(v, 12, "%02X", b); else std::strcpy(v, "ALL"); break;
        case 14: std::snprintf(v, 12, "%s", f.names[b != 0]); break;
        case 15: std::snprintf(v, 12, "%02X", b & f.mask); break;
        case 16: std::snprintf(v, 12, "%02X", ((b & f.mask) + 1) & 0xFF); break;
        default: std::snprintf(v, 12, "%02X", raw & 0xFF); break;
    }
}

// One step of Insert + an arrow on a value: `d` is the step (16 for Up / Down), `sgn` its direction.
void subEdit(uint8_t* in, const Sub& f, int d, int sgn, bool big) {
    uint8_t& b = in[f.byte];
    switch (f.kind) {
        case 2: {                                          // length: unlimited, or 0..3F with the enable bit
            const int len = clampi(((b & 0x40) ? (b & 0x3F) : -1) + d, -1, 0x3F);
            b = len < 0 ? uint8_t(b & ~0x7F) : uint8_t((b & ~0x7F) | 0x40 | len);
            break;
        }
        case 4: b = uint8_t(((b ^ 0x80) + d) ^ 0x80); break;
        case 5: {                                          // volume 0-3 stored as the NR32 code: 3 = 20, 2 = 40, 1 = 60, 0 = 00
            const int vol = clampi(((4 - ((b >> 5) & 3)) & 3) + sgn, 0, 3);
            b = vol ? uint8_t(0x20 * (4 - vol)) : 0;
            break;
        }
        case 6:                                            // the wave: all of byte 3 by hand, only the synth (its high nibble) when the instrument plays the frames itself
            if (in[9] & 3) b = uint8_t((clampi((b >> 4) + sgn, 0, 15) << 4) | (b & 15));
            else b = uint8_t(b + d);
            break;
        case 7: b = uint8_t(clampi(int8_t(b) + d, -3, 127)); break;                                   // shown + 4: at least 1 tick a frame
        case 8: b = uint8_t((b & 0xF0) | (15 - clampi(15 - (b & 15) + d, 0, 15))); break;
        case 9: {                                          // not before the first frame of the animation: a step that would go there does nothing (measured)
            const int t = std::min(15, (b & 15) + d);
            if (t >= (in[10] & 15)) b = uint8_t((b & 0xF0) | t);
            break;
        }
        case 10: {                                         // OFF > ON (byte 6 bit 5) > STEP (byte 5 bit 3)
            const int m = clampi(tableMode(in) + sgn, 0, 2);
            if (m == 0) in[6] = uint8_t(in[6] & ~0x20);
            else { in[6] = uint8_t(in[6] | 0x20); in[5] = m == 2 ? uint8_t(in[5] | 8) : uint8_t(in[5] & ~8); }
            break;
        }
        case 11: if (b & 0x20) b = uint8_t(0x20 | clampi((b & 0x1F) + d, 0, kTables - 1)); break;      // while the table is OFF the number cannot be changed (measured)
        case 12: b = uint8_t(clampi(b + d, 0, 255)); break;
        case 14: b = sgn > 0 ? 1 : 0; break;
        case 15:                                           // the low three bits; a big step flips bit 4
            if (big) b = uint8_t(b ^ 0x10); else b = uint8_t((b & ~7) | clampi((b & 7) + sgn, 0, 7));
            break;
        default: {                                         // hex (kind 0) and enums (kind 1)
            const int before = (b & f.mask) >> f.shift, maxv = f.mask >> f.shift;
            const int v = f.kind == 1 ? clampi(before + sgn, 0, maxv) : clampi(before + d, 0, maxv);
            b = uint8_t((b & ~f.mask) | (v << f.shift));
            if ((in[0] & 3) == 1 && f.byte == 9 && v > before) in[3] &= 0xF0;      // a wave's PLAY steps up: the ROM starts from frame 0 of the synth (measured)
            break;
        }
    }
}
}  // namespace

namespace {
// What LSDJ writes into an instrument the first time it is used: a sound you can hear at once. (A new song's instruments are all zeros, which is silence.)
const uint8_t kInstrDefault[4][16] = {
    {0x00, 0xA0, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x03, 0x00, 0x00, 0xD0, 0x00, 0x00, 0x00, 0xF3, 0x00},      // pulse
    {0x01, 0xA8, 0x0F, 0x00, 0xFF, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},      // wave
    {0x02, 0xA8, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x03, 0x00, 0x00, 0xD0, 0x00, 0x00, 0x00, 0xF3, 0x00},      // kit (D0 and F3 as in every LSDJ kit instrument: with 0 in byte 10 a layered note plays garbage)
    {0x03, 0xA0, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x03, 0x00, 0x00, 0xD0, 0x00, 0x00, 0x00, 0xF3, 0x00},      // noise
};
}  // namespace

// An empty instrument (all zeros) gets the pulse default before it is shown, heard or put in a phrase.
void Lsdj::fixInstr(int i) {
    uint8_t* in = s_.instrument(i & 63);
    for (int k = 0; k < 16; ++k) if (in[k]) return;
    std::memcpy(in, kInstrDefault[0], 16);
}

int Lsdj::instrRows() const { return 2 + rowsFor(s_.instrument(instr)[0]).n; }   // name, type, rows

// ---- input ----------------------------------------------------------------------------------------------------------

bool Lsdj::handDrawn(int syn) const { return (s_.at(syn < 8 ? off::kHandDrawn + 1 : off::kHandDrawn) >> (syn & 7)) & 1; }
void Lsdj::markHandDrawn(int wave) {
    const int syn = (wave >> 4) & 15;
    s_.at(syn < 8 ? off::kHandDrawn + 1 : off::kHandDrawn) |= uint8_t(1 << (syn & 7));
}

void Lsdj::key(Key k, bool down) {
    if (down) msg_ = "";
    if (scr_ == Scr::Instr) fixInstr(instr);
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
            else if (r == 11) kitErase();
            else if (r == 13) kitReplace();
            else if (r == 14) kitDeleteEdited();
            else if (r == 12) {                          // the firmware loader: hands over to the bootloader, which restarts the Tracker
                status_ = "FLASHING...";
                if (tr_ && tr_->flashFirmware(fwSel)) status_ = "FLASHING...";
                else status_ = tr_ && tr_->errorText() ? tr_->errorText() : "CAN'T FLASH THAT";
            }
            else {                                       // 10: erase the song under the cursor from the bank
                status_ = tr_ && fileSel > 0 && tr_->deleteSongFile(fileSel - 1, console_) ? "ERASED" : "CAN'T ERASE";
                fileCount = tr_ ? tr_->listSongs(console_) + 1 : 1;
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
            else if (a_ && sel_ && scr_ == Scr::Kit && !edited_ && !b_) kitRename();
            else if (a_ && sel_ && scr_ == Scr::Loader && !edited_ && !b_) { if (smpCount) sampleLoad(true); }   // Shift + Insert on the loader opens the sample in the editor                    // Shift + Insert on the kit page renames
            else if (a_ && !edited_ && !b_ && !sel_) press(Key::A);
            a_ = false;
        }
        return;
    }
    if (k == Key::B) {
        if (down) {
            bUsed_ = false;
            if (sel_ && scr_ == Scr::File && fileSel > 0) { confirm_ = 10; status_ = "ERASE? INSERT=YES"; bUsed_ = true; }
            else if (sel_ && scr_ == Scr::Song) {                  // Shift + Delete: mute this whole console, or bring it all back
                bool any = false;
                for (int c = 0; c < 4; ++c) any = any || !p_.muted(c);
                for (int c = 0; c < 4; ++c) p_.mute(c, any);
                bUsed_ = true;
            }
        }
        else if (b_ && !bUsed_ && !sel_ && scr_ == Scr::File) scr_ = Scr::Project;
        else if (b_ && !bUsed_ && !sel_ && scr_ == Scr::Library) { libStopPreview(); scr_ = Scr::Instr; }
        else if (b_ && !bUsed_ && !sel_ && scr_ == Scr::Kit && kitRow >= 2) { confirm_ = 11; status_ = "ERASE SAMPLE? INSERT=YES"; }
        else if (b_ && !bUsed_ && !sel_ && scr_ == Scr::Loader) loaderRemove();
        else if (b_ && !bUsed_ && !sel_ && scr_ == Scr::Editor) scr_ = Scr::Instr;
        else if (b_ && !bUsed_ && !sel_ && scr_ == Scr::Firmware) scr_ = Scr::Project;
        else if (b_ && !bUsed_ && !sel_ && scr_ == Scr::Recorder) {                                   // Delete: stops and throws the recording away, or leaves
            if (tr_ && (tr_->recState() == 1 || tr_->recState() == 2)) tr_->recStop(false); else scr_ = Scr::Instr;
        }
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
        if (tableHeld_) { p_.previewOff(tableCh_); tableHeld_ = false; return; }          // Play again ends a table playback
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
        } else if (scr_ == Scr::Recorder) recToggle();                                            // the recorder: Play starts and stops it
        else if (scr_ == Scr::Library) libPreviewKey();                                           // the library: Play previews the sound, nothing is put in the song
        else if (scr_ == Scr::Loader || scr_ == Scr::Editor || scr_ == Scr::Kit) kitAudition();    // the sample pages: Play hears the sample
        else if (!sel_ && scr_ == Scr::Chain) { if (tr_) tr_->startSolo(console_, songCh, 0, chain); else p_.start(0, songCh, 0, false, chain); }   // Play on the Chain page: just this chain, looping
        else if (!sel_ && scr_ == Scr::Phrase) { if (tr_) tr_->startSolo(console_, songCh, phrase, -1); else p_.start(0, songCh, phrase); }          // on the Phrase page: just this phrase
        else if (!sel_ && scr_ == Scr::Table) tableAudition();                                                                                       // on the Table page: the table, on a note
        // Shift + Play on those pages falls through to the whole song
        else if (tr_) tr_->start(console_, startRow());
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

// Play on the Table page: a note on the instrument that uses this table (the current one first), or a plain pulse sound with the table switched on. Play again stops it.
void Lsdj::tableAudition() {
    auto uses = [&](int i) { const uint8_t* in = s_.instrument(i); return (in[6] & 0x20) && (in[6] & 0x1F) == table; };
    int ins = uses(instr) ? instr : -1;
    for (int i = 0; i < kInstruments && ins < 0; ++i) if (uses(i)) ins = i;
    int ch, note = lastNote_;
    if (ins < 0) {
        static const uint8_t def[16] = {0x00, 0xA0, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x03, 0x00, 0x00, 0xD0, 0x00, 0x00, 0x00, 0xF3, 0x00};
        uint8_t* sc = p_.scratchInstr();
        std::memcpy(sc, def, 16);
        sc[6] = uint8_t(0x20 | table);
        ins = Player::kScratch;
        ch = songCh < 2 ? songCh : 0;
    } else {
        ch = previewChannel(ins);
        if ((s_.instrument(ins)[0] & 3) == 2) note = 1;                  // a kit instrument: sample 1
    }
    p_.previewOn(ch, clampi(note, 1, kMaxNote), ins);
    tableHeld_ = true;
    tableCh_ = ch;
}

void Lsdj::encoder(int delta) {
    if (!delta) return;
    if (scr_ == Scr::Instr) fixInstr(instr);
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
    const uint8_t type = ins == Player::kScratch ? p_.scratchInstr()[0] : s_.instrument(ins & 63)[0];
    switch (type & 3) {
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
    if (scr_ == Scr::Library) {                        // the library: the pads play the sound in the preview memory
        if (!libPreview_) return;
        const int pch = previewChannel(Player::kScratch);
        const bool kitSound = (p_.scratchInstr()[0] & 3) == 2;
        const int pnote = kitSound ? 1 + pad % 15 : clampi(13 + pad + 12 * padOct_, 1, kMaxNote);
        padCh_[pad] = int8_t(pch);
        p_.previewOn(pch, pnote, Player::kScratch);
        return;
    }
    if (scr_ == Scr::Kit) {                            // the kit page: pad n hears sample n of the kit on screen
        const uint8_t* kb = kitBuf();
        const int i = pad % 15;
        if (kb && i < KitBank::count(kb)) p_.auditionData(KitBank::data(kb, i), KitBank::length(kb, i));
        return;
    }
    fixInstr(curInstr());
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
    note = kit ? 1 + pad % 15 : clampi(13 + pad + 12 * padOct_, 1, kMaxNote);
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
        if (scr_ == Scr::Instr) fixInstr(instr);
        return;
    }
    scr_ = target;
    if (scr_ == Scr::Instr) fixInstr(instr);
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
    fileCount = tr_->listSongs(console_) + 1;                          // entry 0 is NEW SONG
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
        case Scr::Library: libStopPreview(); scr_ = Scr::Instr; return;
        case Scr::Recorder: if (tr_ && (tr_->recState() == 1 || tr_->recState() == 2)) tr_->recStop(false); scr_ = Scr::Instr; return;
        case Scr::Loader: case Scr::Editor: case Scr::Kit: case Scr::Firmware: scr_ = Scr::Instr; return;
    }
    static const Scr main[5] = {Scr::Song, Scr::Chain, Scr::Phrase, Scr::Instr, Scr::Table};
    if (row == 1) {
        if (k == Key::Right && col < 4) {
            if (scr_ == Scr::Song) { const int c = s_.songCell(songRow, songCh); if (c != 0xFF) { chain = c; } }
            if (scr_ == Scr::Chain) { const int ph = s_.chainPhrase(chain, chainPos); if (ph != 0xFF) phrase = ph; }
            if (scr_ == Scr::Phrase) { const int i = s_.phraseInstr(phrase, step); if (i != 0xFF) instr = i & 63; }
            if (scr_ == Scr::Instr) { const uint8_t t = s_.instrument(instr)[6]; if (t & 0x20) table = t & 0x1F; }
            scr_ = main[col + 1];
            if (scr_ == Scr::Instr) fixInstr(instr);
        } else if (k == Key::Left && col > 0) { scr_ = main[col - 1]; if (scr_ == Scr::Instr) fixInstr(instr); }
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
            instrRow = clampi(instrRow + dy, -1, instrRows() - 1);                    // -1 the number at the top
            if (instrRow == -1 && dx) instr = clampi(instr + dx, 0, kInstruments - 1);   // the number: Left / Right scroll through the instruments
            if (instrRow >= 2) {                                                      // a row with several values: Left / Right pick one
                const RowSet rs = rowsFor(s_.instrument(instr)[0]);
                instrCol = clampi(instrCol + dx, 0, rs.r[instrRow - 2].n - 1);
            }
            break;
        case Scr::Table: tableCol = clampi(tableCol + dx, 0, 5); tableRow = clampi(tableRow + dy, 0, 15); break;
        case Scr::Groove: grooveStep = clampi(grooveStep + dy, 0, 15); break;
        case Scr::Project: projRow = clampi(projRow + dy, 0, 14); break;
        case Scr::File: fileSel = clampi(fileSel + dy, 0, fileCount ? fileCount - 1 : 0); break;
        case Scr::Library: libSel = clampi(libSel + dy + dx * 13, 0, libCount); break;                     // Left / Right turn a page of 13
        case Scr::Loader: case Scr::Editor: case Scr::Kit: kitMove(k); break;
        case Scr::Firmware: fwSel = clampi(fwSel + dy, 0, fwCount ? fwCount - 1 : 0); break;
        case Scr::Recorder: recRow = clampi(recRow + dy, 0, 3); break;
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
            if (phraseCol == 0) { uint8_t& n = s_.note(phrase, step); if (!n) { const uint8_t pi = s_.phraseInstr(phrase, step) == 0xFF ? lastInstr_ : s_.phraseInstr(phrase, step); n = kitInstr(pi) ? 1 : lastNote_; if (s_.phraseInstr(phrase, step) == 0xFF) s_.phraseInstr(phrase, step) = lastInstr_; fixInstr(s_.phraseInstr(phrase, step)); s_.markPhrase(phrase); } }
            else if (phraseCol == 1) { uint8_t& i = s_.phraseInstr(phrase, step); if (i == 0xFF) i = lastInstr_; fixInstr(i); }
            else if (phraseCol == 2 && !s_.phraseCmd(phrase, step)) { s_.phraseCmd(phrase, step) = lastCmd_ ? lastCmd_ : 1; s_.phraseVal(phrase, step) = lastVal_; }
            break;
        case Scr::Project: projectAct(); break;
        case Scr::Instr: if (instrRow == 0) openName(0); break;
        case Scr::Library:
            if (libSel == 0) openName(2);
            else if (tr_) {
                libStopPreview();
                uint8_t blob[128];
                const int n = tr_->loadInstrumentFile(libSel - 1, blob, sizeof blob);
                status_ = n > 0 && unpackInstrument(blob, n) ? "LOADED" : "LOAD FAILED";
            }
            break;
        case Scr::Loader: case Scr::Editor: case Scr::Kit: kitPress(); break;
        case Scr::Recorder: if (recRow == 3) recToggle(); else if (recRow == 2) recMon = !recMon; break;
        case Scr::Firmware:
            if (tr_ && fwCount) { std::snprintf(statusBuf_, sizeof statusBuf_, "FLASH IT? INSERT=YES"); status_ = statusBuf_; confirm_ = 12; }
            break;
        case Scr::File:
            if (tr_ && fileSel == 0) openName(6);
            else if (tr_ && fileCount) { status_ = tr_->loadSongFile(fileSel - 1, console_) ? "LOADED" : "LOAD FAILED"; scr_ = Scr::Project; }
            break;
        default: break;
    }
}

void Lsdj::editKey(Key k) {
    if (scr_ == Scr::Recorder) {                         // the jog wheel: the input or the trigger level
        const int dir = (k == Key::Right || k == Key::Up) ? 1 : -1;
        if (recRow == 0) { recInput = clampi(recInput + dir, 0, 2); if (tr_ && tr_->recAvailable()) tr_->recInput(recInput == 0 ? 0 : recInput == 1 ? 2 : 3); }
        else if (recRow == 1) recTrig = clampi(recTrig + dir, 0, 3);
        else if (recRow == 2) recMon = dir > 0;
        return;
    }
    if (scr_ == Scr::Loader || scr_ == Scr::Editor || scr_ == Scr::Kit) { kitEdit(k); return; }
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
                const uint8_t pi = s_.phraseInstr(phrase, step) == 0xFF ? lastInstr_ : s_.phraseInstr(phrase, step);
                if (kitInstr(pi)) {
                    // a kit note is two sample numbers: the low digit picks a sample of KIT 1, the high digit one of KIT 2, and both play together
                    int a = n & 15, b = n >> 4;
                    if (!n) { if (big) b = 1; else a = 1; }                // an empty step: Up/Down starts with a KIT 2 sample alone, Left/Right with a KIT 1 sample alone
                    else if (big) b = clampi(b + sgn, 0, 15); else a = clampi(a + sgn, 0, 15);
                    n = uint8_t(b << 4 | a);
                } else {
                    if (!n) n = lastNote_;
                    else {                                    // the ROM's notes run 1..108 (C-2 to 0x6C); an octave step that would leave them changes nothing (measured)
                        const int t = n + (big ? sgn * 12 : sgn);
                        if (!big) n = uint8_t(clampi(t, 1, kMaxNote)); else if (t >= 1 && t <= kMaxNote) n = uint8_t(t);
                    }
                    lastNote_ = n;
                }
                if (s_.phraseInstr(phrase, step) == 0xFF) s_.phraseInstr(phrase, step) = lastInstr_;
                s_.markPhrase(phrase);
            } else if (phraseCol == 1) {
                uint8_t& i = s_.phraseInstr(phrase, step);
                if (i == 0xFF) i = lastInstr_; else i = uint8_t(clampi(i + d, 0, kInstruments - 1));
                fixInstr(i);
                lastInstr_ = i; s_.markInstrument(i);
            } else if (phraseCol == 2) {
                uint8_t& c = s_.phraseCmd(phrase, step);
                c = uint8_t(clampi(c + sgn, 0, kCmdCount - 1)); if (c) lastCmd_ = c;
            } else { uint8_t& v = s_.phraseVal(phrase, step); v = uint8_t(v + d); lastVal_ = v; }
            break;
        }
        case Scr::Instr: {
            if (instrRow == -1) { instr = clampi(instr + d, 0, kInstruments - 1); break; }   // the number at the top
            uint8_t* in = s_.instrument(instr);
            if (instrRow == 0) break;
            if (instrRow == 1) { std::memcpy(in, kInstrDefault[(in[0] + sgn) & 3], 16); s_.markInstrument(instr); break; }   // a new type starts from that type's defaults
            const RowSet rs = rowsFor(in[0]);
            if (instrRow - 2 >= rs.n) break;
            const Row& r = rs.r[instrRow - 2];
            subEdit(in, r.s[clampi(instrCol, 0, r.n - 1)], d, sgn, big);
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
            else if (projRow == 14) exportSel = (exportSel + (sgn > 0 ? 1 : 2)) % 3;
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
        case Scr::Groove: return true;
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
                case Scr::Groove: *p = 0; break;                       // 00 ends the groove there
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

// A new song starts at the top of every page.
void Lsdj::newSongView() {
    songRow = songCh = 0;
    chain = chainPos = chainCol = 0;
    phrase = step = phraseCol = 0;
    instr = instrRow = 0;
    table = tableRow = tableCol = 0;
    groove = grooveStep = 0;
    synth = synthRow = 0;
    waveNo = waveCol = 0;
    clearSel();
    status_ = "";
    scr_ = Scr::Song;
}

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
        case 12: openName(6); break;                                    // NEW SONG: ask for a name, then start from an empty song
        case 13: act(Transport::kExportWav, 0, "WAV DONE", "WAV FAILED"); break;
        case 14: act(Transport::kExportFiles, exportSel, "EXPORT DONE", "EXPORT FAILED"); break;
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
    nameMax_ = target == 3 || target == 5 ? 3 : target ? 8 : 5;
    nameCx_ = nameCy_ = 0;
    std::memset(nameBuf_, 0, sizeof nameBuf_);
    if (target == 1) std::snprintf(nameBuf_, sizeof nameBuf_, "%.8s", name_);
    else if (target == 6) {}                                              // a new song starts with no name at all
    else if (target == 3) {}                                             // a sample name starts empty: three letters fill it, so there would be no room to type
    else if (target == 4 || target == 5) {
        const uint8_t* kb = kitBuf();
        if (kb && target == 4 && KitBank::hasSamples(kb)) KitBank::kitName(kb, nameBuf_);
    }
    else { char tmp[6]; instrName(instr, tmp); std::snprintf(nameBuf_, sizeof nameBuf_, "%.5s", tmp); }
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
    else if (nameTarget_ == 6) {                                      // NEW SONG: an empty song under this name
        status_ = tr_ && tr_->newSong(console_, nameBuf_) ? "NEW SONG" : "CAN'T START A NEW SONG";
        nameOpen_ = false;
        return;
    }
    else if (nameTarget_ == 3) { if (n > 0) for (int k = 0; k < 3; ++k) smpName_[k] = k < n ? nameBuf_[k] : '-'; }     // nothing typed keeps the old name
    else if ((nameTarget_ == 4 || nameTarget_ == 5) && tr_) {         // the kit's own name, or one of its samples
        uint8_t* kb = kitBuf();
        if (kb && nameTarget_ == 4) KitBank::setKitName(kb, nameBuf_);
        else if (kb && n > 0) KitBank::setName(kb, kitRow - 2, nameBuf_);
        status_ = kb && tr_->kitSave(console_, kitSlot) ? "SAVED" : "SAVE FAILED";
    }
    else if (nameTarget_ == 2 && n > 0 && tr_) {                      // library: save the current instrument under this name
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

// Play in the library: the selected sound (and its table, in a table slot nobody uses) goes into the preview memory and a note sounds. Nothing in the
// song changes. The pads play the same sound until another is picked or the page is left.
void Lsdj::libPreviewKey() {
    if (!tr_) return;
    if (libSel < 1) { status_ = "PICK A SOUND TO PREVIEW"; return; }
    uint8_t blob[128];
    const int n = tr_->loadInstrumentFile(libSel - 1, blob, sizeof blob);
    if (n < 26 || blob[0] != 'G' || blob[1] != 'B' || blob[2] != 'I' || blob[3] != '1') { status_ = "CAN'T LOAD THAT FILE"; return; }
    libStopPreview();
    uint8_t* sc = p_.scratchInstr();
    std::memcpy(sc, blob + 10, 16);
    if (sc[6] & 0x20) {
        int slot = -1;
        if ((blob[4] & 1) && n >= 26 + 96) {
            static const uint16_t o[6] = {off::kTblEnv, off::kTblTrans, off::kTblCmd1, off::kTblVal1, off::kTblCmd2, off::kTblVal2};
            bool used[kTables] = {};
            for (int i = 0; i < kInstruments; ++i) if (s_.instrument(i)[6] & 0x20) used[s_.instrument(i)[6] & 0x1F] = true;
            for (int t2 = 0; t2 < kTables && slot < 0; ++t2) {
                if (used[t2]) continue;
                bool empty = true;
                for (int c = 0; c < 6 && empty; ++c) for (int r = 0; r < 16; ++r) if (s_.at(uint16_t(o[c] + t2 * 16 + r))) { empty = false; break; }
                if (empty) slot = t2;
            }
            if (slot >= 0) for (int c = 0; c < 6; ++c) std::memcpy(s_.mem() + o[c] + slot * 16, blob + 26 + c * 16, 16);
        }
        libTable_ = slot;
        sc[6] = slot >= 0 ? uint8_t(0x20 | slot) : uint8_t(sc[6] & ~0x3F);
    }
    libPreview_ = true;
    libCh_ = previewChannel(Player::kScratch);
    p_.previewOn(libCh_, (sc[0] & 3) == 2 ? 1 : 25, Player::kScratch);
    status_ = "PREVIEW. PADS PLAY IT";
}

void Lsdj::libStopPreview() {
    if (libPreview_) p_.previewOff(libCh_);
    if (libTable_ >= 0) {                                                  // the borrowed table slot was empty: it is empty again
        static const uint16_t o[6] = {off::kTblEnv, off::kTblTrans, off::kTblCmd1, off::kTblVal1, off::kTblCmd2, off::kTblVal2};
        for (int c = 0; c < 6; ++c) std::memset(s_.mem() + o[c] + libTable_ * 16, 0, 16);
    }
    libTable_ = -1;
    libPreview_ = false;
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
    l.text(0, 17, status_[0] ? status_ : "PLAY=HEAR INSERT=USE");
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
        case Scr::Loader: drawLoader(l); break;
        case Scr::Editor: drawEditor(l); break;
        case Scr::Kit: drawKit(l); break;
        case Scr::Firmware: drawFirmware(l); break;
        case Scr::Recorder: drawRecorder(l); break;
    }
    if (!fullPage()) drawPanel(l, scr_ != Scr::Wave);
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
    if (padOct_ != 0 && !fullPage()) { char o[8]; std::snprintf(o, sizeof o, "O%+d", padOct_); ptext(l, kPc, 9, o); }
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

// What a kit note shows: the name of the KIT 1 sample (or --- for none), then, if a KIT 2 sample plays with it, a plus and its name. out: 3 + 5 characters.
void Lsdj::kitNote(int note, int kit1, int kit2, char out[8]) const {
    auto name = [&](int kit, int sample, char* o) {                          // 3 characters; the sample's own name, or S and its number if the kit is not there
        char nm[4];
        if (tr_ && tr_->kitSampleName(console_, kit, sample - 1, nm)) { std::memcpy(o, nm, 3); return; }
        o[0] = 'S'; o[1] = hx16[sample & 15]; o[2] = ' ';
    };
    std::memset(out, 0, 8);
    const int a = note & 15, b = note >> 4;
    if (a) name(kit1, a, out); else std::memcpy(out, "---", 3);
    if (b) { out[3] = '+'; name(kit2, b, out + 4); }
}

void Lsdj::drawPhrase(Lcd& l) const {
    char t[16]; std::snprintf(t, sizeof t, "PHRASE %02X", phrase); l.text(0, 0, t);
    const char* hd[3] = {"NOTE", "INSTR", "CMD"}; const int hx0[3] = {24, 76, 128};
    for (int h = 0; h < 3; ++h) for (int i = 0; hd[h][i]; ++i) l.glyphAt(hx0[h] + i * 6 - 2, 8, hd[h][i], false, 2, 0);
    for (int r = 0; r < 16; ++r) {
        l.glyph(0, 2 + r, hx16[r]);
        const uint8_t in = s_.phraseInstr(phrase, r);
        char nn[4]; noteName(s_.note(phrase, r), nn);
        char second[8] = "";                                                   // a kit note: the sample names it plays, one after the other
        if (kitInstr(in) && s_.note(phrase, r)) { char kn[8]; kitNote(s_.note(phrase, r), s_.instrument(in & 63)[9] & 0x7F, s_.instrument(in & 63)[2] & 0x7F, kn); std::memcpy(nn, kn, 3); nn[3] = 0; std::memcpy(second, kn + 3, 5); }
        for (int i = 0; i < 3; ++i) l.glyphAt(24 + i * 8, 16 + r * 8, nn[i], r == step && phraseCol == 0, 3, selected(r, 0) ? 1 : 0);
        for (int i = 0; second[i]; ++i) l.glyphAt(48 + i * 8, 16 + r * 8, second[i], false, 3, 0);
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
    l.text(0, 1, status_[0] ? status_ : "SHIFT+BKSP=ERASE");
    // a blank row, then NEW SONG and the songs: a space and a dash before each title (a plus for a loose file from GB/Songs, a star for the built-in demo)
    const int rows = 15, top = std::max(0, std::min(fileSel - rows / 2, fileCount - rows));
    for (int r = 0; r < rows && top + r < fileCount; ++r) {
        const int i = top + r;
        if (i == 0) { if (fileSel == 0) l.text(0, 3 + r, " NEW SONG          ", true); else l.boldText(1, 3 + r, "NEW SONG"); continue; }
        const char* nm = tr_ ? tr_->songName(i - 1) : "";
        char s[32];
        if (nm[0] == '+') std::snprintf(s, sizeof s, " +%-17.17s", nm + 1);
        else if (nm[0] == '*') std::snprintf(s, sizeof s, " *%-17.17s", nm + 1);
        else std::snprintf(s, sizeof s, " -%-17.17s", nm);
        l.text(0, 3 + r, s, i == fileSel);
    }
}

void Lsdj::drawProject(Lcd& l) const {
    char b[24];
    std::snprintf(b, sizeof b, "PROJECT %s", name_[0] ? name_ : "(NEW)");
    l.text(0, 0, b);
    auto row = [&](int y, const char* nm, const char* val, bool cur) { l.text(0, y, nm, cur && !val[0]); if (val[0]) l.text(14, y, val, cur); };   // a row with no value is selected by its name
    const int mode = tr_ ? tr_->syncMode(console_) : (s_.at(off::kSync) ? 1 : 0);
    row(1, "NAME", name_[0] ? name_ : "-", projRow == 0);
    std::snprintf(b, sizeof b, "%02X", s_.at(off::kTempo)); row(2, "TEMPO", b, projRow == 1);
    std::snprintf(b, sizeof b, "%02X", s_.at(off::kTranspose)); row(3, "TRANSPOSE", b, projRow == 2);
    row(4, "SYNC", mode == 1 ? "MASTER" : mode == 2 ? "SLAVE" : "OFF", projRow == 3);
    row(5, "CLONE", cloneDeep_ ? "DEEP" : "SLIM", projRow == 4);
    row(6, "LOOK", Lcd::lookName(tr_ ? tr_->look() : 0), projRow == 5);
    row(7, "PRELISTEN", prelisten_ ? "ON" : "OFF", projRow == 6);
    row(8, "HELP", "", projRow == 7);
    row(9, "CLEAN SONG DATA", "", projRow == 8);
    row(10, "CLEAN INSTR DATA", "", projRow == 9);
    row(11, console_ ? "SAVE SONG B" : "SAVE SONG A", "", projRow == 10);
    row(12, console_ ? "LOAD SONG B" : "LOAD SONG A", "", projRow == 11);
    row(13, console_ ? "NEW SONG B" : "NEW SONG A", "", projRow == 12);
    row(14, "EXPORT WAV", "", projRow == 13);
    row(15, "EXPORT LSDJ", exportSel == 0 ? "A" : exportSel == 1 ? "B" : "A+B", projRow == 14);
    if (sel_) l.text(0, 16, audio_[0] ? audio_ : "WORK TIME: 0:00");      // the technical line only shows while Shift is held
    l.text(0, 17, status_);
}

void Lsdj::drawInstr(Lcd& l) const {
    l.text(0, 0, "INSTRUMENT");
    { char t[4]; std::snprintf(t, sizeof t, "%02X", instr); l.text(11, 0, t, instrRow == -1); }
    const uint8_t* in = s_.instrument(instr);
    char nm[6]; instrName(instr, nm);
    l.text(0, 2, "NAME"); l.rect(72, 16, 40, 8, 1); l.text(9, 2, nm, instrRow == 0);
    l.text(0, 3, "TYPE"); l.text(9, 3, kTypes[in[0] & 3], instrRow == 1);
    const RowSet rs = rowsFor(in[0]);
    for (int i = 0; i < rs.n; ++i) {
        const Row& r = rs.r[i];
        l.text(0, r.row, r.s[0].kind == 6 && (in[9] & 3) ? "SYNTH" : r.label);      // the wave row names the synth once the instrument is not played by hand
        int x = 9;
        for (int k = 0; k < r.n; ++k) {
            char v[12];
            subText(in, r.s[k], v);
            if (k) { char sep[2] = {r.sep, 0}; l.text(x++, r.row, sep); }
            l.text(x, r.row, v, instrRow == 2 + i && clampi(instrCol, 0, r.n - 1) == k);
            x += int(std::strlen(v));
        }
    }
    if ((in[0] & 3) == 2 && tr_) {                                          // the names of the two kits, beside their numbers
        char kn[9];
        if (tr_->kitName(console_, in[9] & 0x7F, kn)) l.text(12, 5, kn);
        if (tr_->kitName(console_, in[2] & 0x7F, kn)) l.text(12, 6, kn);
    }
}

}  // namespace gb
