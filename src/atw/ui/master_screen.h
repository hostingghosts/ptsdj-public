// Atari tracker -- Master: the output level. Jog = master volume; hold F1-F4 and turn the jog for that track's volume (kept with the song); bars show what each track is writing to the chip right now (0-F), and the
// output peak (hold the loudest sample for a moment). Meters only; nothing here is saved with the song.
#pragma once

#include "atw/ui/screen.h"

namespace atw {
namespace ui {

class MasterScreen final : public Screen {
public:
    const char* title() const override { return "MASTER"; }
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    bool button(uint8_t id, bool down) override;
    void encoder(int delta) override;
    bool animated() const override { return true; }

private:
    int peak_ = 0;          // held peak, 0..32767
    uint32_t peakAt_ = 0;
    int lvl_[4] = {};       // track bars: instant rise, slow fall
};

}  // namespace ui
}  // namespace atw
