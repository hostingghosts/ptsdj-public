// GB tracker -- user interface shell (ui.h).
#include "atw/ui/ui.h"

#include <cstdio>

#include "atw/app.h"
#include "ui/buttons.h"

namespace atw {
namespace ui {

namespace b = ot::btn;

void Ui::init(App& app) {
    app_ = &app;
    blank_.bind(&app.lsdj, &app.lsdjB);
    for (int i = 0; i < int(ScreenId::kCount); ++i) screen(ScreenId(i))->attach(*this);
    show(ScreenId::kBlank);
}

ot::IDisplay& Ui::display() { return *app_->platform().display; }

Screen* Ui::screen(ScreenId id) {
    switch (id) {
        case ScreenId::kBlank: return &blank_;
        default: return &blank_;
    }
}

void Ui::show(ScreenId id) {
    current_ = id;
    screen(id)->enter();
    dirty_ = true;
}

void Ui::handle(const ot::InputEvent& ev) {
    dirty_ = true;
    if (ev.type == ot::InputEvent::kEncoder) { screen(current_)->encoder(ev.delta); return; }
    if (ev.type == ot::InputEvent::kPadDown || ev.type == ot::InputEvent::kPadUp) {
        screen(current_)->pad(ev.id, ev.type == ot::InputEvent::kPadDown, ev.velocity);
        return;
    }
    if (ev.type != ot::InputEvent::kButtonDown && ev.type != ot::InputEvent::kButtonUp) return;
    const bool down = ev.type == ot::InputEvent::kButtonDown;
    if (ev.id == b::Power) {
        if (down) {
            powerHeld_ = true;
            powerAt_ = now_;
        } else if (powerHeld_) {   // released before the board cut the power: cancel
            powerHeld_ = false;
            if (hwMuted_ && app_->platform().muteOutput) app_->platform().muteOutput(false);
            hwMuted_ = false;
        }
        return;
    }
    screen(current_)->button(ev.id, down);
}

// The board powers off kPowerHoldMs after the press (hal board::kPowerHoldOffMs); the bar runs on the same clock.
static constexpr uint32_t kPowerHoldMs = 1500;

int Ui::powerProgress() const {
    if (!powerHeld_) return 0;
    const uint32_t t = now_ - powerAt_;
    return t >= kPowerHoldMs ? 100 : int(t * 100 / kPowerHoldMs);
}

void Ui::drawShutdown(ot::Gfx& g) {
    const int p = powerProgress();
    g.fill(0, 0, 800, 480, col::kBg);
    g.text(400, 190, kLarge, col::kAccent1, "SHUTTING DOWN", ot::IDisplay::kOptCenter);
    g.text(400, 232, kText, col::kDim, "release the power button to cancel", ot::IDisplay::kOptCenter);
    const int w = 480, x = 400 - w / 2, y = 270;
    g.fill(x, y, w, 22, col::kPanel);
    g.fill(x, y, w * p / 100, 22, col::kAccent2);
    g.frame(x, y, w, 22, col::kCursor, 2);
}

void Ui::drawSplash(ot::Gfx& g) {
    g.fill(0, 0, 800, 480, col::kBg);
    g.text(400, 200, kTitleFont, col::kAccent1, "GB TRACKER", ot::IDisplay::kOptCenter);
#ifdef GB_VERSION
    char line[32];
    std::snprintf(line, sizeof line, "v%s", GB_VERSION);
    g.text(400, 250, kText, col::kDim, line, ot::IDisplay::kOptCenter);
#endif
}

void Ui::update(uint32_t nowMs) {
    now_ = nowMs;
    if (splashMs_ && !splashSet_) {
        splashSet_ = true;
        splashUntil_ = nowMs + splashMs_;
    }
    if (splashSet_ && int32_t(nowMs - splashUntil_) < 0) {
        if (nowMs - lastFrame_ >= 100 || frames_ == 0) {
            lastFrame_ = nowMs;
            ot::Gfx g(*app_->platform().display);
            g.beginFrame(col::kBg);
            drawSplash(g);
            g.endFrame();
            ++frames_;
        }
        return;
    }
    if (splashSet_) { splashSet_ = false; splashMs_ = 0; dirty_ = true; }
    if (powerHeld_) {
        if (powerProgress() >= 85 && !hwMuted_ && app_->platform().muteOutput) {   // as the stock firmware: mute near the end of the hold
            app_->platform().muteOutput(true);
            hwMuted_ = true;
        }
        dirty_ = true;
    }
    Screen& s = *screen(current_);
    if (frames_ != 0 && nowMs - lastFrame_ >= (dirty_ ? 16u : (s.animated() ? 40u : 1000u))) s.prepare(*app_->platform().display);
    // Redraw on change, while the screen animates, and once a second otherwise (the first frame goes out at once).
    const uint32_t period = dirty_ ? 16u : (s.animated() ? 40u : 1000u);
    if (frames_ != 0 && nowMs - lastFrame_ < period) return;
    lastFrame_ = nowMs;
    dirty_ = false;
    ot::Gfx g(*app_->platform().display);
    g.beginFrame(col::kBg);
    s.draw(g);
    if (powerHeld_) drawShutdown(g);
    g.endFrame();
    ++frames_;
}

}  // namespace ui
}  // namespace atw

namespace atw {
namespace ui {

void GbScreen::prepare(ot::IDisplay& d) {
    if (!lsdj_) return;
    lsdj_->draw(lcd_);
    lcd_.prepare(d);
}

void GbScreen::draw(ot::Gfx& g) {
    if (!lsdj_) return;
    lcd_.draw(g.display());
}

bool GbScreen::button(uint8_t id, bool down) {
    if (!lsdj_) return false;
    using gb::Key;
    switch (id) {
        case ot::btn::Up: lsdj_->key(Key::Up, down); break;
        case ot::btn::Down: lsdj_->key(Key::Down, down); break;
        case ot::btn::Left: lsdj_->key(Key::Left, down); break;
        case ot::btn::Right: lsdj_->key(Key::Right, down); break;
        case ot::btn::Insert: lsdj_->key(Key::A, down); break;       // A
        case ot::btn::Delete: lsdj_->key(Key::B, down); break;       // B
        case ot::btn::Shift: lsdj_->key(Key::Select, down); break;   // Select
        case ot::btn::Play: lsdj_->key(Key::Start, down); break;     // Start
        case ot::btn::Song: if (down && lsdjB_) lsdj_ = lsdj_ == lsdjA_ ? lsdjB_ : lsdjA_; break;   // switch between tracks 1-4 and 5-8
        default: return false;
    }
    return true;
}

void GbScreen::encoder(int delta) { if (lsdj_) lsdj_->encoder(delta); }
bool GbScreen::pad(int p, bool down, uint8_t) { if (lsdj_ && down) lsdj_->padNote(p); return true; }

}  // namespace ui
}  // namespace atw
