// GB tracker -- user interface shell: one Screen at a time over IDisplay + ot::Gfx, the power-hold shutdown bar and a boot splash.
// Main loop only. The screens (pattern, instrument, song ...) are added later; today there is only the blank one.
#pragma once

#include <cstdint>

#include "atw/ui/screen.h"
#include "hal_api.h"
#include "ui/buttons.h"

namespace atw {
class App;
namespace ui {

enum class ScreenId : uint8_t { kBlank, kCount };

// The first screen: nothing but the background colour.
class BlankScreen final : public Screen {
public:
    const char* title() const override { return ""; }
    void draw(ot::Gfx&) override {}
};

class Ui {
public:
    void init(App& app);
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
    void drawSplash(ot::Gfx& g);

    App* app_ = nullptr;
    BlankScreen blank_;
    ScreenId current_ = ScreenId::kBlank;
    bool dirty_ = true;
    uint32_t now_ = 0, lastFrame_ = 0, frames_ = 0;
    uint32_t splashMs_ = 0, splashUntil_ = 0;
    bool splashSet_ = false;
    bool powerHeld_ = false, hwMuted_ = false;
    uint32_t powerAt_ = 0;
};

}  // namespace ui
}  // namespace atw
