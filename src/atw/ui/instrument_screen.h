// Atari tracker -- Instrument screen: the instrument editor, on pages (Instr. Params again: next page; Left / Right too).
//
//   POKEY  GENERAL   instrument, name, fade-out, minimum volume, vibrato, frequency shift
//          ENVELOPE  one step per video frame: volume, distortion, command, X / Y, filter, portamento; length and loop;
//                    or ADSR (top row): attack, decay, sustain, peak, tone, curve and the song's release shape the volume
//          TABLE     pitch table (semitones or frequency): value per step, length, loop, speed, mode, operation
//          AUDCTL    the chip's control bits for this instrument (15 kHz, high-pass, 16-bit joins, 1.79 MHz, poly 9)
//          SOUNDS    try the starter sounds, the RMT instruments and .rti files of the card on the pads: KEEP or CANCEL
//   TIA    GENERAL / STEPS (waveform, divider, volume, pitch per step) / SOUNDS
//   jog: value   Up/Down: row   pads: play the instrument   hold Instr: pick another   F1-F5: page actions   F7 / F8: previous / next instrument
#pragma once

#include <string>
#include <vector>

#include "atari/instedit.h"
#include "atw/ui/screen.h"

namespace atw {
namespace ui {

class InstrumentScreen final : public Screen {
public:
    const char* title() const override { return "INSTRUMENT"; }
    void enter() override;
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    bool button(uint8_t id, bool down) override;
    bool pad(int p, bool down, uint8_t vel) override;
    void encoder(int delta) override;
    bool padColour(int p, uint32_t& rgb) override;
    void leave();   // a sound being tried and not kept is put back

private:
    enum Action : uint8_t { kActName = 1, kActInit, kActPaste };
    int pageCount() const;
    const char* pageName(int page) const;
    int rows(const int*& list) const;     // the rows of the page: Field ids, -1 = the name
    bool adsrActive(int slot) const;
    void applyAdsr(int slot);
    void changeAdsr(int field, int delta);
    bool soundsPage() const;
    bool audctlPage() const;
    void sync();
    void editHeld(void (*fn)(InstrumentScreen&));   // the audio engine held off while the instrument changes
    void valueText(int field, char* out, size_t cap);
    void change(int field, int delta);
    bool onStepPage() const;
    void insertStep();
    void deleteStep();
    bool drawPads() const;                        // Shift held on the envelope / TIA steps page: the pads draw the volumes
    int drawStepOfPad(int p) const;               // the step a pad column stands for (the group of 12 that holds the edited step)
    void drawVolume(int p);
    void setLoopHere();           // the loop returns to the step being edited
    void shiftVolumes(int d);     // every step's volume up or down (RMT: change all steps)
    void clearStep();             // the step being edited back to empty
    void tableAll(int d);         // TABLE page: every step's value up or down
    void tableFill();             // TABLE/STEPS: every step gets the value under the cursor (TIA: in the cursor's row)
    void tableRamp();             // TABLE/STEPS: even steps, up or down, from the cursor step to the last step
    void tableClearAll();         // TABLE/STEPS: every step 0 (TIA: the row's default)
    bool tableKeys() const { return (chip_ == atari::Chip::Pokey && page_ == 2) || (chip_ == atari::Chip::Tia && page_ == 1); }
    int tiaShortcutRow() const;   // TIA: grid row under the cursor, volume outside the grid
    bool gridPage() const { return grid_ && ((chip_ == atari::Chip::Pokey && page_ == 2) || (chip_ == atari::Chip::Tia && page_ == 1)); }
    void drawGrid(ot::Gfx& g);
    void drawTiaGrid(ot::Gfx& g);
    void setName(const char* name);
    void saveRti();
public:
    // The Sample loader / Sample editor keys: SOUNDS (browse and load sounds and .rti files), or the table page (POKEY TABLE, TIA STEPS).
    void gotoSounds() { page_ = pageCount() - 1; row_ = 0; scanCard(); }
    void gotoGrid() { page_ = chip_ == atari::Chip::Pokey ? 2 : 1; row_ = 0; grid_ = !grid_; }
    void gotoTable() { page_ = chip_ == atari::Chip::Pokey ? 2 : 1; row_ = 0; }
private:
    // sounds
    void scanCard();
    int presetCount() const;
    void applyPreset();
    void keepSound();
    void cancelSound();
    void drawGraph(ot::Gfx& g);
    void changeInstrument(int d);

    atari::InstrumentEditor ed_;
    atari::Chip chip_ = atari::Chip::Pokey;
    bool bound_ = false;
    int page_ = 0, row_ = 0;
    bool grid_ = false;
    char buf_[96] = {};
    int gridRow_ = 0;            // TIA table mode: 0 waveform, 1 divider, 2 volume, 3 pitch          // table mode: the whole pitch table as a grid
    // clipboard
    bool clipHas_ = false;
    atari::PokeyInstrument clipP_;
    atari::TiaInstrument clipT_;
    // sounds page
    int cat_ = 0, preset_ = 0;
    bool trial_ = false;
    atari::PokeyInstrument bakP_;
    atari::TiaInstrument bakT_;
    char card_[48][20] = {};
    int cardCount_ = 0;
};

}  // namespace ui
}  // namespace atw
