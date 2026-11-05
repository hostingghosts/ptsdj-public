// Atari tracker -- Export (File key): WAV, assembly, .a78 (Atari 7800), .bin (Atari 2600). The song is the order list. Files go to
// AtariTracker/Export/ on the card. While a WAV renders, only CANCEL (F7) works.
//   Up/Down  choose   F8 / jog push  export (asks first when the file exists)   F6 or Delete on the NAME row  type the name (the project's name, shared with Config)
#pragma once

#include "atw/ui/screen.h"

namespace atw {
namespace ui {

class ExportScreen final : public Screen {
public:
    const char* title() const override { return "EXPORT"; }
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    bool button(uint8_t id, bool down) override;
    void encoder(int delta) override;
    bool animated() const override { return true; }

private:
    void run();                  // asks before replacing a file that is already there
    void doRun();
    const char* exportName() const;
    int row_ = 0;
};

}  // namespace ui
}  // namespace atw
