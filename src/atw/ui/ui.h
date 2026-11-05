// Atari tracker -- user interface: screens over IDisplay + ot::Gfx. Main loop only; the audio engine is reached through its API.
//
// Module keys: Pattern -> Pattern, [Instr. Params] -> Instrument, [Song] -> Song, [Config] -> Settings, [Master] -> Master, [Performance] -> Perform, [File] -> Export,
// [Help] -> key help.
// Play: start / stop (Shift+Play loops the pattern). Rec: edit mode. Hold Instr anywhere: the instrument picker (a 12 x 4 grid like the
// pads, names from the song); pads pick and play a slot, releasing Instr chooses it.
#pragma once

#include <cstdint>

#include "atw/atsong.h"
#include "atw/undo.h"
#include "atw/ui/export_screen.h"
#include "atw/ui/instrument_screen.h"
#include "atw/ui/master_screen.h"
#include "atw/ui/perform_screen.h"
#include "atw/ui/pattern_screen.h"
#include "atw/ui/screen.h"
#include "atw/ui/settings_screen.h"
#include "atw/ui/song_screen.h"
#include "hal_api.h"
#include "ui/buttons.h"

namespace atw {
class App;
namespace ui {

enum class ScreenId : uint8_t { kPattern, kInstrument, kSong, kSettings, kMaster, kPerform, kExport, kCount };

class Ui {
public:
    enum class Pop : uint8_t { kNone, kNote, kFx1, kFx2 };
    void init(App& app);
    void handle(const ot::InputEvent& ev);   // one panel event; brackets it for undo
    void update(uint32_t nowMs);
    void show(ScreenId id);
    ScreenId current() const { return current_; }
    // Results of commands: a framed box above the bottom bar. Routine feedback: a short line in the title bar.
    void message(const char* text, uint32_t ms = 1500);
    void status(const char* text, uint32_t ms = 1200);
    // Confirm dialog: a screen asks before a destructive action. The first call opens it and returns false; F8 re-sends button `action`
    // to the screen, and then confirm() returns true; F7 or any other key cancels.
    bool confirm(Screen* s, uint8_t action, const char* question, const char* ok = "OK");
    bool confirming(uint8_t action) const { return confirmed_ == action; }
    bool dialogOpen() const { return ask_.open; }
    void songChanged();   // after a load / new: reset cursors
    bool pickerOpen() const { return pick_.open; }
    bool popupOpen() const { return pop_ != Pop::kNone; }   // the Note / Fx pad map (hold Note, Fx1, Fx2)
    // Pad keyboard: types a name on the 48 pads. F8 (OK) re-sends button `action` to the screen, which then reads the text from
    // keyboardResult(action); F7 or any module key cancels. `fileName` allows only A-Z 0-9 _ - (no spaces).
    void keyboard(Screen* s, uint8_t action, const char* title, const char* initial, bool fileName = false);
    const char* keyboardResult(uint8_t action) const { return kb_.done && kb_.action == action ? kb_.text : nullptr; }
    bool keyboardOpen() const { return kb_.open; }
    void requestRedraw() { dirty_ = true; }
    // MIDI keyboard: cell note 1..62. Auditions on the cursor track; the screen may record it (Pattern, Rec on).
    void midiNoteOn(uint8_t note);
    void midiNoteOff(uint8_t note);
    void midiAllOff();
    // PAT -/+ : while a song or pattern plays and `live` is true the new pattern is queued and starts at its top when the playing one has ended
    // (stepping back onto the playing pattern cancels it); otherwise the pattern being viewed and edited changes.
    void changePattern(int d, bool live = true);
    void undoRedo(bool redo);   // Shift+Rec / the UNDO and REDO keys of the pattern's Shift page

    App& app() { return *app_; }
    Song& song();
    // Shared editing state.
    bool shift = false;
    bool edit = false;          // Rec: pattern edit mode
    uint8_t octave = 1;         // bottom pad row octave (0..2)
    uint8_t inst = 1;           // current instrument, 1..48
    void showSplash(uint32_t ms) { splashMs_ = ms; }
    uint8_t masterVolume = 80;  // percent, not saved
    bool metronome = false;     // a click on every beat while playing (Config; kept in AtariTracker/settings.txt)
    uint8_t metroVolume = 60;   // percent
    bool preroll = false;       // Rec + Play: one bar of clicks first
    bool funkKeepsTempo = false; // Song page FUNK -/+ trade speed against funk so the pair of rows (the tempo) stays the same (Config; kept in settings.txt)
    void applyTransport();      // the metronome settings to the audio engine
    uint8_t lastNote = 0;       // the note of the pad (or MIDI key) played last, cell note 1..62 (0 = none yet): what Fill writes
    uint8_t step = 1;           // rows advanced after an entry
    uint8_t pattern = 0;        // pattern being edited
    uint8_t orderPos = 0;       // song position
    int channel = 0;            // cursor track (audition target)

