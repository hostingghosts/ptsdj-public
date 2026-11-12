// The Master Mix page (Shift + Master): the output chain as a Game Boy style page, and its help. The values live in the App; this only draws them.
#pragma once

#include "gb/dual.h"
#include "gb/ui/lcd.h"

namespace gb {

constexpr int kMixRows = 5;                 // limiter on / off, drive, ceiling, release, master volume

struct MixView {
    OutputLimiter::Settings m;
    int master = 32, masterMax = 32;        // the master volume step, 1.5 dB each
    int row = 0;                            // the setting under the cursor
    float reductionDb = 0;                  // how far the limiter is turning the sound down now (0 or below)
    float outDb = -90;                      // the loudest sample leaving the Tracker, dBFS
};

void drawMixPage(Lcd& l, const MixView& v);
int mixHelpPages();
void drawMixHelp(Lcd& l, int page);         // the F8 pop-up: covers the page, like the other pages' help
bool mixHelpFits();                         // every help line fits (23 characters, 13 lines to an entry)

}  // namespace gb
