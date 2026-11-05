// Atari tracker -- application: song, audio engine and UI over the HAL interfaces (platform/hal_api.h). Platform independent: the device
// glue (app/device_main.cpp) and the simulator (sim/src/main.cpp) fill a Platform and call run() / loop().
#pragma once

#include <cstdint>
#include <string>

#include "atw/atsong.h"
#include "atw/midi_in.h"
#include "atw/audio.h"
#include "atw/export.h"
#include "atw/ui/ui.h"
#include "atw/undo.h"
#include "hal_api.h"

namespace atw {

struct DriverStats {
    uint32_t minC = 0, avgC = 0, maxC = 0, blocks = 0;   // callback cycles (DWT) this window
    uint32_t underruns = 0;                              // cumulative late blocks
    uint32_t displayFaults = 0;                          // FT81x coprocessor resets
};

struct Platform {
    ot::IDisplay* display = nullptr;
    ot::IAudioOut* audio = nullptr;
    ot::IStorage* storage = nullptr;   // null = no card
    ot::IInput* input = nullptr;
    ot::ILeds* leds = nullptr;
    ot::IClock* clock = nullptr;
    void (*service)() = nullptr;       // HAL housekeeping, once per loop pass
    uint32_t sampleRateMilliHz = 44100000;
    Pattern* patterns = nullptr;       // kPatternCount patterns (SDRAM on the device)
    ot::IMidiPort* midi = nullptr;
    const char* name = "host";
    uint32_t (*cycleCounter)() = nullptr;
    bool (*driverStats)(DriverStats& out) = nullptr;
    void (*setRawDetents)(bool on) = nullptr;
    bool (*isUpdateImageName)(const char* name) = nullptr;
    bool (*requestFirmwareUpdate)(ot::IStorage& fs, const char* image) = nullptr;
    void (*muteOutput)(bool on) = nullptr;   // hardware mute of the output stage (device only)
};

class App {
public:
    static constexpr const char* kDir = "AtariTracker";
    static constexpr const char* kProjectDir = "AtariTracker/Projects";
    static constexpr const char* kMacDir = "AtariTracker/Mac";
    static constexpr const char* kSettings = "AtariTracker/settings.txt";
    static constexpr const char* kAutosave = "AtariTracker/autosave.atr";

    bool init(const Platform& p);
    void boot();                        // the card: folders and (later) the workspace
    void loop();                        // one main-loop pass
    [[noreturn]] void run();

    void markDirty();                   // the song was edited: unsaved, and an autosave is due
    bool dirty() const { return dirty_; }
    // The project's name: Config and Export edit it, saving and exporting inherit it. "" = not named yet.
    const char* projectName() const { return project_; }
    void setProjectName(const char* name);
    // The file the song came from or was last saved to ("" = none); a name typed since is a different file.
    const char* projectFile() const { return file_; }
    // Projects on the card (AtariTracker/Projects/<NAME>.atr). All return false and fill `error` (if given) when nothing was changed.
    bool saveProject(const char* name, std::string* error = nullptr);
    bool openProject(const char* name, std::string* error = nullptr);
    void newProject();
    // The Mac Atari app's session files (AtariTracker/Mac/<NAME>.atari.json; atw/macfile.h). Opening makes a new, unsaved project.
    bool saveMacSession(const char* name, std::string* error = nullptr);
    bool openMacSession(const char* name, std::string* error = nullptr);
    int listMacSessions(char (*names)[kNameLen + 1], int max);
    int listProjects(char (*names)[kNameLen + 1], int max);   // names without the extension, sorted
    bool restoreAutosave();             // at start-up: the last session if it was not saved as a project
    void autosave();
    void saveSettings();                // metronome and pre-roll (settings.txt)                    // now (also called from loop() when the song has been idle for a while)
    // Undo / redo of song edits (atw/undo.h); false when there is nothing to do. The engine and the screens follow the change.
    bool undo();
    bool redo();
    int undoSteps() const { return undo_.undoSteps(); }
    int redoSteps() const { return undo_.redoSteps(); }
    // Around one input event (Ui::handle): what the screen edits is copied first, kept as an undo step if the event edited the song.
    void undoBegin(Undo::Target t) { serialAtBegin_ = editSerial_; epochAtBegin_ = epoch_; undo_.begin(t); }
    void undoEnd() { undo_.end(editSerial_ != serialAtBegin_ && epoch_ == epochAtBegin_, p_.clock ? p_.clock->millis() : 0); }
    bool screenshot(char* name, uint32_t cap);
    // The song's chip or region changed (Config): the engine is rebuilt with it.
    void applySongConfig();

    const Platform& platform() const { return p_; }
    Song song;
    AudioEngine engine;
    Exporter exporter;
    ui::Ui ui;

private:
    void loadSettings();
    void songReplaced();                // after new / open: engine and UI start over
    bool undoStep(bool redo);
    int listFiles(const char* dir, const char* suffix, char (*names)[kNameLen + 1], int max);
    void pollMidi();
    MidiIn midiIn_[2];
    void touch();                       // unsaved + autosave due, without being an undoable edit
    Platform p_;
    bool dirty_ = false;
    bool autosaved_ = true;
    Undo undo_;
    uint32_t editSerial_ = 0, serialAtBegin_ = 0, epoch_ = 0, epochAtBegin_ = 0;
    uint32_t editedAt_ = 0;
    char project_[kNameLen + 1] = {};
    char file_[kNameLen + 1] = {};
};

}  // namespace atw