    // Pad -> cell note (1..62) for the current octave; 0 = none.
    uint8_t padNote(int pad) const;
    void auditionOn(int pad, uint8_t instrument);
    void auditionOff(int pad);
    void auditionNote(int pad, uint8_t instrument, uint8_t note);   // `instrument` at `note` while the pad is held
    void retriggerHeld(uint8_t instrument);                         // the held pads again with another instrument
    void preview(uint8_t instrument);                               // 300 ms on the cursor track when no pad is held
    uint32_t now() const { return now_; }
    bool screenPadColour(int p, uint32_t& rgb) { return screen(current_)->padColour(p, rgb); }   // what the screen shown wants a pad lit with (tests)
    bool fHeld(int i) const { return (fHeld_ >> i) & 1; }   // bottom button F(i+1) is down
    uint32_t frames() const { return frames_; }
    const char* instrumentName(int slot);   // 1..48 of the song's chip
    // TIA has a 5-bit divider: each waveform reaches only part of the scale in tune. False for a note (cell note 1..62) that `instrument`
    // (1..48) plays more than 40 cents off its pitch; always true on POKEY.
    bool noteInTune(uint8_t instrument, uint8_t cellNote) const;
    // POKEY instruments that belong to particular tracks (atsong.h instrumentTrackMask) are not offered elsewhere. `why` gets the text for a refusal.
    bool instrumentFits(uint8_t instrument, int track, char* why = nullptr) const;

private:
    void handleEvent(const ot::InputEvent& ev);
    Undo::Target undoTarget();
    Screen* screen(ScreenId id);
    void drawChrome(ot::Gfx& g, Screen& s);
    void drawHelp(ot::Gfx& g);
    void drawPicker(ot::Gfx& g);
    void drawPopup(ot::Gfx& g);
    bool popupInput(const ot::InputEvent& ev);
    void closePopup(bool choose);
    void drawDialog(ot::Gfx& g);
    void drawKeyboard(ot::Gfx& g);
    void keyboardInput(const ot::InputEvent& ev);
    bool pickerInput(const ot::InputEvent& ev);
    void openPicker(bool sticky);
    void updateLeds();
    void play();

    struct Pick {
        bool open = false;
        bool sticky = false;       // opened by a tap: stays until Instr / F8 / F7
        uint8_t slot = 1;
        uint32_t instrDownAt = 0;
        bool instrHeld = false;
    } pick_;
    // Hold Note / Fx1 / Fx2 on the pattern screen: a pad map pops up (like the instrument picker); a short tap jumps to the column instead.
    struct Hold {
        uint8_t id = 0;
        bool down = false;
        uint32_t at = 0;
    } hold_;
    Pop pop_ = Pop::kNone;
    int popSel_ = 0;             // highlighted effect (index into the chip's list)
    struct Kb {
        bool open = false, done = false, fileName = false;
        char title[32] = {};
        char text[kNameLen + 1] = {};
        Screen* target = nullptr;
        uint8_t action = 0;
    } kb_;
    struct Ask {
        bool open = false;
        char text[72] = {};
        const char* ok = "OK";
        Screen* target = nullptr;
        uint8_t action = 0;
    } ask_;

    App* app_ = nullptr;
    ScreenId current_ = ScreenId::kPattern;
    PatternScreen pattern_;
    InstrumentScreen instrument_;
    SongScreen songScreen_;
    SettingsScreen settings_;
    MasterScreen master_;
    PerformScreen perform_;
    ExportScreen export_;
    bool dirty_ = true;
    bool help_ = false;
    uint32_t now_ = 0, lastFrame_ = 0, lastLeds_ = 0, frames_ = 0;
    // Power button held (the stock shutdown sequence): progress bar for kPowerHoldMs, sound fades out, release cancels.
    // Boot splash: shown for splashMs_ after the first update (set by the device/sim main, not by tests).
    uint32_t splashMs_ = 0, splashUntil_ = 0;
    bool splashSet_ = false;
    void drawSplash(ot::Gfx& g);
    // The bottom buttons light while held and flash briefly when tapped.
    uint8_t fHeld_ = 0;                  // bit i: F(i+1) is down
    uint32_t fFlashUntil_[8] = {};
    bool powerHeld_ = false, hwMuted_ = false;
    uint32_t powerAt_ = 0;
    int powerProgress() const;
    void drawShutdown(ot::Gfx& g);
    char msg_[64] = {};
    uint32_t msgUntil_ = 0;
    char status_[48] = {};
    uint32_t statusUntil_ = 0;
    int confirmed_ = -1;
    uint32_t previewUntil_ = 0;
    int previewChannel_ = -1;
    uint64_t pickerPads_ = 0;   // pads pressed inside the picker (their release is the Ui's)
    uint64_t padsDown_ = 0;
    uint8_t padHeld_[ot::kPadCount] = {};   // cell note held per pad
    int padChannel_[ot::kPadCount] = {};
    int8_t midiChannel_[64] = {};           // track a MIDI note sounds on, +1 (0 = not held)
};

}  // namespace ui
}  // namespace atw
