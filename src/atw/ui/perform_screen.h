// Atari tracker -- Perform: live track volume, mute and solo while the song plays.
//   Left/Right track   jog / Up / Down  volume 0-F of the note sounding on the track (the effect V does the same)
//   F1-F4 mute track   F5-F8 solo track (while any track is soloed only those sound)
// With Rec on and the song playing, a volume change is written as a V effect at the row being played. Pads play notes as everywhere.
#pragma once

#include "atw/ui/screen.h"

namespace atw {
namespace ui {

class PerformScreen final : public Screen {
public:
    const char* title() const override { return "PERFORM"; }
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    bool button(uint8_t id, bool down) override;
    void encoder(int delta) override;
    bool animated() const override { return true; }

private:
    void setVolume(int track, int v);
    int track_ = 0;
    uint8_t vol_[4] = {15, 15, 15, 15};
    char lab_[8][16];
};

}  // namespace ui
}  // namespace atw
