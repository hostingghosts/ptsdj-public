#include "atw/ui/pattern_screen.h"

#include <cstdio>
#include <cstring>

#include "atw/app.h"
#include "ui/buttons.h"

namespace atw {
namespace ui {

namespace {
constexpr int kGridX = 24;
constexpr int kChanW = 186;   // 16 grid characters per track and a gap
constexpr int kHeadY = kContentY + 2;
constexpr int kChanY = kHeadY + 22;
constexpr int kRowY = kChanY + 20;
constexpr int kRowH = 16;
constexpr int kHelpY = kRowY + PatternScreen::kRows * kRowH + 8;
// Character offset and width of each cursor field inside "C-4 01 F V0C s04".
constexpr int kFieldChar[PatternScreen::kFields] = {0, 4, 7, 9, 10, 11, 13, 14, 15};
constexpr int kFieldW[PatternScreen::kFields] = {3, 2, 1, 1, 1, 1, 1, 1, 1};
constexpr uint8_t kLengths[] = {16, 32, 48, 64, 96, 128};

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

Pattern& PatternScreen::pat() { return ui().song().pattern(ui().pattern); }
Cell& PatternScreen::cell() {
    if (row_ >= pat().rows) row_ = pat().rows - 1;   // the length may have shrunk
    return pat().cells[row_][ui().channel];
}

void PatternScreen::edited() { ui().app().markDirty(); }

void PatternScreen::moveRow(int d) {
    const int rows = pat().rows;
    row_ = ((row_ + d) % rows + rows) % rows;
}

void PatternScreen::advance() {
    if (ui().step) moveRow(ui().step);
}

void PatternScreen::setEffect(int slot, uint8_t type) {
    Cell& c = cell();
    c.fx[slot].type = type;
    if (!type) c.fx[slot].value = 0;
}

void PatternScreen::enterHex(int d) {
    Cell& c = cell();
    switch (field_) {
        case 2:   // volume: 0-F
            c.vol = uint8_t(d + 1);
            advance();
            break;
        case 4: case 7: {
            Effect& e = c.fx[fxSlot(field_)];
            e.value = uint8_t((d << 4) | (e.value & 15));
            field_++;
            break;
        }
        case 5: case 8: {
            Effect& e = c.fx[fxSlot(field_)];
            e.value = uint8_t((e.value & 0xF0) | d);
            field_--;
            advance();
            break;
        }
        default: break;
    }
    edited();
}

// Rows per beat go through 1, 2, 4, 8, 16, 32, 64; a tap wraps round, the jog stops at the ends.
void PatternScreen::stepBeat(int d, bool wrap) {
    Ui& u = ui();
    Song& sg = u.song();
    int i = 0;
    while (i < 6 && (1 << (i + 1)) <= sg.stepsPerBeat) ++i;
    i += d;
    if (wrap) i = (i + 7) % 7;
    const int n = 1 << clampi(i, 0, 6);
    if (n != sg.stepsPerBeat) {                    // the beat keeps its length: only the number of rows in it changes
        sg.stepsPerBeat = uint8_t(n);
        u.app().markDirty();
    }
    char m[112];
    const int bpm = bpmOfPair(sg.speed, sg.funk, sg.region, sg.engineSpeed);
    if (n > 4 * sg.speed) std::snprintf(m, sizeof m, "%d steps per beat is more than the tempo allows: one step per tick, so %d BPM becomes about %d", n, bpm, bpm * 4 * sg.speed / n);
    else std::snprintf(m, sizeof m, "%d steps per beat, %d BPM, a step lasts %d.%02d ticks", n, bpm, 4 * sg.speed / n, (4 * sg.speed % n) * 100 / n);
    u.status(m, 2400);
}

void PatternScreen::encoder(int d) {
    if (stepHeld_) {                       // hold STEP (F7) and turn: the rows advanced after an entry
        ui().step = uint8_t(clampi(ui().step + d, 0, 16));
        heldTurned_ = true;
        return;
    }
    if (beatHeld_) {                       // hold Shift + F8 and turn: rows per beat
        stepBeat(d, false);
        heldTurned_ = true;
        return;
    }
    if (lenHeld_) {                        // hold LEN (F8) and turn: the pattern length
        Pattern& p = pat();
        p.rows = uint8_t(clampi(p.rows + d * (ui().shift ? 16 : 1), 1, kMaxRows));
        if (row_ >= p.rows) row_ = p.rows - 1;
        edited();
        heldTurned_ = true;
        return;
    }
    if (!ui().edit) {
        clearSelection();
        moveRow(d);
        return;
    }
    Cell& c = cell();
    const bool sh = ui().shift;
    const atari::Chip chip = ui().song().chip;
    switch (field_) {
        case 0:
            if (note::isNote(c.note)) c.note = uint8_t(clampi(c.note + d * (sh ? 12 : 1), note::kFirst, note::kLast));
            else c.note = uint8_t(clampi(1 + ui().octave * 12, note::kFirst, note::kLast));
            if (!c.inst) c.inst = ui().inst;
            ui().app().engine.noteOn(ui().channel, c.note, c.inst);
            break;
        case 1: {
            int v = c.inst, dir = d < 0 ? -1 : 1;
            for (int k = 0, left = d < 0 ? -d : d; k < 4 * kInstrumentCount && left > 0; ++k) {   // steps over instruments this track cannot play
                const int step = sh ? 16 : 1;
                v = clampi(v + dir * step, 0, kInstrumentCount);
                if (v == 0 || v == kInstrumentCount || ui().instrumentFits(uint8_t(v), ui().channel)) --left;
                if (v == 0 || v == kInstrumentCount) break;
            }
            if (v && !ui().instrumentFits(uint8_t(v), ui().channel)) break;
            c.inst = uint8_t(v);
            if (c.inst) ui().inst = c.inst;
            break;
        }
        case 2: c.vol = uint8_t(clampi(c.vol + d, 0, 16)); break;
        case 3: case 6: {
            uint8_t list[16];
            const int n = fxList(chip, list);
            Effect& e = c.fx[fxSlot(field_)];
            int idx = -1;   // -1 = none
            for (int i = 0; i < n; i++)
                if (list[i] == e.type) idx = i;
            idx = clampi(idx + d, -1, n - 1);
            setEffect(fxSlot(field_), idx < 0 ? 0 : list[idx]);
            break;
        }
        default: {
            Effect& e = c.fx[fxSlot(field_)];
            const bool hi = field_ == 4 || field_ == 7;
            e.value = uint8_t(e.value + d * (sh || hi ? 16 : 1));
            if (!e.type) e.type = atari::FX_VOLUME;   // a parameter without an effect picks the first one
            break;
        }
    }
    edited();
}

bool PatternScreen::pad(int p, bool down, uint8_t vel) {
    (void)vel;
    if (!down) {
        ui().auditionOff(p);
        return true;
    }
    if (field_ == 1) {
        // The instrument column, as on the stock Tracker: pad 1-48 is instrument 1-48. In edit mode it is written into the step (an
        // empty step also gets a note) and the cursor moves on; otherwise the pad just chooses it. Either way it plays.
        if (p >= kInstrumentCount) return true;
        const uint8_t slot = uint8_t(p + 1);
        char why[48];
        if (!ui().instrumentFits(slot, ui().channel, why)) {   // belongs to other tracks (16-bit pair, 1.79 MHz, filter)
            ui().status(why, 2500);
            return true;
        }
        ui().inst = slot;
        ui().auditionNote(p, slot, uint8_t(1 + (ui().octave + 1) * 12));
        if (ui().edit) {
            writeInstrument(slot);
            advance();
        }
        return true;
    }
    if ((field_ == 3 || field_ == 6) && ui().edit) {   // effect letter: the pads pick from the chip's list
        uint8_t list[16];
        const int n = fxList(ui().song().chip, list);
        if (p < n) {
            setEffect(fxSlot(field_), list[p]);
            field_++;                                   // on to the parameter
            edited();
        }
        return true;
    }
    if (hexPads() && p < 16) {
        enterHex(p);
        return true;
    }
    if (field_ == 0) notePad(p, vel);
    else ui().auditionOn(p, ui().inst);
    return true;
}

bool PatternScreen::recordTarget(Pattern*& pat, int& row) {
    const AudioEngine& e = ui().app().engine;
    if (!e.playing()) return false;
    const Sequencer& sq = e.sequencer();
    pat = &ui().song().pattern(e.pattern());
    row = e.row();
    const int sp = sq.speed() > 0 ? sq.speed() : 1;
    if (sq.frameInRow() * 2 >= sp) row = (row + 1) % pat->rows;   // a note played late in the row belongs to the next one
    return true;
}

void PatternScreen::notePad(int p, uint8_t vel) {
    (void)vel;
    const uint8_t n = ui().padNote(p);
    if (!n) return;
    ui().auditionOn(p, ui().inst);                 // says why when the instrument is not for this track
    if (!ui().edit || !ui().instrumentFits(ui().inst, ui().channel)) return;
    Pattern* tp;
    int tr;
    if (recordTarget(tp, tr)) {            // record + play: at the player head, the cursor and STEP are not involved
        Cell& c = tp->cells[tr][ui().channel];
        c.note = n;
        c.inst = ui().inst;
        recorded_[ui().channel] = true;
        edited();
        return;
    }
    Cell& c = cell();
    c.note = n;
    c.inst = ui().inst;
    advance();
    edited();
}

// A key held during Rec + Play was let go: the note ends where the player head is. A release inside the row the note was
// recorded in goes to the next row, so the note itself is never overwritten.
void PatternScreen::recordOff(int channel) {
    if (channel < 0 || channel >= kTracks || !recorded_[channel]) return;
    recorded_[channel] = false;
    Pattern* tp;
    int tr;
    if (!ui().edit || !recordTarget(tp, tr)) return;
    if (tp->cells[tr][channel].note) tr = (tr + 1) % tp->rows;
    Cell& c = tp->cells[tr][channel];
    if (c.note) return;
    c.note = note::kOff;
    edited();
}

void PatternScreen::chooseEffect(int slot, uint8_t type) {
    if (!ui().edit) return;
    setEffect(slot, type);
    edited();
}

uint8_t PatternScreen::effectAt(int slot) { return cell().fx[slot].type; }

bool PatternScreen::hexPads() const { return ui().edit && (field_ == 2 || field_ == 4 || field_ == 5 || field_ == 7 || field_ == 8); }

bool PatternScreen::padColour(int p, uint32_t& rgb) {
    if (field_ == 1) {   // instrument column: the pads are instruments 1-48; the step's one is lit
        const uint8_t shown = const_cast<PatternScreen*>(this)->cell().inst ? const_cast<PatternScreen*>(this)->cell().inst : ui().inst;
        rgb = p + 1 == shown ? 0x00C060 : 0x081810;
        return true;
    }
    if (ui().edit && (field_ == 3 || field_ == 6)) {   // effect letters: the pads of the chip's effects
        uint8_t list[16];
        const int n = fxList(ui().song().chip, list);
        rgb = p < n ? 0x707010 : 0;
        return true;
    }
    if (!hexPads() || p >= 16) return false;
    rgb = 0x701010;   // hex keys
    return true;
}

void PatternScreen::writeInstrument(uint8_t inst) {
    // "Instruments are always added with a note" (stock manual): an empty step also gets one.
    Cell& c = cell();
    c.inst = inst;
    if (!c.note) c.note = uint8_t(1 + (ui().octave + 1) * 12);
    edited();
}

bool PatternScreen::midiNote(uint8_t n) {
    if (!ui().edit || field_ != 0 || !ui().instrumentFits(ui().inst, ui().channel)) return false;
    Pattern* tp;
    int tr;
    if (recordTarget(tp, tr)) {
        Cell& pc = tp->cells[tr][ui().channel];
        pc.note = n;
        pc.inst = ui().inst;
        recorded_[ui().channel] = true;
        edited();
        return true;
    }
    Cell& c = cell();
    c.note = n;
    c.inst = ui().inst;
    advance();
    edited();
    return true;
}

void PatternScreen::extendSelection(int dRow, int dTrack) {
    Ui& u = ui();
    if (!sel_) {
        sel_ = true;
        anchorRow_ = row_;
        anchorTrack_ = u.channel;
    }
    row_ = clampi(row_ + dRow, 0, pat().rows - 1);
    u.channel = clampi(u.channel + dTrack, 0, kTracks - 1);
}

bool PatternScreen::inSelection(int row, int track) const {
    if (!sel_) return false;
    const int r0 = anchorRow_ < row_ ? anchorRow_ : row_, r1 = anchorRow_ < row_ ? row_ : anchorRow_;
    const int t0 = anchorTrack_ < ui().channel ? anchorTrack_ : ui().channel, t1 = anchorTrack_ < ui().channel ? ui().channel : anchorTrack_;
    return row >= r0 && row <= r1 && track >= t0 && track <= t1;
}

void PatternScreen::block(int& r0, int& r1, int& t0, int& t1) const {
    r0 = r1 = row_;
    t0 = t1 = ui().channel;
    if (!sel_) return;
    r0 = anchorRow_ < row_ ? anchorRow_ : row_;
    r1 = anchorRow_ < row_ ? row_ : anchorRow_;
    t0 = anchorTrack_ < ui().channel ? anchorTrack_ : ui().channel;
    t1 = anchorTrack_ < ui().channel ? ui().channel : anchorTrack_;
}

// PREVIEW plays only what is selected, looped, from the block's first row, for as long as the key is held; with nothing selected it is the
// cursor's track, the whole pattern.
void PatternScreen::startPreview() {
    Ui& u = ui();
    int r0, r1, t0, t1;
    block(r0, r1, t0, t1);
    if (!sel_) {
        r0 = 0;
        r1 = pat().rows - 1;
    }
    uint8_t mask = 0;
    for (int t = t0; t <= t1; ++t) mask = uint8_t(mask | (1u << t));
    if (!previewing_) previewRow_ = row_;
    previewing_ = true;
    u.app().engine.playPreview(u.pattern, uint8_t(r0), uint8_t(r1), mask);
}

void PatternScreen::stopPreview() {
    if (!previewing_) return;
    previewing_ = false;
    ui().app().engine.stop();
    row_ = previewRow_;
}

// FILL writes the note of the last pad (or MIDI key) and the chosen instrument on every STEP-th row of the selected block, from its first row;
// the rows in between stay as they are. It needs Rec, like every edit.
void PatternScreen::fill() {
    Ui& u = ui();
    if (!u.edit) { u.status("Turn Rec on to fill"); return; }
    if (!sel_) { u.status("Select steps first: Shift + arrows"); return; }
    if (!u.lastNote) { u.status("Play a note on the pads first: Fill writes it"); return; }
    int r0, r1, t0, t1;
    block(r0, r1, t0, t1);
    Pattern& p = pat();
    const int every = u.step < 1 ? 1 : u.step;
    int written = 0;
    char why[48] = {};
    for (int t = t0; t <= t1; ++t) {
        if (!u.instrumentFits(u.inst, t, why)) continue;     // e.g. a 16-bit bass on track 1
        for (int r = r0; r <= r1 && r < p.rows; r += every) {
            p.cells[r][t].note = u.lastNote;
            p.cells[r][t].inst = u.inst;
            ++written;
        }
    }
    if (!written) { u.status(why[0] ? why : "Nothing to fill", 2500); return; }
    edited();
    char m[48];
    std::snprintf(m, sizeof m, "Filled %d steps", written);
    u.status(m);
}

void PatternScreen::copySelection() {
    Ui& u = ui();
    Pattern& p = pat();
    int r0 = row_, r1 = row_, t0 = u.channel, t1 = u.channel;
    if (sel_) {
        r0 = anchorRow_ < row_ ? anchorRow_ : row_;
        r1 = anchorRow_ < row_ ? row_ : anchorRow_;
        t0 = anchorTrack_ < u.channel ? anchorTrack_ : u.channel;
        t1 = anchorTrack_ < u.channel ? u.channel : anchorTrack_;
    }
    clipRows_ = r1 - r0 + 1;
    clipTracks_ = t1 - t0 + 1;
    for (int r = 0; r < clipRows_; ++r)
        for (int t = 0; t < clipTracks_; ++t) clip_[r][t] = p.cells[r0 + r][t0 + t];
    if (sel_) {                              // the cursor goes back to the block's first step, ready to paste elsewhere
        row_ = r0;
        u.channel = t0;
        clearSelection();
    }
    char m[40];
    std::snprintf(m, sizeof m, "Copied %d x %d", clipRows_, clipTracks_);
    u.status(m);
}

void PatternScreen::pasteSelection() {
    Ui& u = ui();
    Pattern& p = pat();
    if (!clipRows_) {
        u.status("Nothing copied");
        return;
    }
    const int tracks = kTracks;
    int rows = 0;
    for (int r = 0; r < clipRows_ && row_ + r < p.rows; ++r, ++rows)
        for (int t = 0; t < clipTracks_ && u.channel + t < tracks; ++t) p.cells[row_ + r][u.channel + t] = clip_[r][t];
    edited();
    clearSelection();
    if (rows > 1) row_ = clampi(row_ + rows - 1, 0, p.rows - 1);   // as on the stock Tracker: the cursor ends on the last pasted step
    u.status("Pasted");
}

bool PatternScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (id == b::F5 && !down && previewing_) {
        stopPreview();
        return true;
    }
    if (id == b::F8 && (beatHeld_ || (ui().shift && down))) {   // Shift + F8: rows per beat; a tap steps through the usual values, holding and turning sets any
        if (down) {
            beatHeld_ = true;
            heldTurned_ = false;
        } else {
            beatHeld_ = false;
            if (!heldTurned_) {
                stepBeat(1, true);
            }
        }
        return true;
    }
    if (id == b::F7 || id == b::F8) {      // a tap steps through the values, holding and turning the jog sets them
        bool& held = id == b::F7 ? stepHeld_ : lenHeld_;
        if (down) {
            if (ui().shift) return id == b::F7 ? buttonDown(id, true) : false;     // Shift + F7: the sound effect mark
            held = true;
            heldTurned_ = false;
            return true;
        }
        if (!held) return false;
        held = false;
        if (!heldTurned_) {
            Pattern& p = pat();
            if (id == b::F7) {
                ui().step = uint8_t((ui().step + 1) % 9);
            } else {
                int i = 0;
                while (i < int(sizeof kLengths) && kLengths[i] <= p.rows) ++i;
                p.rows = kLengths[i % int(sizeof kLengths)];
                if (row_ >= p.rows) row_ = p.rows - 1;
                edited();
            }
        }
        return true;
    }
    return buttonDown(id, down);
}

bool PatternScreen::buttonDown(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    Ui& u = ui();
    const bool sh = u.shift;
    Pattern& p = pat();
    const int tracks = kTracks;
    switch (id) {
        case b::Up:
        case b::Down:
        case b::Left:
        case b::Right: {
            const int dRow = id == b::Up ? -1 : (id == b::Down ? 1 : 0), dTrack = id == b::Left ? -1 : (id == b::Right ? 1 : 0);
            if (sh) {                        // Shift + arrows select, when Rec is on or nothing is playing (as on the stock Tracker)
                if (!(u.edit || !u.app().engine.playing())) return true;
                // As on the stock Tracker: Shift + Up on the first step selects the whole track; Shift + Up again, all tracks. With whole tracks selected,
                // Shift + Left / Right take in the next track as well (full height).
                if (id == b::Up && (selColumn_ || row_ == 0)) {
                    if (selColumn_) {
                        anchorTrack_ = 0;
                        u.channel = kTracks - 1;
                    } else {
                        selColumn_ = true;
                        anchorTrack_ = u.channel;
                    }
                    sel_ = true;
                    anchorRow_ = 0;
                    row_ = p.rows - 1;
                } else if (selColumn_) {
                    if (dTrack) u.channel = clampi(u.channel + dTrack, 0, kTracks - 1);
                } else {
                    extendSelection(dRow, dTrack);
                }
            } else {
                clearSelection();
                if (dRow) moveRow(dRow);
                else u.channel = clampi(u.channel + dTrack, 0, kTracks - 1);   // Left / Right move between tracks; the field stays
            }
            return true;
        }
        case b::Note:
            if (u.edit && field_ == 0) {   // the note key on a note field: note off ===
                cell().note = note::kOff;
                advance();
                edited();
            }
            field_ = 0;
            return true;
        case b::Instr: field_ = field_ == 1 ? 2 : 1; return true;                       // again: the volume column
        case b::Fx1: field_ = field_ >= 3 && field_ <= 5 ? 3 + (field_ - 3 + 1) % 3 : 3; return true;   // again: next digit
        case b::Fx2: field_ = field_ >= 6 && field_ <= 8 ? 6 + (field_ - 6 + 1) % 3 : 6; return true;
        case b::Delete:
            if (!u.edit) return true;
            if (!sh && sel_) {               // a selection: clear every step in it
                for (int r = 0; r < p.rows; ++r)
                    for (int t2 = 0; t2 < tracks; ++t2)
                        if (inSelection(r, t2)) p.cells[r][t2] = Cell{};
            } else if (sh) {                 // Shift + Delete = backspace: the step before the cursor (or selection) goes, the rest moves up
                int first = row_, t0 = u.channel, t1 = u.channel;
                if (sel_) {
                    first = anchorRow_ < row_ ? anchorRow_ : row_;
                    t0 = anchorTrack_ < u.channel ? anchorTrack_ : u.channel;
                    t1 = anchorTrack_ < u.channel ? u.channel : anchorTrack_;
                }
                if (first < 1) return true;
                for (int t2 = t0; t2 <= t1; ++t2) {
                    for (int r = first - 1; r + 1 < p.rows; ++r) p.cells[r][t2] = p.cells[r + 1][t2];
                    p.cells[p.rows - 1][t2] = Cell{};
                }
                clearSelection();
                row_ = first - 1;
            } else {
                Cell& c = cell();
                switch (field_) {
                    case 0: c.note = 0; break;
                    case 1: c = Cell{}; break;               // the instrument column clears the step (stock)
                    case 2: c.vol = 0; break;
                    case 3: case 4: case 5: c.fx[0] = Effect{}; break;
                    default: c.fx[1] = Effect{}; break;
                }
                advance();
            }
            edited();
            return true;
        case b::Insert:
            if (sh) {                        // Shift + Insert = HOME, as on the stock Tracker: back to the first step
                row_ = 0;
                clearSelection();
                return true;
            }
            if (!u.edit) return true;
            {                                // an empty step at the top of the selection (or the cursor) in every selected track, the rest moves down
                int first = row_, t0 = u.channel, t1 = u.channel;
                if (sel_) {
                    first = anchorRow_ < row_ ? anchorRow_ : row_;
                    t0 = anchorTrack_ < u.channel ? anchorTrack_ : u.channel;
                    t1 = anchorTrack_ < u.channel ? u.channel : anchorTrack_;
                }
                for (int t2 = t0; t2 <= t1; ++t2) {
                    for (int r = p.rows - 1; r > first; --r) p.cells[r][t2] = p.cells[r - 1][t2];
                    p.cells[first][t2] = Cell{};
                }
            }
            edited();
            return true;
        case b::Copy:
            if (!u.edit) {                   // as on the stock Tracker, copy and paste work in edit mode
                u.status("Rec on to copy and paste");
                return true;
            }
            if (sh) pasteSelection();
            else copySelection();
            return true;
        case b::EncPush:
            follow_ = !follow_;
            u.status(follow_ ? "Follow playback on" : "Follow playback off");
            return true;
        default: break;
    }
    if (sh && id >= b::F1 && id <= b::F4 && u.song().chip == atari::Chip::Tia) {   // TIA: which two of the four tracks play
        Song& s = u.song();
        const uint8_t t = uint8_t(id - b::F1);
        if (!s.trackUsed(t)) {
            s.tiaTrack[0] = s.tiaTrack[1];                  // the track that has been in use longest makes room
            s.tiaTrack[1] = t;
            u.app().engine.resetVoices();
            edited();
            char m[40];
            std::snprintf(m, sizeof m, "TIA plays tracks %d and %d", s.tiaTrack[0] + 1, s.tiaTrack[1] + 1);
            u.status(m);
        }
        return true;
    }
    if (sh && id == b::F7) {                       // Shift page: this pattern's track is (or is no longer) a sound effect a game can play
        Song& sg = u.song();
        const int pat = u.pattern, track = u.channel;
        const int at = sg.sfxFind(pat, track);
        if (at >= 0) {
            for (int i = at; i + 1 < sg.sfxCount; ++i) { sg.sfxPattern[i] = sg.sfxPattern[i + 1]; sg.sfxTrack[i] = sg.sfxTrack[i + 1]; }
            --sg.sfxCount;
            u.app().markDirty();
            u.status("Not a sound effect any more");
        } else if (sg.trackSlot(track) < 0) {
            u.status("The chip does not play this track");
        } else if (sg.sfxCount >= 16) {
            u.status("16 sound effects at most");
        } else {
            sg.sfxPattern[sg.sfxCount] = uint8_t(pat);
            sg.sfxTrack[sg.sfxCount] = uint8_t(track);
            ++sg.sfxCount;
            char m[96];
            std::snprintf(m, sizeof m, "SFX %d marked. Nothing changes here; an exported game plays it on a channel it picks", sg.sfxCount - 1);
            u.app().markDirty();
            u.status(m, 2200);
        }
        return true;
    }
    if (sh && id == b::F5) { u.undoRedo(false); return true; }   // Shift page: UNDO / REDO
    if (sh && id == b::F6) { u.undoRedo(true); return true; }
    if (sh && id >= b::F1 && id <= b::F4) {   // Shift: the mute page, as on the stock Tracker
        AudioEngine& e = u.app().engine;
        const int t = id - b::F1;
        e.setMuted(t, !e.muted(t));
        return true;
    }
    switch (id) {
        case b::F1: u.changePattern(-1, follow_); clearSelection(); return true;     // while it plays and the view follows: queued for the next top
        case b::F2: u.changePattern(1, follow_); clearSelection(); return true;
        case b::F3: if (u.octave > 0) --u.octave; return true;
        case b::F4: if (u.octave < 2) ++u.octave; return true;
        case b::F5: startPreview(); return true;        // PREVIEW: the selection loops while the key is held (its release ends it)
        case b::F6: fill(); return true;                // FILL: the pad note + instrument on every STEP-th row of the selection (Rec)
        default: return false;
    }
}

void PatternScreen::labels(const char* out[8]) {
    static char step[12], len[12], beat[12];
    std::snprintf(step, sizeof step, "STEP %u", unsigned(ui().step));
    std::snprintf(len, sizeof len, "LEN %u", unsigned(pat().rows));
    std::snprintf(beat, sizeof beat, "BEAT %u", unsigned(ui().song().stepsPerBeat));
    if (ui().shift) {   // the mute page (TIA: which tracks it plays)
        static char m[4][24];
        const AudioEngine& e = ui().app().engine;
        const Song& sg = ui().song();
        for (int i = 0; i < kTracks; ++i) {
            if (sg.chip == atari::Chip::Tia) std::snprintf(m[i], sizeof m[i], sg.trackUsed(i) ? "TRACK %d ON" : "USE %d", i + 1);
            else std::snprintf(m[i], sizeof m[i], "%s %d", e.muted(i) ? "UNMUTE" : "MUTE", i + 1);
            out[i] = m[i];
        }
        out[4] = "UNDO";
        out[5] = "REDO";
        out[6] = sg.sfxFind(ui().pattern, ui().channel) >= 0 ? "SFX ON" : "SFX";
        out[7] = beat;
        return;
    }
    out[0] = "PAT -";
    out[1] = "PAT +";
    out[2] = "OCT -";
    out[3] = "OCT +";
    out[4] = "PREVIEW";
    out[5] = "FILL";
    out[6] = step;
    out[7] = len;
}

void PatternScreen::draw(ot::Gfx& g) {
    Ui& u = ui();
    Song& s = u.song();
    const AudioEngine& e = u.app().engine;
    const int tracks = kTracks;
    const bool here = e.playing() && e.pattern() == u.pattern;
    if (e.playing() && follow_ && !previewing_) {
        u.pattern = uint8_t(e.pattern());
        row_ = e.row();
    }
    Pattern& p = pat();
    if (row_ >= p.rows) row_ = p.rows - 1;
    if (u.channel >= tracks) u.channel = tracks - 1;
    char buf[96];

    std::snprintf(buf, sizeof buf, "PAT %02X  LEN %u  OCT %u  STEP %u", unsigned(u.pattern), unsigned(p.rows), unsigned(u.octave + 1), unsigned(u.step));
    if (u.shift) std::snprintf(buf, sizeof buf, "SHIFT  F7 SFX mark (games)   F8 hold + jog: steps per beat");
    g.text(8, kHeadY, kMono, u.shift ? col::kAccent2 : col::kText, buf);
    if (u.shift) {}
    else if (const int fx = u.song().sfxFind(u.pattern, u.channel); fx >= 0) {         // this track is a sound effect for a game
        std::snprintf(buf, sizeof buf, "SFX %d", fx);
        g.text(280, kHeadY, kMono, col::kAccent2, buf);
    }
    std::snprintf(buf, sizeof buf, "INS %02X %.20s", unsigned(u.inst), u.instrumentName(u.inst));
    if (!u.shift) g.text(340, kHeadY, kMono, col::kInst, buf);
    if (const int q = e.queuedPattern(); q >= 0 && e.playing() && !u.shift) {
        std::snprintf(buf, sizeof buf, "NEXT %02X", unsigned(q));
        g.text(600, kHeadY, kMono, col::kAccent2, buf);
    }
    if (u.edit) g.text(740, kHeadY, kMono, col::kEdit, "EDIT");
    else if (!follow_) g.text(724, kHeadY, kMono, col::kDim, "NOFOLLOW");

    for (int ch = 0; ch < tracks; ++ch) {
        const int x = kGridX + ch * kChanW;
        const bool used = s.trackUsed(ch);
        std::snprintf(buf, sizeof buf, "TRACK %d %s", ch + 1, !used ? "OFF" : (e.muted(ch) ? "MUTE" : ""));
        g.text(x, kChanY, kMono, e.muted(ch) || !used ? col::kDim : col::kAccent1, buf);
        if (ch > 0) g.vline(x - 8, kChanY, kRowY + kRows * kRowH - kChanY, col::kFaint);
    }

    // Rows around the cursor: backgrounds, then one glyph layer per colour, tracks outermost so the x >= 512 translate switches once.
    const bool paging = e.playing() && follow_;
    const int maxTop = p.rows > kRows ? p.rows - kRows : 0;
    int top;
    if (paging) {
        if (row_ < viewTop_ || row_ >= viewTop_ + kRows) viewTop_ = row_;
        top = clampi(viewTop_, 0, maxTop);
    } else {
        top = clampi(row_ - kRows / 2, 0, maxTop);
        viewTop_ = top;
    }
    const int shown = p.rows - top < kRows ? p.rows - top : kRows;
    const int spb = s.stepsPerBeat < 1 ? 4 : s.stepsPerBeat;
    for (int i = 0; i < shown; ++i) {
        const int r = top + i;
        const int y = kRowY + i * kRowH;
        if (r == row_) g.fill(0, y, 800, kRowH, paging ? col::kPlayRow : col::kPanelHi);
        else if (here && r == e.row()) g.fill(0, y, 800, kRowH, col::kPlayRow);
        else if (r % spb == 0) g.fill(0, y, 800, kRowH, col::kRowHi);
    }
    if (sel_)
        for (int i = 0; i < shown; ++i)
            for (int ch = 0; ch < tracks; ++ch)
                if (inSelection(top + i, ch)) g.fill(kGridX + ch * kChanW - 4, kRowY + i * kRowH, 136, kRowH, 0x28507F);
    enum Layer { kDimL, kRowNumL, kNoteL, kInstL, kVolL, kFxL, kOffL, kLayers };   // kOffL: notes the TIA sound cannot play in tune
    const uint32_t layerCol[kLayers] = {col::kFaint, col::kText, col::kNote, col::kInst, col::kGood, col::kCmd, col::kBad};
    for (int layer = 0; layer < kLayers; ++layer) {
        GlyphLayer gl(g, layerCol[layer]);
        if (layer == kDimL || layer == kRowNumL) {
            for (int i = 0; i < shown; ++i) {
                const int r = top + i;
                const bool bright = r % spb == 0 || r == row_;
                if (bright != (layer == kRowNumL)) continue;
                hex2(buf, uint8_t(r));
                buf[2] = 0;
                gl.put(2, kRowY + i * kRowH, buf);
            }
        }
        for (int ch = 0; ch < tracks; ++ch) {
            const int x = kGridX + ch * kChanW;
            const bool used = s.trackUsed(ch);
            for (int i = 0; i < shown; ++i) {
                const Cell& c = p.cells[top + i][ch];
                const int y = kRowY + i * kRowH;
                if (!used) {                                    // a track the chip does not play: everything in the dim layer
                    if (layer != kDimL) continue;
                    char n[4] = "---";
                    if (c.note) noteName(c.note, n);
                    gl.put(x, y, n);
                    if (c.inst) { hex2(buf, c.inst); buf[2] = 0; gl.put(x + 32, y, buf); } else gl.put(x + 32, y, "--");
                    if (c.vol) { buf[0] = hexDigit(c.vol - 1); buf[1] = 0; gl.put(x + 56, y, buf); } else gl.put(x + 56, y, "-");
                    for (int k = 0; k < 2; k++) {
                        if (c.fx[k].type) { buf[0] = fxLetter(c.fx[k].type); hex2(buf + 1, c.fx[k].value); buf[3] = 0; gl.put(x + 72 + k * 32, y, buf); }
                        else gl.put(x + 72 + k * 32, y, "---");
                    }
                    continue;
                }
                switch (layer) {
                    case kDimL:
                        if (!c.note) gl.put(x, y, "---");
                        if (!c.inst) gl.put(x + 32, y, "--");
                        if (!c.vol) gl.put(x + 56, y, "-");
                        for (int k = 0; k < 2; k++)
                            if (!c.fx[k].type) gl.put(x + 72 + k * 32, y, "---");
                        break;
                    case kNoteL:
                        if (c.note && u.noteInTune(c.inst ? c.inst : u.inst, c.note)) {
                            char n[4];
                            noteName(c.note, n);
                            gl.put(x, y, n);
                        }
                        break;
                    case kOffL:                             // red: a TIA note the sound cannot play in tune, an instrument this track cannot play
                        if (c.note && !u.noteInTune(c.inst ? c.inst : u.inst, c.note)) {
                            char n[4];
                            noteName(c.note, n);
                            gl.put(x, y, n);
                        }
                        if (c.inst && !u.instrumentFits(c.inst, ch)) {
                            hex2(buf, c.inst);
                            buf[2] = 0;
                            gl.put(x + 32, y, buf);
                        }
                        break;
                    case kInstL:
                        if (c.inst && u.instrumentFits(c.inst, ch)) {
                            hex2(buf, c.inst);
                            buf[2] = 0;
                            gl.put(x + 32, y, buf);
                        }
                        break;
                    case kVolL:
                        if (c.vol) {
                            buf[0] = hexDigit(c.vol - 1);
                            buf[1] = 0;
                            gl.put(x + 56, y, buf);
                        }
                        break;
                    case kFxL:
                        for (int k = 0; k < 2; k++)
                            if (c.fx[k].type) {
                                buf[0] = fxLetter(c.fx[k].type);
                                hex2(buf + 1, c.fx[k].value);
                                buf[3] = 0;
                                gl.put(x + 72 + k * 32, y, buf);
                            }
                        break;
                    default: break;
                }
            }
        }
    }
    // Cursor field box.
    {
        const int i = row_ - top;
        const int x = kGridX + u.channel * kChanW + kFieldChar[field_] * 8;
        g.frame(x - 1, kRowY + i * kRowH, kFieldW[field_] * 8 + 2, kRowH, u.edit ? col::kEdit : col::kCursor, 2);
    }

    // Help line for the field under the cursor.
    const Cell& c = p.cells[row_][u.channel];
    if (field_ == 0) {
        std::snprintf(buf, sizeof buf, "NOTE  pads / jog: note   Note key: off ===   Shift+Delete: delete row");
    } else if (field_ == 1) {
        std::snprintf(buf, sizeof buf, "INSTRUMENT %02X %.16s   pads 1-48: instrument   hold Instr: all", unsigned(c.inst), c.inst ? u.instrumentName(c.inst) : "-");
    } else if (field_ == 2) {
        std::snprintf(buf, sizeof buf, "VOLUME %c   pads 1-16: 0-F (Rec)   jog: level", c.vol ? hexDigit(c.vol - 1) : '-');
    } else {
        const Effect& fx = c.fx[fxSlot(field_)];
        if (fx.type) std::snprintf(buf, sizeof buf, "FX %c%02X  %s  (%s)", fxLetter(fx.type), unsigned(fx.value), fxName(fx.type), fxParamHelp(fx.type));
        else std::snprintf(buf, sizeof buf, "EFFECT  pads / jog: V S s A v T B a C");
    }
    g.text(8, kHelpY, kMono, col::kDim, buf);
    if (hexPads()) g.text(596, kHelpY, kMono, col::kEdit, "HEX PADS");
}

}  // namespace ui
}  // namespace atw
