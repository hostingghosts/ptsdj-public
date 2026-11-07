// GB tracker device entry point (MK66FX1M0). Boot order kept exactly as in
// OpenTracker (docs/firmware-map.md): USB diagnostics first, then rail + SD
// and the ot_restore.txt escape hatch before any other driver, then the
// rest of the board, then the app (only with working SDRAM, as in OpenTracker's
// boot order; the song data will live there), else the hardware selftest.
#include <Arduino.h>
#include <cstring>

#include "atw/app.h"
#include "audio_sgtl5000.h"
#include "board.h"
#include "diag.h"
#include "ft81x.h"
#include "fw_update.h"
#include "hal.h"
#include "input.h"
#include "sd_storage.h"
#include "selftest.h"

namespace appns = atw;

namespace {

using namespace ot;

appns::App gApp;

uint32_t dwtCycles() { return ARM_DWT_CYCCNT; }

bool driverStats(appns::DriverStats& d) {
    hal::Sgtl5000Audio& a = hal::audio();
    d.blocks = a.renderCyclesWindow(d.minC, d.avgC, d.maxC);
    d.underruns = a.underruns();
    d.displayFaults = hal::display().faults();
    return true;
}

void setRawDetents(bool on) { hal::input().setRawDetents(on); }

// hal::powerOff() calls this with the audio still running: make sure it is silent.
void powerOffHook() {
    gApp.quiesce();
    delay(40);   // let the audio callback apply it
}

}  // namespace

extern "C" int main(void) {
    hal::diagInit();
    if (hal::boardEarlyInit().sdMounted) {
        hal::diagStage("restore trigger check");
        hal::checkRestoreTrigger(hal::storage());
        hal::diagLog("restore: no trigger");
    }
    // Switched off with the power button (EEPROM power flag 0): stay off until
    // the button is pressed, as stock does, even when power comes back.
    hal::bootPowerGate();
    const hal::BootReport& boot = hal::boardInit();
#if !defined(OT_FORCE_SELFTEST)
    if (boot.sdramOk) {
        hal::diagStage("application");
        hal::diagFreezeBoot();
        hal::diagWatchdog(false);   // the app main loop has no diagBeat()
        // The codec init leaves the headphone amp muted (spec 01 §5.1).
        hal::audio().setHeadphoneVolume(0.7f);
        hal::HalSet& h = hal::halSet();
        appns::Platform p;
        p.display = h.display;
        p.audio = h.audio;
        p.storage = boot.sdMounted ? h.storage : nullptr;
        p.input = h.input;
        p.leds = h.leds;
        p.clock = h.clock;
        p.service = h.service;
        p.sampleRateMilliHz = board::kSampleRateMilliHz;   // MCLK / 256 = 44,117,647 mHz
        p.midi = h.midi;
        p.bigMem = reinterpret_cast<uint8_t*>(board::kSdramBase);   // 16 MB SDRAM: the kit ROM image
        p.bigMemSize = board::kSdramSize;
        p.name = "K66 180 MHz";
        p.cycleCounter = &dwtCycles;
        p.driverStats = &driverStats;
        p.setRawDetents = &setRawDetents;
        p.isUpdateImageName = &hal::isUpdateImageName;
        p.requestFirmwareUpdate = &hal::requestFirmwareUpdate;
        p.muteOutput = &hal::muteOutput;
        if (gApp.init(p)) {
            hal::setPowerOffHook(&powerOffHook);
            gApp.ui.showSplash(3600);
            gApp.boot();
            gApp.run();
        }
    }
#endif
    (void)boot;
    hal::diagStage("selftest");
    hal::diagWatchdog(true);
    hal::runSelftest();
}
