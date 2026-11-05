// Atari tracker -- Pattern screen.
//
// Four tracks (POKEY) or two (TIA); per row: NOTE | INSTRUMENT | VOLUME | FX1 | FX2, e.g. "C-4 01 F V0C s04".
// Cursor fields per track: note, instrument, volume, effect 1 (letter, 2 hex digits), effect 2 (letter, 2 hex digits).
//   Rec            edit mode (red cursor); edits need it
//   pads           note at the cursor (and audition); instrument column: pads 1-48 are the instruments; hex fields: pads 1-16 type 0-F;
//                  effect letter: the pads pick an effect
//   jog wheel      value under the cursor (Shift: octave / 0x10); not editing: rows
//   Up/Down        rows, Left/Right tracks; with Shift they select steps / tracks (Shift+Insert: HOME = step 0)
//   Note/Instr/Fx1/Fx2   pick the field (Instr again: volume, Fx again: next digit); hold for a pad map; Note on a note field in edit mode: note off ===
//   Delete         clear the field or selection (Shift: backspace: the step before goes, the rest moves up)
//   Insert         an empty step at the cursor / selection in its tracks, the rest moves down (Shift: Home)
//   Copy           copy the selection or the step (Shift+Copy: paste at the cursor); both need Rec, as on the stock Tracker
//   F1/F2 pattern  F3/F4 octave  F5 PREVIEW (the selection loops while held)  F6 FILL (Rec: the pad note + instrument every STEP rows of the selection)  F7 step  F8 length (hold either and turn the jog)   (Shift: mute / undo page)
#pragma once

#include "atw/atsong.h"
#include "atw/ui/screen.h"

namespace atw {
namespace ui {

class PatternScreen final : public Screen {
public:
    static constexpr int kFields = 9;   // note, inst, vol, fx1 letter / hi / lo, fx2 letter / hi / lo
    static constexpr int kRows = 18;    // visible rows (display-list budget)

    const char* title() const override { return "PATTERN"; }
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    bool button(uint8_t id, bool down) override;
    bool pad(int p, bool down, uint8_t vel) override;
    void recordOff(int channel);   // live record: the last key of a track went up -> a note-off at the player head
    bool midiNote(uint8_t note) override;
    bool padColour(int p, uint32_t& rgb) override;
    void encoder(int delta) override;
    void stepBeat(int d, bool wrap);       // rows per beat 1, 2, 4 .. 64 (Shift + F8)
    bool animated() const override { return true; }

    void notePad(int p, uint8_t vel);      // a pad as a note whatever column the cursor is in (hold Note); with Rec it is written
    void chooseEffect(int slot, uint8_t type);   // the choice of the Fx pad map (written with Rec)
    uint8_t effectAt(int slot);            // the effect type under the cursor in slot 0 / 1
    bool hexPads() const;                  // edit mode on a volume / effect parameter field: pads 1-16 type 0-F
    void writeInstrument(uint8_t inst);    // the picker's Insert (edit mode)
    int row() const { return row_; }
    void reset() { row_ = field_ = 0; }

private:
    bool buttonDown(uint8_t id, bool down);
    Pattern& pat();
    Cell& cell();
    void moveRow(int d);
    void advance();
    void enterHex(int digit);
    void setEffect(int slot, uint8_t type);
    void edited();
    static int fxSlot(int field) { return field >= 6 ? 1 : 0; }
    // Recording while the player runs goes to the player head, not to the cursor and the step setting (which are for entering without playback).
    bool recordTarget(Pattern*& pat, int& row);
    // Shift + arrows select a block of steps and tracks; Copy takes it, Shift+Copy pastes it at the cursor.
    void clearSelection() { sel_ = false; selColumn_ = false; }
    void extendSelection(int dRow, int dTrack);
    bool inSelection(int row, int track) const;
    void copySelection();
    void pasteSelection();
    // The selected block (the cursor's step when nothing is selected).
    void block(int& r0, int& r1, int& t0, int& t1) const;
    void startPreview();                   // Shift page PREVIEW: the selection loops while the key is held
    void stopPreview();
    void fill();                           // Shift page FILL: the pad note and the instrument on every STEP-th row of the selection (Rec)

    int row_ = 0;
    int field_ = 0;
    int viewTop_ = 0;   // first row shown while following playback
    bool follow_ = true;
    Cell clip_[kMaxRows][kTracks] = {};
    int clipRows_ = 0, clipTracks_ = 0;
    bool sel_ = false;
    int anchorRow_ = 0, anchorTrack_ = 0;
    bool previewing_ = false;              // PREVIEW is held: the cursor stays and returns to previewRow_ when it ends
    int previewRow_ = 0;
    bool recorded_[kTracks] = {};          // a note was recorded live on the track: its release writes a note off
    bool selColumn_ = false;               // the selection is whole tracks (stock: Shift + Up on the first step), Left / Right add the next track
    bool stepHeld_ = false, lenHeld_ = false, beatHeld_ = false, heldTurned_ = false;   // F7 / F8 held: the jog sets STEP / LEN
};

}  // namespace ui
}  // namespace atw
