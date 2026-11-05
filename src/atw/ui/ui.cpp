// Atari tracker -- user interface (ui.h).
#include "atw/ui/ui.h"

#include <cmath>
#include <cstdio>
#include <cstring>


#include "atw/app.h"
#include "ui/buttons.h"

namespace atw {
namespace ui {

namespace b = ot::btn;

void Ui::init(App& app) {
    app_ = &app;
    for (int i = 0; i < int(ScreenId::kCount); ++i) screen(ScreenId(i))->attach(*this);
    current_ = ScreenId::kPattern;
    screen(current_)->enter();
    dirty_ = true;
}

Song& Ui::song() { return app_->song; }

Screen* Ui::screen(ScreenId id) {
    switch (id) {
        case ScreenId::kPattern: return &pattern_;
        case ScreenId::kInstrument: return &instrument_;
        case ScreenId::kSong: return &songScreen_;
        case ScreenId::kSettings: return &settings_;
        case ScreenId::kMaster: return &master_;
        case ScreenId::kPerform: return &perform_;
        case ScreenId::kExport: return &export_;
        default: return &pattern_;
    }
}

void Ui::show(ScreenId id) {
    if (current_ == ScreenId::kInstrument && id != ScreenId::kInstrument) instrument_.leave();
    current_ = id;
    help_ = false;
    screen(id)->enter();
    dirty_ = true;
}

void Ui::message(const char* text, uint32_t ms) {
    std::snprintf(msg_, sizeof msg_, "%s", text);
    msgUntil_ = now_ + ms;
    dirty_ = true;
}

void Ui::status(const char* text, uint32_t ms) {
    std::snprintf(status_, sizeof status_, "%s", text);
    statusUntil_ = now_ + ms;
    dirty_ = true;
}

bool Ui::confirm(Screen* s, uint8_t action, const char* question, const char* ok) {
    if (confirmed_ == action) return true;
    ask_.open = true;
    std::snprintf(ask_.text, sizeof ask_.text, "%s", question);
    ask_.ok = ok;
    ask_.target = s;
    ask_.action = action;
    dirty_ = true;
    return false;
}

void Ui::songChanged() {
    pattern = 0;
    orderPos = 0;
    inst = 1;
    channel = 0;
    edit = false;
    pattern_.reset();
    dirty_ = true;
}

const char* Ui::instrumentName(int slot) {
    if (slot < 1 || slot > kInstrumentCount) return "";
    const Song& s = song();
    return (s.chip == atari::Chip::Pokey ? s.pokey[slot - 1].name : s.tia[slot - 1].name).c_str();
}

bool Ui::noteInTune(uint8_t instrument, uint8_t cellNote) const {
    const Song& s = app_->song;
    if (s.chip != atari::Chip::Tia || instrument < 1 || instrument > kInstrumentCount || !note::isNote(cellNote)) return true;
    for (const atari::TiaStep& st : s.tia[instrument - 1].steps) {
        if (st.fixedDivider >= 0) continue;          // a fixed divider does not follow the note
        int n = cellNote - 1 + st.pitchOffset;
        n = n < 0 ? 0 : (n > 61 ? 61 : n);
        if (s.tia[instrument - 1].combined) return atari::tiaCombinedInTune(n, s.region, s.tiaTune);
        return atari::tiaNoteInTune(st.waveform, n, s.region, s.tiaTune);
    }
    return true;
}

bool Ui::instrumentFits(uint8_t instrument, int track, char* why) const {
    const Song& s = app_->song;
    if (s.chip != atari::Chip::Pokey || instrument < 1 || instrument > kInstrumentCount || track < 0 || track > 3) return true;
    const uint8_t mask = instrumentTrackMask(s.pokey[instrument - 1]);
    if ((mask >> track) & 1) return true;
    if (why) {
        char tr[24];
        trackMaskText(mask, tr);
        std::snprintf(why, 48, "Instrument %02X: %s", unsigned(instrument), tr);
    }
    return false;
}

// --- pads ---------------------------------------------------------------------------

uint8_t Ui::padNote(int pad) const {
    if (pad < 0 || pad >= ot::kPadCount) return 0;
    const int r = pad / ot::kPadCols, c = pad % ot::kPadCols;
    const int semi = (octave + (ot::kPadRows - 1 - r)) * 12 + c;   // the bottom row is `octave`
    return semi + 1 <= note::kLast ? uint8_t(semi + 1) : 0;
}

void Ui::auditionNote(int pad, uint8_t instrument, uint8_t n) {
    if (pad < 0 || pad >= ot::kPadCount) return;
    const int ch = channel < kTracks ? channel : 0;
    char why[48];
    if (!instrumentFits(instrument, ch, why)) {      // e.g. a 16-bit bass on track 1: it would only sound very high
        status(why, 2500);
        return;
    }
    app_->engine.noteOn(ch, n, instrument);
    lastNote = n;
    padHeld_[pad] = n;
    padChannel_[pad] = ch;
}

void Ui::auditionOn(int pad, uint8_t instrument) {
    const uint8_t n = padNote(pad);
    if (n) auditionNote(pad, instrument, n);
}

void Ui::retriggerHeld(uint8_t instrument) {
    for (int p = 0; p < ot::kPadCount; ++p)
        if (padHeld_[p] && instrumentFits(instrument, padChannel_[p])) app_->engine.noteOn(padChannel_[p], padHeld_[p], instrument);
}

void Ui::preview(uint8_t instrument) {
    for (int p = 0; p < ot::kPadCount; ++p)
        if (padHeld_[p]) {
            retriggerHeld(instrument);
            return;
        }
    const int ch = channel < kTracks ? channel : 0;
    if (!instrumentFits(instrument, ch)) return;
    if (previewChannel_ >= 0 && previewChannel_ != ch) app_->engine.noteOff(previewChannel_);
    previewChannel_ = ch;
    app_->engine.noteOn(ch, uint8_t(1 + (octave + 1) * 12), instrument);
    previewUntil_ = now_ + 300;
}

void Ui::auditionOff(int pad) {
    if (pad < 0 || pad >= ot::kPadCount || !padHeld_[pad]) return;
    padHeld_[pad] = 0;
    for (int p = 0; p < ot::kPadCount; ++p)
        if (padHeld_[p] && padChannel_[p] == padChannel_[pad]) return;   // another pad still holds the track
    app_->engine.noteOff(padChannel_[pad]);
    if (current_ == ScreenId::kPattern) pattern_.recordOff(padChannel_[pad]);
}

// --- pad keyboard -------------------------------------------------------------------

namespace {
// Pad keyboard rows, top row of pads first (pad = row * 12 + column).
const char kKeys[4][13] = {"1234567890-_", "QWERTYUIOP()", "ASDFGHJKL!?.", "ZXCVBNM,'/+ "};
}  // namespace

void Ui::keyboard(Screen* s, uint8_t action, const char* title, const char* initial, bool fileName) {
    kb_.open = true;
    kb_.done = false;
    kb_.fileName = fileName;
    kb_.target = s;
    kb_.action = action;
    std::snprintf(kb_.title, sizeof kb_.title, "%s", title);
    std::snprintf(kb_.text, sizeof kb_.text, "%s", initial);
    dirty_ = true;
}

void Ui::keyboardInput(const ot::InputEvent& ev) {
    const size_t len = std::strlen(kb_.text);
    if (ev.type == ot::InputEvent::kButtonUp && ev.id == b::Shift) shift = false;
    if (ev.type == ot::InputEvent::kPadDown && ev.id < 48) {
        const char c = kKeys[ev.id / 12][ev.id % 12];
        const bool ok = !kb_.fileName || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (ok && len < sizeof kb_.text - 1) {
            kb_.text[len] = c;
            kb_.text[len + 1] = 0;
        }
        return;
    }
    if (ev.type != ot::InputEvent::kButtonDown) return;
    switch (ev.id) {
        case b::Shift: shift = true; return;
        case b::Delete:
            if (len) kb_.text[len - 1] = 0;
            return;
        case b::F8:
        case b::EncPush:
            if (!len) return;
            kb_.open = false;
            kb_.done = true;
            if (kb_.target) kb_.target->button(kb_.action, true);
            kb_.done = false;
            return;
        default:
            if (ev.id == b::F7 || b::isModuleKey(ev.id)) {
                kb_.open = false;
                status("Cancelled");
            }
            return;
    }
}

void Ui::drawKeyboard(ot::Gfx& g) {
    g.text(16, kContentY + 6, kMono, col::kDim, kb_.title);
    char line[kNameLen + 4];
    std::snprintf(line, sizeof line, "%s_", kb_.text);
    g.fill(16, kContentY + 26, 768, 34, col::kPanel);
    g.text(28, kContentY + 34, kLarge, col::kText, line);
    const int cw = 66, chh = 70, x0 = 4, y0 = kContentY + 70;
    char k[2] = {};
    for (int p = 0; p < 48; ++p) {
        const char c = kKeys[p / 12][p % 12];
        const bool ok = !kb_.fileName || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        const int x = x0 + (p % 12) * cw, y = y0 + (p / 12) * chh;
        g.fill(x, y, cw - 4, chh - 4, ok ? col::kPanelHi : col::kPanel);
        k[0] = c;
        g.text(x + cw / 2 - 2, y + chh / 2 - 2, kLarge, ok ? col::kText : col::kFaint, c == ' ' ? "SP" : k, ot::IDisplay::kOptCenter);
    }
    g.text(16, kBarY - 22, kMono, col::kDim, "pads: type   Delete: back   then OK or CANCEL below");
}

// --- instrument picker (hold Instr) -------------------------------------------------

void Ui::openPicker(bool sticky) {
    pick_.open = true;
    pick_.sticky = sticky;
    pick_.slot = inst;
    dirty_ = true;
}

void Ui::drawPicker(ot::Gfx& g) {
    const Song& sg = song();
    bool used[kInstrumentCount + 1] = {};
    for (int i = 0; i < sg.orderLen; ++i) {
        for (int t = 0; t < kTracks; ++t) {
            const int pi = sg.order[i].trackPattern(t);
            if (pi < 0) continue;
            const Pattern& p = sg.pattern(pi);
            for (int r = 0; r < p.rows; ++r)
                if (p.cells[r][t].inst >= 1 && p.cells[r][t].inst <= kInstrumentCount) used[p.cells[r][t].inst] = true;
        }
    }
    g.text(16, kContentY + 6, kMono, col::kDim,
           pick_.sticky ? "PICK INSTRUMENT  pads / jog: select and play   Instr: choose   CLOSE: keep"
                        : "PICK INSTRUMENT  pads / jog: select and play   release Instr: choose   Insert: write");
    const int cw = 66, chh = 90, x0 = 4, y0 = kContentY + 30;
    char buf[12];
    for (int p = 0; p < kInstrumentCount; ++p) {
        const int slot = p + 1;
        const int x = x0 + (p % ot::kPadCols) * cw, y = y0 + (p / ot::kPadCols) * chh;
        const bool sel = slot == pick_.slot, cur = slot == inst;
        const char* name = instrumentName(slot);
        const bool fits = instrumentFits(uint8_t(slot), channel);
        const bool live = used[slot] || std::strcmp(name, "Empty") != 0;
        g.fill(x, y, cw - 4, chh - 4, sel ? col::kCursor : (live && fits ? col::kPanelHi : col::kPanel));
        if (cur && !sel) g.frame(x, y, cw - 4, chh - 4, col::kInst, 1);
        std::snprintf(buf, sizeof buf, "%02X", unsigned(slot));
        const uint32_t tc = sel ? col::kOnCursor : (!fits ? col::kBad : (live ? col::kText : col::kFaint));
        g.text(x + 4, y + 4, kMono, tc, buf);
        if (!live) continue;
        // The name in two lines of 7, broken at a space where there is one.
        char a[8] = {}, rest7[8] = {};
        const char* sp = std::strchr(name, ' ');
        const int len = int(std::strlen(name));
        int k = sp && sp - name <= 7 ? int(sp - name) : 7;
        if (k > len) k = len;
        std::snprintf(a, sizeof a, "%.*s", k, name);
        const char* tail = name + k;
        while (*tail == ' ') ++tail;
        std::snprintf(rest7, sizeof rest7, "%.7s", tail);
        g.text(x + 4, y + 30, kSmall, tc, a);
        if (rest7[0]) g.text(x + 4, y + 48, kSmall, tc, rest7);
    }
}

bool Ui::pickerInput(const ot::InputEvent& ev) {
    auto move = [&](int d) { pick_.slot = uint8_t(((pick_.slot - 1 + d) % kInstrumentCount + kInstrumentCount) % kInstrumentCount + 1); };
    auto choose = [&]() {
        pick_.open = false;
        hold_.down = false;
        char why[48];
        if (!instrumentFits(pick_.slot, channel, why)) {   // not for this track: the choice stays as it was
            status(why, 2500);
            return;
        }
        inst = pick_.slot;
        // As on the stock Tracker: in edit mode the chosen instrument is written into the step too.
        if (current_ == ScreenId::kPattern && edit) pattern_.writeInstrument(inst);
        char m[48];
        std::snprintf(m, sizeof m, "Instrument %02X %.20s", unsigned(inst), instrumentName(inst));
        status(m);
    };
    switch (ev.type) {
        case ot::InputEvent::kButtonDown:
            if (ev.id == b::Shift) {
                shift = true;
                return true;
            }
            switch (ev.id) {
                case b::Left: move(-1); return true;
                case b::Right: move(1); return true;
                case b::Up: move(-ot::kPadCols); return true;
                case b::Down: move(ot::kPadCols); return true;
                case b::F7: pick_.open = false; return true;   // CLOSE: keep the instrument
                case b::Instr:
                case b::EncPush:
                    if (pick_.sticky) choose();
                    return true;
                case b::Insert:
                    if (current_ == ScreenId::kPattern && edit && instrumentFits(pick_.slot, channel)) {
                        pattern_.writeInstrument(pick_.slot);
                        inst = pick_.slot;
                    }
                    return true;
                default: return true;
            }
        case ot::InputEvent::kButtonUp:
            if (ev.id == b::Shift) shift = false;
            if (ev.id == b::Instr) {
                pick_.instrHeld = false;
                if (!pick_.sticky) choose();
            }
            return true;
        case ot::InputEvent::kPadDown: {
            const int slot = ev.id + 1;
            if (slot >= 1 && slot <= kInstrumentCount) {
                pick_.slot = uint8_t(slot);
                // Every pad plays the same note: compare sounds, not pitches.
                const uint8_t n = uint8_t(1 + (octave + 1) * 12);
                const int ch = channel < kTracks ? channel : 0;
                if (instrumentFits(pick_.slot, ch)) app_->engine.noteOn(ch, n, pick_.slot);
                padHeld_[ev.id] = n;
                padChannel_[ev.id] = ch;
                if (ev.id < 64) {
                    padsDown_ |= 1ull << ev.id;
                    pickerPads_ |= 1ull << ev.id;   // released after the picker closes: still ours
                }
            }
            return true;
        }
        case ot::InputEvent::kPadUp:
            if (ev.id < 64) {
                padsDown_ &= ~(1ull << ev.id);
                pickerPads_ &= ~(1ull << ev.id);
            }
            auditionOff(ev.id);
            return true;
        case ot::InputEvent::kEncoder: move(ev.delta); return true;
        default: return true;
    }
}

// --- Note / Fx pad maps (hold Note, Fx1, Fx2 on the pattern) -----------------------------------------------------------

void Ui::closePopup(bool choose) {
    const Pop was = pop_;
    pop_ = Pop::kNone;
    dirty_ = true;
    if (!choose || was == Pop::kNote) return;
    uint8_t list[16];
    const int n = fxList(song().chip, list);
    if (popSel_ < 0 || popSel_ >= n) return;
    pattern_.chooseEffect(was == Pop::kFx1 ? 0 : 1, list[popSel_]);
    char m[48];
    std::snprintf(m, sizeof m, "Effect %c %s", fxLetter(list[popSel_]), fxName(list[popSel_]));
    status(m);
}

bool Ui::popupInput(const ot::InputEvent& ev) {
    uint8_t list[16];
    const int n = fxList(song().chip, list);
    auto move = [&](int d) { if (n) popSel_ = ((popSel_ + d) % n + n) % n; };
    switch (ev.type) {
        case ot::InputEvent::kButtonDown:
            if (ev.id == b::Shift) shift = true;
            else if (pop_ != Pop::kNote) {
                if (ev.id == b::Left || ev.id == b::Up) move(-1);
                else if (ev.id == b::Right || ev.id == b::Down) move(1);
            }
            return true;
        case ot::InputEvent::kButtonUp:
            if (ev.id == b::Shift) shift = false;
            if (hold_.down && ev.id == hold_.id) {
                hold_.down = false;
                closePopup(true);
            }
            return true;
        case ot::InputEvent::kPadDown:
            if (ev.id < 64) padsDown_ |= 1ull << ev.id;
            if (pop_ == Pop::kNote) pattern_.notePad(ev.id, ev.velocity);
            else if (ev.id < n) popSel_ = ev.id;
            return true;
        case ot::InputEvent::kPadUp:
            if (ev.id < 64) padsDown_ &= ~(1ull << ev.id);
            auditionOff(ev.id);
            return true;
        case ot::InputEvent::kEncoder:
            if (pop_ != Pop::kNote) move(ev.delta);
            return true;
        default: return true;
    }
}

namespace {
// What an effect does and how to write it: shown under the pad map while Fx1 / Fx2 is held. Lines are separated by '|'.
const char* fxHelpText(uint8_t type) {
    switch (type) {
        case atari::FX_VOLUME: return "VOLUME   V0x   x = 0-F: volume of this track from here on (F = full).|Example: V08 is half volume. On a step with a note it sets that note's volume.";
        case atari::FX_SLIDE_UP: return "SLIDE UP   Sxx   the pitch rises smoothly until the next note.|POKEY: xx = 01-3F, how fast (a fraction of the pitch every frame). TIA: xx/16 steps a frame.|S00 stops the slide. On an 8-bit voice it moves in whole divider steps; 16-bit voices are smoothest.";
        case atari::FX_SLIDE_DOWN: return "SLIDE DOWN   sxx   the pitch falls smoothly until the next note.|POKEY: xx = 01-3F, how fast (a fraction of the pitch every frame). TIA: xx/16 steps a frame.|s00 stops the slide.";
        case atari::FX_ARP: return "ARPEGGIO   Axy   plays the note, then +x and +y semitones, one every frame.|A47 = major chord, A37 = minor chord, A0C = octave jumps. A00 = off.|It lasts until the next note.";
        case atari::FX_VIBRATO: return "VIBRATO (POKEY)   vxy   x = depth 1-3 (about 10, 26, 46 cents), y = frames before it starts.|A slow wave, about 5 Hz. On an 8-bit voice it moves in whole divider steps: clearest on low notes|and 16-bit voices. v00 = off.";
        case atari::FX_SPEED: return "TEMPO   Txx   frames per row, 01-FF: lower is faster. 06 = 150 BPM (NTSC).|Takes effect from this row on, for the whole song until another T.";
        case atari::FX_BREAK: return "BREAK   B00   ends this pattern after this row; the song goes on to the next entry.|Use it for patterns shorter than their length.";
        case atari::FX_AUDCTL: return "AUDCTL (POKEY)   axx   sets the POKEY clock / filter / 16-bit bits for this voice.|01 15 kHz, 04 / 02 high-pass, 10 / 08 16-bit join, 40 / 20 1.79 MHz, 80 poly9.|Advanced: other voices can change pitch with it.";
        case atari::FX_DELAY: return "DELAY   Dxx   the note on this row starts xx video frames late (less than the row length keeps it in the row).|D02 = 2 frames late. Use it for swing, flams and ghost-note feel. A new note or note off cancels a waiting one.|Its other effects (arpeggio, slide, vibrato ...) start with the note.";
        case atari::FX_RETRIG: return "RETRIGGER   Rxx   plays the note again every xx frames until the next note or note off.|R03 = a fast stutter, R06 = rolls and drum flams. R00 stops it.";
        case atari::FX_VOLSLIDE: return "LEVEL SLIDE   Lxy   the track volume rises by x and falls by y sixteenths of a step every frame.|L02 = a slow fade out, L40 = a quick swell. L00 stops (keeps the volume). Lasts until the next note.";
        case atari::FX_PORTA: return "PORTAMENTO   P0x   the note on this row glides from the pitch of the one before it. x = speed 1 (slow) to 8 (instant).|Needs a note still sounding on the track. P00 = off. On an 8-bit voice it moves in divider steps;|16-bit voices glide smoothest.";
        case atari::FX_CUT: return "CUT   Cxx   note off after xx frames: C03 = a 3-frame stab. Makes staccato notes|without an extra note-off row.";
        default: return "";
    }
}
}  // namespace

void Ui::drawPopup(ot::Gfx& g) {
    const int cw = 66, chh = 90, x0 = 4, y0 = kContentY + 30;
    char buf[16];
    if (pop_ == Pop::kNote) {
        std::snprintf(buf, sizeof buf, "%u", unsigned(octave + 1));
        g.text(16, kContentY + 6, kMono, col::kDim, song().chip == atari::Chip::Tia ? "THE NOTES ON THE PADS   red: this TIA sound cannot play it in tune   release Note: close" : "THE NOTES ON THE PADS   pads play (and write with Rec)   release Note: close   bottom row = octave");
        for (int p = 0; p < ot::kPadCount; ++p) {
            const uint8_t n = padNote(p);
            const int x = x0 + (p % ot::kPadCols) * cw, y = y0 + (p / ot::kPadCols) * chh;
            const int semi = p % ot::kPadCols;
            const bool black = semi == 1 || semi == 3 || semi == 6 || semi == 8 || semi == 10;
            const bool tune = noteInTune(inst, n);
            g.fill(x, y, cw - 4, chh - 4, (padsDown_ >> p) & 1 ? col::kCursor : (black ? col::kPanel : col::kPanelHi));
            if (!n) continue;
            char nm[4];
            noteName(n, nm);
            g.text(x + cw / 2 - 2, y + chh / 2 - 4, kLarge, !tune ? col::kBad : (black ? col::kDim : col::kText), nm, ot::IDisplay::kOptCenter);
        }
        return;
    }
    uint8_t list[16];
    const int n = fxList(song().chip, list);
    g.text(16, kContentY + 6, kMono, col::kDim,
           pop_ == Pop::kFx1 ? "PICK EFFECT 1   pads / jog: select   release Fx1: choose (written with Rec)"
                             : "PICK EFFECT 2   pads / jog: select   release Fx2: choose (written with Rec)");
    for (int p = 0; p < ot::kPadCount; ++p) {
        const int x = x0 + (p % ot::kPadCols) * cw, y = y0 + (p / ot::kPadCols) * chh;
        const bool live = p < n, sel = p == popSel_;
        g.fill(x, y, cw - 4, chh - 4, sel && live ? col::kCursor : (live ? col::kPanelHi : col::kPanel));
        if (!live) continue;
        char l[2] = {fxLetter(list[p]), 0};
        g.text(x + 6, y + 6, kLarge, sel ? col::kOnCursor : col::kCmd, l);
        char nm[9];
        std::snprintf(nm, sizeof nm, "%.7s", fxName(list[p]));
        g.text(x + 4, y + 44, kSmall, sel ? col::kOnCursor : col::kText, nm);
    }
    if (popSel_ >= 0 && popSel_ < n) {          // what the highlighted effect does, in the free space below
        const char* t = fxHelpText(list[popSel_]);
        const int rowsUsed = (n + ot::kPadCols - 1) / ot::kPadCols;
        const int yFirst = y0 + rowsUsed * chh + 8;
        int y = yFirst;
        char line[128];
        while (*t) {
            int len = 0;
            while (t[len] && t[len] != '|' && len < int(sizeof line) - 1) { line[len] = t[len]; ++len; }
            line[len] = 0;
            g.text(16, y, kMono, y == yFirst ? col::kText : col::kDim, line);
            y += 24;
            t += len;
            if (*t == '|') ++t;
        }
    }
}

// --- transport ----------------------------------------------------------------------

void Ui::applyTransport() { app_->engine.setMetronome(metronome, metroVolume); }

void Ui::play() {
    AudioEngine& e = app_->engine;
    const bool count = edit && preroll;   // record + play with a pre-roll: one bar of clicks first
    // Pattern mode (the Pattern screen and the editors) plays only the pattern being edited, looping; the Song, Master and Perform screens
    // play the song. Shift + Play does the other one.
    const bool songScreen = current_ == ScreenId::kSong || current_ == ScreenId::kMaster || current_ == ScreenId::kPerform;
    const bool wantSong = songScreen != shift;
    if (e.playing() || e.countingIn()) {
        e.stop();
    } else if (!wantSong) {
        if (count) e.countInPattern(pattern);
        else e.playPattern(pattern);
    } else {
        // The song from the position that holds the edited pattern (the selected entry on the Song screen).
        int pos = -1;
        const Song& sg = song();
        if (songScreen && orderPos < sg.orderLen) pos = orderPos;
        if (pos < 0 && orderPos < sg.orderLen && sg.order[orderPos].uses(pattern)) pos = orderPos;
        for (int i = 0; pos < 0 && i < sg.orderLen; ++i)
            if (sg.order[i].uses(pattern)) pos = i;
        if (pos < 0) pos = 0;
        if (count) e.countInSong(uint8_t(pos));
        else e.playSong(uint8_t(pos));
    }
}

// --- input --------------------------------------------------------------------------

Undo::Target Ui::undoTarget() {       // what the screen shown can edit
    switch (current_) {
        case ScreenId::kPattern: return {Undo::Kind::kPattern, pattern, atari::Chip::Pokey};
        case ScreenId::kInstrument: return {Undo::Kind::kInstrument, uint8_t(inst - 1), song().chip};
        default: return {Undo::Kind::kHead, 0, atari::Chip::Pokey};
    }
}

void Ui::handle(const ot::InputEvent& ev) {
    app_->undoBegin(undoTarget());
    handleEvent(ev);
    app_->undoEnd();
}

void Ui::midiNoteOn(uint8_t n) {
    if (app_->exporter.busy() || kb_.open || ask_.open || n < note::kFirst || n > note::kLast) return;
    app_->undoBegin(undoTarget());
    dirty_ = true;
    screen(current_)->midiNote(n);
    const int ch = channel < kTracks ? channel : 0;
    if (midiChannel_[n] && midiChannel_[n] - 1 != ch) app_->engine.noteOff(midiChannel_[n] - 1);
    if (!instrumentFits(inst, ch)) {
        app_->undoEnd();
        return;
    }
    app_->engine.noteOn(ch, n, inst);
    lastNote = n;
    midiChannel_[n] = int8_t(ch + 1);
    app_->undoEnd();
}

void Ui::midiNoteOff(uint8_t n) {
    if (n < note::kFirst || n > note::kLast || !midiChannel_[n]) return;
    const int ch = midiChannel_[n] - 1;
    midiChannel_[n] = 0;
    for (int k = 1; k < 64; ++k)
        if (midiChannel_[k] == ch + 1) return;               // another key still holds the track
    for (int p = 0; p < ot::kPadCount; ++p)
        if (padHeld_[p] && padChannel_[p] == ch) return;
    app_->engine.noteOff(ch);
    if (current_ == ScreenId::kPattern) {
        app_->undoBegin(undoTarget());
        pattern_.recordOff(ch);
        app_->undoEnd();
    }
}

void Ui::midiAllOff() {
    for (int k = 1; k < 64; ++k)
        if (midiChannel_[k]) {
            app_->engine.noteOff(midiChannel_[k] - 1);
            midiChannel_[k] = 0;
        }
}

void Ui::changePattern(int d, bool live) {
    AudioEngine& e = app_->engine;
    char m[64];
    if (live && e.playing()) {
        const int base = e.queuedPattern() >= 0 ? e.queuedPattern() : e.pattern();
        const int n = (base + d + kPatternCount) % kPatternCount;
        e.queuePattern(n == e.pattern() ? -1 : n);
        if (n == e.pattern()) std::snprintf(m, sizeof m, "Staying on pattern %02X", unsigned(n));
        else std::snprintf(m, sizeof m, "Pattern %02X starts when %02X has ended", unsigned(n), unsigned(e.pattern()));
        status(m, 1800);
        return;
    }
    pattern = uint8_t((pattern + d + kPatternCount) % kPatternCount);
    std::snprintf(m, sizeof m, "Pattern %02X", unsigned(pattern));
    status(m, 800);
}

void Ui::undoRedo(bool redo) {
    char m[40];
    if (redo ? app_->redo() : app_->undo()) std::snprintf(m, sizeof m, "%s (%d left)", redo ? "Redo" : "Undo", redo ? app_->redoSteps() : app_->undoSteps());
    else std::snprintf(m, sizeof m, "Nothing to %s", redo ? "redo" : "undo");
    status(m);
}

void Ui::handleEvent(const ot::InputEvent& ev) {
    dirty_ = true;
    if (app_->exporter.busy()) {          // a WAV is rendering from the song: nothing may change it, only CANCEL works
        if (ev.type == ot::InputEvent::kButtonDown && ev.id == b::F7) app_->exporter.cancel();
        return;
    }
    Screen& s = *screen(current_);
    if (kb_.open) {
        keyboardInput(ev);
        return;
    }
    if (pick_.open && !ask_.open) {
        pickerInput(ev);
        return;
    }
    if (pop_ != Pop::kNone && !ask_.open) {
        popupInput(ev);
        return;
    }
    if (ev.id == b::Instr && (ev.type == ot::InputEvent::kButtonDown || ev.type == ot::InputEvent::kButtonUp)) {
        const bool down = ev.type == ot::InputEvent::kButtonDown;
        pick_.instrHeld = down;
        pick_.instrDownAt = down ? now_ : 0;
        if (down && current_ == ScreenId::kInstrument && !help_ && !ask_.open) {   // a tap opens it here
            openPicker(true);
            return;
        }
    }
    if (ask_.open) {   // the dialog takes every input
        if (ev.type == ot::InputEvent::kButtonDown && ev.id == b::Shift) shift = true;
        if (ev.type == ot::InputEvent::kButtonUp && ev.id == b::Shift) shift = false;
        if (ev.type == ot::InputEvent::kPadUp) {
            if (ev.id < 64) padsDown_ &= ~(1ull << ev.id);
            auditionOff(ev.id);
            return;
        }
        if (ev.type != ot::InputEvent::kButtonDown || ev.id == b::Shift) return;
        if (ev.id == ask_.action && b::autoRepeats(ev.id)) return;   // the key that opened it auto-repeats: not an answer
        ask_.open = false;
        if ((ev.id == b::F8 || ev.id == b::EncPush) && ask_.target) {
            const bool sh = shift;
            shift = false;   // the screen sees the plain key it asked about
            confirmed_ = ask_.action;
            ask_.target->button(ask_.action, true);
            confirmed_ = -1;
            shift = sh;
        } else {
            status("Cancelled");
        }
        return;
    }
    switch (ev.type) {
        case ot::InputEvent::kButtonDown:
        case ot::InputEvent::kButtonUp: {
            const bool down = ev.type == ot::InputEvent::kButtonDown;
            if (ev.id <= b::F8) {
                if (down) { fHeld_ = uint8_t(fHeld_ | (1u << ev.id)); fFlashUntil_[ev.id] = now_ + 140; }
                else fHeld_ = uint8_t(fHeld_ & ~(1u << ev.id));
                dirty_ = true;
            }
            if (ev.id == b::Shift) {
                shift = down;
                return;
            }
            if (ev.id == b::Help) {
                if (down) help_ = !help_;
                return;
            }
            if (help_ && down) {
                help_ = false;
                return;
            }
            if (ev.id == b::Power) {
                if (!down) {
                    if (powerHeld_) {
                        powerHeld_ = false;
                        if (hwMuted_ && app_->platform().muteOutput) app_->platform().muteOutput(false);
                        hwMuted_ = false;
                        app_->engine.setMasterVolume(masterVolume);
                        dirty_ = true;
                    }
                } else if (!shift) {
                    powerHeld_ = true;
                    powerAt_ = now_;
                    dirty_ = true;
                }
                if (down && shift) {
                    char file[40];
                    message(app_->screenshot(file, sizeof file) ? file : "Screenshot failed", 2000);
                }
                return;
            }
            // Note / Instr / Fx1 / Fx2 on the pattern: a tap jumps to the column, holding pops up the pad map instead (update()).
            if (current_ == ScreenId::kPattern && !ask_.open &&
                (ev.id == b::Note || ev.id == b::Instr || ev.id == b::Fx1 || ev.id == b::Fx2)) {
                if (down) {
                    hold_ = Hold{ev.id, true, now_};
                } else if (hold_.down && hold_.id == ev.id) {
                    hold_.down = false;
                    s.button(ev.id, true);
                }
                return;
            }
            if (s.button(ev.id, down) || !down) return;
            switch (ev.id) {
                case b::Pattern: show(ScreenId::kPattern); break;
                case b::InstrParams: show(ScreenId::kInstrument); break;
                case b::Song: show(ScreenId::kSong); break;
                case b::Config: show(ScreenId::kSettings); break;
                case b::Master: show(ScreenId::kMaster); break;
                case b::Performance: show(ScreenId::kPerform); break;
                case b::File: show(ScreenId::kExport); break;
                case b::Rec:
                    if (shift) undoRedo(false);
                    else edit = !edit;
                    break;
                case b::SampleRecorder:
                    if (shift) undoRedo(true);
                    else { show(ScreenId::kInstrument); instrument_.gotoGrid(); }
                    break;
                case b::SampleLoader: show(ScreenId::kInstrument); instrument_.gotoSounds(); break;
                case b::SampleEditor: show(ScreenId::kInstrument); instrument_.gotoTable(); break;
                case b::SamplePlayback: settings_.askChip(); break;
                case b::Play: play(); break;
                default: break;
            }
            return;
        }
        case ot::InputEvent::kPadDown:
        case ot::InputEvent::kPadUp: {
            const bool down = ev.type == ot::InputEvent::kPadDown;
            if (ev.id < 64) padsDown_ = down ? (padsDown_ | (1ull << ev.id)) : (padsDown_ & ~(1ull << ev.id));
            if (!down && ev.id < 64 && ((pickerPads_ >> ev.id) & 1)) {   // pressed inside the picker
                pickerPads_ &= ~(1ull << ev.id);
                auditionOff(ev.id);
                return;
            }
            if (s.pad(ev.id, down, ev.velocity)) return;
            if (down) auditionOn(ev.id, inst);
            else auditionOff(ev.id);
            return;
        }
        case ot::InputEvent::kEncoder:
            if (help_) return;
            s.encoder(ev.delta);
            return;
        default: return;
    }
}

// --- drawing ------------------------------------------------------------------------

void Ui::drawDialog(ot::Gfx& g) {
    const int w = 560, h = 110, x = 400 - w / 2, y = kBarY - h - 16;
    g.fill(x, y, w, h, col::kPanel);
    g.frame(x, y, w, h, col::kCursor, 2);
    g.text(400, y + 40, kMono, col::kText, ask_.text, ot::IDisplay::kOptCenter);
    char hint[48];
    std::snprintf(hint, sizeof hint, "%s or CANCEL (buttons below)", ask_.ok);
    g.text(400, y + 82, kMono, col::kDim, hint, ot::IDisplay::kOptCenter);
}

void Ui::drawChrome(ot::Gfx& g, Screen& s) {
    const Song& sg = song();
    const AudioEngine& e = app_->engine;
    g.fill(0, 0, 800, kTitleH, col::kPanel);
    g.text(8, 4, kLarge, col::kAccent1, "ATARI");
    g.text(76, 4, kLarge, col::kAccent2, "TRACKER");
    // Header zones, laid out from the right with measured widths so nothing can overlap:
    // [logo][title *][status/position ...][chip region BPM][INS Tn][edit]. Whatever does not
    // fit is shortened or dropped (title first), never drawn over a neighbour.
    char ins[24], pos[96], bpm[48];
    std::snprintf(ins, sizeof ins, "INS %02X T%d", unsigned(inst), channel < kTracks ? channel + 1 : 1);
    const int insW = g.textWidth(kText, ins);
    const int insX = 780 - insW;
    g.text(insX, 6, kText, col::kInst, ins);
    int right = insX - 12;
    if (now_ >= statusUntil_) {
        std::snprintf(bpm, sizeof bpm, "%s %s %dBPM", sg.chip == atari::Chip::Pokey ? "POKEY" : "TIA",
                      sg.region == atari::Region::PAL ? "PAL" : "NTSC", bpmOfPair(sg.speed, sg.funk, sg.region, sg.engineSpeed));
        const int bw = g.textWidth(kText, bpm);
        right -= bw;
        g.text(right, 6, kText, col::kText, bpm);
        right -= 12;
    }
    const char* posText[3] = {};
    uint32_t posCol = col::kDim;
    if (now_ < statusUntil_) {
        posText[0] = status_;
        posCol = col::kWarn;
    } else if (e.playing()) {
        const bool song = e.sequencerMode() == Sequencer::kSong;
        std::snprintf(pos, sizeof pos, "%s %02X PAT %02X ROW %02X", song ? "POS" : "LOOP", unsigned(e.orderPos()),
                      unsigned(e.pattern()), unsigned(e.row()));
        char shortPos[40];
        std::snprintf(shortPos, sizeof shortPos, "%02X.%02X.%02X", unsigned(e.orderPos()), unsigned(e.pattern()),
                      unsigned(e.row()));
        static char shortBuf[40];
        std::snprintf(shortBuf, sizeof shortBuf, "%s", shortPos);
        posText[0] = pos;
        posText[1] = shortBuf;
        posCol = col::kGood;
    } else if (e.countingIn()) {
        posText[0] = "COUNT-IN";
        posCol = col::kWarn;
    } else {
        posText[0] = "STOPPED";
    }
    // Title takes what is left of the left side, keeping room for the status text it would crowd.
    const int titleX = 196;
    const int titleW = g.textWidth(kText, s.title());
    int statusW = 0;
    const char* shown = nullptr;
    for (int i = 0; i < 3 && posText[i]; ++i) {
        const int w = g.textWidth(kText, posText[i]);
        if (w <= right - titleX) { shown = posText[i]; statusW = w; break; }
    }
    if (!shown && posText[0]) {   // nothing fits whole: clip the longest to the room
        static char clipped[40];
        std::snprintf(clipped, sizeof clipped, "%.24s", posText[0]);
        while (clipped[0] && g.textWidth(kText, clipped) > right - titleX) clipped[std::strlen(clipped) - 1] = 0;
        shown = clipped;
        statusW = g.textWidth(kText, clipped);
    }
    const int statusX = right - statusW;
    if (shown) g.text(statusX, 6, kText, posCol, shown);
    const int starW = app_->dirty() ? 22 : 0;
    if (titleX + titleW + starW + 10 <= statusX) {
        g.text(titleX, 6, kText, col::kText, s.title());
        if (app_->dirty()) g.text(titleX + titleW + 12, 6, kText, col::kWarn, "*");
    } else if (app_->dirty() && titleX + starW <= statusX) {
        g.text(titleX, 6, kText, col::kWarn, "*");
    }
    if (edit) g.fill(786, 6, 10, 16, col::kEdit);
    // Bottom bar: F1..F8 (a dialog shows only its own choices).
    const char* labels[8] = {};
    if (kb_.open) {
        labels[6] = "CANCEL";
        labels[7] = "OK";
    } else if (pick_.open) {
        labels[6] = "CLOSE";
    } else if (ask_.open) {
        labels[6] = "CANCEL";
        labels[7] = ask_.ok;
    } else {
        s.labels(labels);
    }
    for (int i = 0; i < 8; ++i) {
        const int x = i * 100 + 3;
        const bool lit = labels[i] && (((fHeld_ >> i) & 1) || int32_t(fFlashUntil_[i] - now_) > 0);
        g.fill(x, kBarY, 94, 50, lit ? col::kCursor : (labels[i] ? col::kPanel : col::kBg));
        if (labels[i]) g.text(x + 47, kBarY + 25, kSmall, lit ? col::kOnCursor : col::kText, labels[i], ot::IDisplay::kOptCenter);
    }
    if (now_ < msgUntil_) {
        const int w = 8 * int(std::strlen(msg_)) + 32;
        g.fill(400 - w / 2, kBarY - 40, w, 30, col::kPanelHi);
        g.frame(400 - w / 2, kBarY - 40, w, 30, col::kCursor, 1);
        g.text(400, kBarY - 25, kMono, col::kText, msg_, ot::IDisplay::kOptCenter);
    }
}

void Ui::drawHelp(ot::Gfx& g) {
    static const char* const kLines[] = {
        "KEYS  (any key closes)",
        "Pattern: pattern grid      Instr. Params: instrument",
        "Song: order list           Config: chip, region, projects",
        "Master: levels, volume     Performance: live track volume, mute, solo",
        "File: export WAV, assembly, .a78, .bin",
        "",
        "Play: start / stop - the pattern on Pattern, the song on Song / Master / Perform (Shift: the other)",
        "Rec: edit mode   Shift+Rec: undo   Shift+Sample Rec: redo",
        "Pads: notes, bottom row = octave (OCT -/+ on Pattern)",
        "      in the instrument column the pads are instruments 1-48",
        "      with Rec on volume / effect fields the first 16 pads (lit red) type 0-F",
        "Jog: value under the cursor (Shift: coarse)   hold Instr / Note / Fx1 / Fx2: pad map to pick from",
        "Hold Step / Len (F7 / F8) and turn the jog to set them   Shift+Insert: Home   Shift + arrows: select, Copy / Shift+Copy",
        "Shift + F-keys: mute a track (TIA: choose its two tracks), undo, redo   Config: BPM in Song, metronome, pre-roll",
        "Instrument pages: Shift + pads draw the volumes, F3 loop here, F4 / F5 all volumes, F6 clear   Sample loader / editor / playback: sounds / table / chip",
        "MIDI in (DIN and USB, any channel): notes play the cursor track; Rec records them (60 = C-4)",
        "Left / Right: track   Note / Instr / Fx1 / Fx2: field (Instr again: volume)   Copy, paste: Rec on",
    };
    g.fill(60, 40, 680, 370, col::kPanel);
    g.frame(60, 40, 680, 370, col::kCursor, 2);
    int y = 56;
    for (const char* l : kLines) {
        g.text(80, y, kMono, l == kLines[0] ? col::kWarn : col::kText, l);
        y += 22;
    }
}

void Ui::updateLeds() {
    ot::ILeds* leds = app_->platform().leds;
    if (!leds) return;
    static uint32_t shown[ot::kPadCount];
    static bool init = false;
    Screen& s = *screen(current_);
    bool changed = !init;
    for (int p = 0; p < ot::kPadCount && p < leds->padCount(); ++p) {
        uint32_t c;
        if (!s.padColour(p, c)) {
            const int semi = p % ot::kPadCols;
            const bool black = semi == 1 || semi == 3 || semi == 6 || semi == 8 || semi == 10;
            c = semi == 0 ? 0x002448 : (black ? 0x000008 : 0x101010);
            if (!noteInTune(inst, padNote(p))) c = 0x180400;     // TIA: a note this sound cannot play in tune
        }
        if ((padsDown_ >> p) & 1) c = 0x00A0FF;
        if (c != shown[p] || !init) {
            shown[p] = c;
            leds->setPad(uint8_t(p), uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c));
            changed = true;
        }
    }
    init = true;
    if (changed) leds->show();
}

// The board powers off kPowerHoldMs after the press (hal board::kPowerHoldOffMs); the bar runs on the same clock.
static constexpr uint32_t kPowerHoldMs = 1500;

int Ui::powerProgress() const {
    if (!powerHeld_) return 0;
    const uint32_t t = now_ - powerAt_;
    return t >= kPowerHoldMs ? 100 : int(t * 100 / kPowerHoldMs);
}

void Ui::drawShutdown(ot::Gfx& g) {
    const int p = powerProgress();
    g.fill(0, 0, 800, 480, col::kBg);
    g.text(400, 190, kLarge, col::kAccent1, "SHUTTING DOWN", ot::IDisplay::kOptCenter);
    g.text(400, 232, kText, col::kDim, "release the power button to cancel", ot::IDisplay::kOptCenter);
    const int w = 480, x = 400 - w / 2, y = 270;
    g.fill(x, y, w, 22, col::kPanel);
    g.fill(x, y, w * p / 100, 22, col::kAccent2);
    g.frame(x, y, w, 22, col::kCursor, 2);
}

void Ui::drawSplash(ot::Gfx& g) {
    g.fill(0, 0, 800, 480, col::kBg);
    g.text(400, 170, kTitleFont, col::kAccent1, "ATARI TRACKER", ot::IDisplay::kOptCenter);
    const Song& sg = song();
    char line[64];
    std::snprintf(line, sizeof line, "%s  %s", sg.chip == atari::Chip::Pokey ? "POKEY  4 TRACKS" : "TIA  2 TRACKS",
                  sg.region == atari::Region::PAL ? "PAL" : "NTSC");
    g.text(400, 240, kText, col::kText, line, ot::IDisplay::kOptCenter);
#ifdef SIDT_VERSION
    std::snprintf(line, sizeof line, "v%s", SIDT_VERSION);
    g.text(400, 280, kText, col::kDim, line, ot::IDisplay::kOptCenter);
#endif
}

void Ui::update(uint32_t nowMs) {
    now_ = nowMs;
    if (splashMs_ && !splashSet_) {
        splashSet_ = true;
        splashUntil_ = nowMs + splashMs_;
    }
    if (splashSet_ && int32_t(nowMs - splashUntil_) < 0) {
        if (nowMs - lastFrame_ >= 100 || frames_ == 0) {
            lastFrame_ = nowMs;
            ot::Gfx g(*app_->platform().display);
            g.beginFrame(col::kBg);
            drawSplash(g);
            g.endFrame();
            ++frames_;
        }
        return;
    }
    if (splashSet_) { splashSet_ = false; splashMs_ = 0; dirty_ = true; }
    app_->engine.setRecordClick(edit && preroll);
    for (int t = 0; t < kTracks; ++t) app_->engine.setTrackMix(t, song().trackVolume[t]);
    if (powerHeld_) {
        // Sound goes quiet early (stock: muted below 15 % left) so the codec never sees a loud signal when the power drops.
        const int p = powerProgress();
        const int v = p < 15 ? masterVolume : (p >= 55 ? 0 : masterVolume * (55 - p) / 40);
        app_->engine.setMasterVolume(uint8_t(v));
        if (p >= 85 && !hwMuted_ && app_->platform().muteOutput) {   // as the stock firmware: the output stage is muted near the end of the hold
            app_->platform().muteOutput(true);
            hwMuted_ = true;
        }
        dirty_ = true;
    }
    if (previewChannel_ >= 0 && int32_t(nowMs - previewUntil_) >= 0) {
        bool held = false;
        for (int p = 0; p < ot::kPadCount; ++p) held = held || (padHeld_[p] && padChannel_[p] == previewChannel_);
        if (!held) app_->engine.noteOff(previewChannel_);
        previewChannel_ = -1;
    }
    if (pick_.instrHeld && !pick_.open && !ask_.open && !help_ && nowMs - pick_.instrDownAt >= 180)   // the stock hold time
        openPicker(false);   // held: choose on release
    if (hold_.down && hold_.id != b::Instr && pop_ == Pop::kNone && !pick_.open && !ask_.open && !help_ && !kb_.open && current_ == ScreenId::kPattern &&
        nowMs - hold_.at >= 180) {
        pop_ = hold_.id == b::Note ? Pop::kNote : (hold_.id == b::Fx1 ? Pop::kFx1 : Pop::kFx2);
        popSel_ = 0;
        if (pop_ != Pop::kNote) {
            uint8_t list[16];
            const int n = fxList(song().chip, list);
            const uint8_t cur = pattern_.effectAt(pop_ == Pop::kFx1 ? 0 : 1);
            for (int i = 0; i < n; ++i) if (list[i] == cur) popSel_ = i;
        }
        dirty_ = true;
    }
    if (nowMs - lastLeds_ >= 40) {
        lastLeds_ = nowMs;
        updateLeds();
    }
    Screen& s = *screen(current_);
    bool flashing = false;
    for (int i = 0; i < 8; ++i) flashing = flashing || int32_t(fFlashUntil_[i] + 40 - nowMs) > 0;
    const bool animate = flashing || s.animated() || app_->engine.playing() || nowMs < msgUntil_ + 40 || nowMs < statusUntil_ + 40;
    const uint32_t period = dirty_ ? 16u : 40u;
    if (!(dirty_ || animate) || nowMs - lastFrame_ < period) return;
    lastFrame_ = nowMs;
    dirty_ = false;
    ot::Gfx g(*app_->platform().display);
    g.beginFrame(col::kBg);
    if (kb_.open) drawKeyboard(g);
    else if (pick_.open) drawPicker(g);
    else if (pop_ != Pop::kNone) drawPopup(g);
    else if (!help_) s.draw(g);
    drawChrome(g, s);
    if (help_) drawHelp(g);
    if (ask_.open) drawDialog(g);
    if (powerHeld_) drawShutdown(g);
    g.endFrame();
    ++frames_;
}

}  // namespace ui
}  // namespace atw
