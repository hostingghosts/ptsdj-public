// Atari tracker -- Song screen: the order list. Each entry plays a pattern `repeat` times; the song loops to the restart position.
//   Up/Down  entry   Left/Right  field (pattern, repeat, the four track cells; a cell is another pattern, "same as the entry" or "out")   jog  value   Insert / Delete  entry   F1 restart here   F2 edit pattern
//   F3/F4    speed (video frames per row)   F5/F6  funk (frames of the odd rows, 0 = off)   F8  tap tempo
#pragma once

#include "atw/ui/screen.h"

namespace atw {
namespace ui {

class SongScreen final : public Screen {
public:
    const char* title() const override { return "SONG"; }
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    bool button(uint8_t id, bool down) override;
    void encoder(int delta) override;
    bool animated() const override { return true; }

private:
    enum { kFieldPattern, kFieldRepeat, kFieldTrack, kFields = kFieldTrack + 4 };   // fields: PAT, REP, then T1..T4
    int field_ = 0;
    uint32_t taps_[5] = {};   // tap tempo: the last tap times (ms)
    int tapCount_ = 0;
    void setSpeed(int speed);
    void tap();
    void edited();
    void nudgeFunk(int d);               // FUNK -/+ (with "funk keeps tempo": speed takes the opposite step)
};

}  // namespace ui
}  // namespace atw
