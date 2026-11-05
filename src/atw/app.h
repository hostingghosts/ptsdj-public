// GB tracker -- application shell over the HAL interfaces (platform/hal_api.h). Platform independent: the device glue
// (app/device_main.cpp) and the simulator (sim/src/main.cpp) fill a Platform and call run() / loop().
// Today it is an empty shell: a silent audio output and a blank screen. The Game Boy sequencer and sound engine come later.
#pragma once

#include <cstdint>

#include "atw/ui/ui.h"
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
    ui::Ui ui;

private:
    Platform p_;
};

}  // namespace atw
