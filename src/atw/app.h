// GB tracker -- application shell over the HAL interfaces (platform/hal_api.h). Platform independent: the device glue
// (app/device_main.cpp) and the simulator (sim/src/main.cpp) fill a Platform and call run() / loop().
// Today it is an empty shell: a silent audio output and a blank screen. The Game Boy sequencer and sound engine come later.
#pragma once

#include <cstdint>

#include "atw/ui/ui.h"
#include "gb/dual.h"
#include "gb/lsdsng.h"
#include "gb/player.h"
#include "gb/song.h"
#include "gb/ui/lsdj.h"
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
    ot::IMidiPort* midi = nullptr;
    uint8_t* bigMem = nullptr;         // large scratch RAM (SDRAM on the device) for the kit ROM image
    uint32_t bigMemSize = 0;
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
    bool init(const Platform& p);
    void boot();                        // the card: folders and (later) the workspace
    void loop();                        // one main-loop pass
    [[noreturn]] void run();
    // Silences the output and keeps the work (the device calls it from the power-off hook). Nothing to keep yet.
    void quiesce();

    const Platform& platform() const { return p_; }
    // Two linked Game Boys: A (tracks 1-4, the one the older code and tests mean by song/player/lsdj) and B (tracks 5-8).
    gb::Song song, songB;
    gb::Player player{song}, playerB{songB};
    gb::Lsdj lsdj{song, player}, lsdjB{songB, playerB};
    gb::Dual dual{player, playerB};
    ui::Ui ui;
    void fill(int16_t* out, uint32_t frames);   // audio callback body
    // Songs live on the card as .lsdsng files in GB/Songs (the format LSDJ's own tools use).
    bool loadSong(const char* path, int console = 0);
    bool saveSong(const char* path, const char* name, int console = 0);
    // A linked session is two files, BASE_A.lsdsng and BASE_B.lsdsng in GB/Songs.
    bool loadLinked(const char* base);
    bool saveLinked(const char* base);
    // Renders both songs once from their first rows until every channel has come round again (or `maxSeconds`); writes a 16-bit stereo WAV.
    bool exportWav(const char* path, int rate = 44100, int maxSeconds = 600);
    static constexpr const char* kSongDir = "GB/Songs";
    static constexpr const char* kExportDir = "GB/Export";   // WAV exports; the session is always called SESSION until a file browser exists
    static constexpr const char* kRomPath = "GB/LSDJ.GB";   // the owner's own LSDJ ROM: its sample kits are read from it
    bool loadKits();                             // false when there is no ROM on the card
    gb::RomKits kits;

private:
    bool exporting_ = false;
    struct LinkedTransport final : gb::Transport {
        App* app = nullptr;
        void start(int console, int row) override;
        void stop() override { app->dual.stop(); }
        bool running() const override { return app->dual.running(); }
        bool action(Action a) override;
    } transport_;
    Platform p_;
};

}  // namespace atw
