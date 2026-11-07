// GB tracker -- user interface shell: one Screen at a time over IDisplay + ot::Gfx, the power-hold shutdown bar and a boot splash.
// Main loop only. The screens (pattern, instrument, song ...) are added later; today there is only the blank one.
#pragma once

#include <cstdint>

#include "atw/ui/screen.h"
#include "gb/ui/lcd.h"
#include "gb/ui/lsdj.h"
#include "hal_api.h"
#include "ui/buttons.h"

namespace atw {
class App;
namespace ui {

enum class ScreenId : uint8_t { kBlank, kCount };

// The LSDJ-style screens: one Screen that draws the Game Boy display inside a grey lens and maps the Tracker's controls to its keys.
// With both projects linked (SYNC) the Master key puts the two consoles side by side; the F keys are then a small mixer.
class GbScreen final : public Screen {
public:
    void bind(App* app, gb::Lsdj* a, gb::Lsdj* b) { app_ = app; lsdjA_ = a; lsdjB_ = b; lsdj_ = a; }
    gb::Lcd& lcd() { return lcd_; }
    const char* title() const override { return ""; }
    void prepare(ot::IDisplay& d) override;
    void draw(ot::Gfx& g) override;
    bool button(uint8_t id, bool down) override;
    void encoder(int delta) override;
    bool pad(int p, bool down, uint8_t vel) override;
    bool animated() const override { return true; }
    static void drawLens(ot::Gfx& g, bool playing, int look);   // the grey Game Boy bezel around the display, with a margin of screen colour so no letter touches the frame
    static constexpr int kLcdX = 100, kLcdY = 28;           // where the single display sits

private:
    bool splitOn() const;
    void drawStrip(ot::Gfx& g, int top);
    App* app_ = nullptr;
    gb::Lsdj* lsdj_ = nullptr;                      // the console on screen (in the split view: the one the keys control)
    gb::Lsdj* lsdjA_ = nullptr;
    gb::Lsdj* lsdjB_ = nullptr;
    gb::Lcd lcd_;                                   // one buffer: the split view renders and uploads A, then B
    bool shift_ = false;
    bool split_ = false;
    bool aHeld_ = false, bHeld_ = false;            // F1 / F8 held in the split view: the F key dock is up
    uint8_t popupKey_ = 0xFF;                       // the key holding a pop-up open (F8, or Note / Instr params with Shift)
    float dock_ = 0;                                // 0 lowered .. 1 raised
    void closePopup() { if (lsdj_) lsdj_->hidePopup(); popupKey_ = 0xFF; }
    int volHeld_ = -1;                              // F2 / F7 held: the jog wheel sets that console's level
    void swapConsole() { if (lsdjB_) lsdj_ = lsdj_ == lsdjA_ ? lsdjB_ : lsdjA_; }
};

class Ui {
public:
    void init(App& app);
    ot::IDisplay& display();
    void handle(const ot::InputEvent& ev);   // one panel event
    void update(uint32_t nowMs);
    void show(ScreenId id);
    ScreenId current() const { return current_; }
    void requestRedraw() { dirty_ = true; }
    void showSplash(uint32_t ms) { splashMs_ = ms; }

    bool shift = false;

private:
    Screen* screen(ScreenId id);
    int powerProgress() const;
    void drawShutdown(ot::Gfx& g);
    void drawSplash(ot::Gfx& g, uint32_t elapsedMs);

    App* app_ = nullptr;
    GbScreen blank_;
    ScreenId current_ = ScreenId::kBlank;
    bool dirty_ = true;
    uint32_t now_ = 0, lastFrame_ = 0, frames_ = 0;
    uint32_t splashMs_ = 0, splashUntil_ = 0, splashStart_ = 0;
    int chime_ = 0;                          // splash sound: 0 waiting, 1 low note played, 2 high note played, 3 silenced
    bool splashSet_ = false;
    bool powerHeld_ = false, hwMuted_ = false;
    uint32_t powerAt_ = 0;
};

}  // namespace ui
}  // namespace atw
