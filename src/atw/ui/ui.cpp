// GB tracker -- user interface shell (ui.h).
#include "atw/ui/ui.h"

#include <algorithm>
#include <cstdio>

#include "atw/app.h"
#include "ui/buttons.h"

namespace atw {
namespace ui {

namespace b = ot::btn;

void Ui::init(App& app) {
    app_ = &app;
    blank_.bind(&app, &app.lsdj, &app.lsdjB);
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

// The power-on screen of the Game Boy: the block letters drop to the middle of the display, then the chime plays (Ui::update).
static constexpr uint32_t kSplashLand = 1500;

void Ui::drawSplash(ot::Gfx& g, uint32_t el) {
    GbScreen::drawLens(g, false, app_->look());
    g.color(0xFFFFFF);
    blank_.lcd().draw(g.display(), GbScreen::kLcdX, GbScreen::kLcdY, 3);
    (void)el;
}

void Ui::update(uint32_t nowMs) {
    now_ = nowMs;
    if (splashMs_ && !splashSet_) {
        splashSet_ = true;
        splashStart_ = nowMs;
        splashUntil_ = nowMs + splashMs_;
        chime_ = 0;
    }
    if (splashSet_ && int32_t(nowMs - splashUntil_) < 0) {
        const uint32_t el = nowMs - splashStart_;
        if (chime_ == 0 && el >= kSplashLand) { app_->bootChime(0); chime_ = 1; }
        if (chime_ == 1 && el >= kSplashLand + 95) { app_->bootChime(1); chime_ = 2; }
        if (chime_ == 2 && el >= kSplashLand + 1700) { app_->bootChime(2); chime_ = 3; }
        if (nowMs - lastFrame_ >= 33 || frames_ == 0) {
            lastFrame_ = nowMs;
            blank_.lcd().setLook(app_->look());
            blank_.lcd().clear(0);
            const int n = 5, scale = 3, adv = 8 * scale + 2;
            const int x0 = (gb::Lcd::kW - n * adv + 2) / 2;
            const int y = el >= kSplashLand ? 56 : -24 + int(80u * el / kSplashLand);
            static const char word[] = "PTSDJ";
            for (int i = 0; i < n; ++i) blank_.lcd().bigGlyph(x0 + i * adv, y, word[i], scale, 3);
            if (el >= kSplashLand) blank_.lcd().text(9, 11, "TRACKER");
#ifdef GB_VERSION
            { char line[32]; std::snprintf(line, sizeof line, "v%s", GB_VERSION); blank_.lcd().text(1, 17, line); }
#endif
            blank_.lcd().prepare(*app_->platform().display);
            ot::Gfx g(*app_->platform().display);
            g.beginFrame(col::kBg);
            drawSplash(g, el);
            g.endFrame();
            ++frames_;
        }
        return;
    }
    if (splashSet_) { splashSet_ = false; splashMs_ = 0; dirty_ = true; if (chime_ < 3) { app_->bootChime(2); chime_ = 3; } }
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

bool GbScreen::splitOn() const { return split_ && lsdjA_ && lsdjA_->syncLinked(); }

void GbScreen::prepare(ot::IDisplay& d) {
    if (!lsdj_) return;
    lcd_.setLook(app_->look());
    if (splitOn()) {
        lsdjA_->draw(lcd_); lcd_.prepare(d, gb::Lcd::kRamA);
        app_->pump();                               // keep the audio queue topped up between the two uploads
        lsdjB_->draw(lcd_); lcd_.prepare(d, gb::Lcd::kRamB);
    } else {
        lsdj_->draw(lcd_);
        lcd_.prepare(d);
    }
}

namespace {
constexpr uint32_t kLens = 0x77757F, kLensDark = 0x2E2C36, kLensLight = 0xD6D4DE, kMagenta = 0x8E2F5A, kNavy = 0x2D3258;
}

// The grey Game Boy bezel: dark frame round the display window (with a margin of the screen's own colour inside it), the "DOT MATRIX WITH
// STEREO SOUND" legend between two pairs of coloured lines, and the battery light on the left.
void GbScreen::drawLens(ot::Gfx& g, bool playing, int look) {
    g.fill(0, 0, 800, 480, kLens);
    g.fill(84, 14, 632, 460, kLensDark);
    g.fill(92, 20, 616, 448, gb::Lcd::kPalettes[look & 3][0]);
    g.fill(100, 3, 165, 2, kMagenta); g.fill(100, 9, 165, 2, kNavy);
    g.fill(535, 3, 165, 2, kMagenta); g.fill(535, 9, 165, 2, kNavy);
    g.text(400, 1, ui::kSmall, kLensLight, "DOT MATRIX WITH STEREO SOUND", ot::IDisplay::kOptCenterX);
    g.fill(32, 236, 14, 14, playing ? 0xFF3030 : 0x5A2A30);
    g.text(10, 258, ui::kSmall, kLensLight, "BATTERY");
}

void GbScreen::draw(ot::Gfx& g) {
    if (!lsdj_) return;
    const bool play = lsdjA_->playing() || (lsdjB_ && lsdjB_->playing());
    const int look = app_->look();
    if (!splitOn()) {
        drawLens(g, play, look);
        g.color(0xFFFFFF);
        lcd_.draw(g.display(), kLcdX, kLcdY, 3);
        g.text(750, 462, ui::kSmall, kLensLight, "?", ot::IDisplay::kOptCenterX);      // F8 explains the page
        return;
    }
    g.fill(0, 0, 800, 480, kLens);
    g.text(400, 1, ui::kSmall, kLensLight, "DOT MATRIX WITH STEREO SOUND", ot::IDisplay::kOptCenterX);
    g.fill(0, 52, 800, 296, kLensDark);
    const bool aActive = lsdj_ == lsdjA_;
    g.color(aActive ? 0xFFFFFF : 0x8C8C8C);                 // the console that is not controlled is dimmed
    lcd_.draw(g.display(), 0, 56, 2);
    g.color(aActive ? 0x8C8C8C : 0xFFFFFF);
    lcd_.draw(g.display(), 400, 56, 2, gb::Lcd::kRamB);
    g.text(50, 462, ui::kSmall, aActive ? 0xFFD84A : kLensLight, "A", ot::IDisplay::kOptCenterX);      // above F1 and F8: hold one to raise the dock
    g.text(750, 462, ui::kSmall, aActive ? kLensLight : 0xFFD84A, "B", ot::IDisplay::kOptCenterX);
    const bool want = aHeld_ || bHeld_ || volHeld_ >= 0;
    dock_ = want ? std::min(1.f, dock_ + 0.4f) : std::max(0.f, dock_ - 0.4f);
    if (dock_ > 0) drawStrip(g, 480 - int(136 * dock_));
}

// F1..F8 sit under the screen: song A, its level, the four tracks of the active song (mute, solo with Shift), song B's level, song B.
void GbScreen::drawStrip(ot::Gfx& g, int dockTop) {
    g.fill(0, dockTop, 800, 480 - dockTop, kLensDark);
    const int y = dockTop + 4, h = 96, w = 94;
    const bool aActive = lsdj_ == lsdjA_;
    for (int i = 0; i < 8; ++i) {
        const int x = 3 + i * 100;
        uint32_t fill = 0x5A5864;
        const char* top = "";
        char label[16] = "";
        float frac = -1;
        bool lit = false;
        if (i == 0 || i == 7) { const bool me = (i == 0) == aActive; fill = me ? 0xB8962C : 0x5A5864; top = i == 0 ? "SONG A" : "SONG B"; lit = me; }
        else if (i == 1 || i == 6) { top = i == 1 ? "VOL A" : "VOL B"; frac = float((i == 1 ? lsdjA_ : lsdjB_)->volume()) / 15.f; std::snprintf(label, sizeof label, "%X", (i == 1 ? lsdjA_ : lsdjB_)->volume()); }
        else {
            const int c = i - 2;
            const gb::Lsdj& l = *lsdj_;
            bool solo = !l.channelMuted(c);
            for (int o = 0; o < 4; ++o) if (o != c) solo = solo && l.channelMuted(o);
            std::snprintf(label, sizeof label, "TRK %d", c + 1 + (aActive ? 0 : 4));
            top = l.channelMuted(c) ? "MUTE" : solo ? "SOLO" : l.channelOn(c) ? "ON" : "-";
            fill = l.channelMuted(c) ? 0x3A3842 : solo ? 0x2F8F56 : 0x5A5864;
            lit = !l.channelMuted(c);
        }
        g.roundRect(x, y, w, h, 6, fill);
        g.text(x + w / 2, y + 8, ui::kText, lit ? 0xFFFFFF : kLensLight, top, ot::IDisplay::kOptCenterX);
        if (frac >= 0) g.hbar(x + 8, y + 44, w - 16, 14, frac, 0x7BE07B, kLensLight);
        g.text(x + w / 2, y + 66, ui::kSmall, kLensLight, label, ot::IDisplay::kOptCenterX);
        char fk[4] = {'F', char('1' + i), 0};
        g.text(x + w / 2, y + 78, ui::kSmall, 0x1A1820, fk, ot::IDisplay::kOptCenterX);
    }
}

// The Tracker's keys: arrows, Insert = A, Delete = B, Shift = Select, Play = Start; the module and editing keys jump straight to a page
// (Song, Pattern = Chain, Note = Phrase, Instr, Instr params = Table, Fx1 = Synth, Fx2 = Wave, Config = Project, File = songs, Sample loader = the
// instrument library, Performance = Groove, Help = help); Shift + Song swaps the console on screen; Rec toggles the Phrase page's Rec mode, and Play in
// Rec mode loops the phrase over the playing song and records the pads. Copy / Shift + Copy copy and paste selected cells. Master toggles the side by
// side view of the two linked consoles; F1 / F8 pick the console to control, F2 / F7 held + jog = level, F3-F6 mute (Shift: solo) a track.
bool GbScreen::button(uint8_t id, bool down) {
    if (!lsdj_) return false;
    using gb::Key;
    using gb::Scr;
    if (id <= ot::btn::F8) {
        if (id == 7 && !splitOn()) {                       // F8 outside the side by side view: hold it for an explanation of the page
            if (down && lsdj_->screen() != Scr::Help) { lsdj_->showPopup(gb::Lsdj::kPageHelp); popupKey_ = id; }
            else if (!down && popupKey_ == id) closePopup();
            return true;
        }
        if (id == 0 || id == 7) {
            (id == 0 ? aHeld_ : bHeld_) = down;
            if (down) lsdj_ = id == 0 ? lsdjA_ : lsdjB_;
        }
        else if (id == 1 || id == 6) volHeld_ = down ? (id == 1 ? 0 : 1) : -1;
        else if (down) { if (shift_) lsdj_->soloChannel(id - 2); else lsdj_->toggleMuteChannel(id - 2); }
        return true;
    }
    if (popupKey_ != 0xFF && (id == ot::btn::Left || id == ot::btn::Right || id == ot::btn::Up || id == ot::btn::Down)) {   // a pop-up is up: left / right turn its pages
        if (down && id == ot::btn::Left) lsdj_->popupTurn(-1);
        else if (down && id == ot::btn::Right) lsdj_->popupTurn(1);
        return true;
    }
    switch (id) {
        case ot::btn::Up: lsdj_->key(Key::Up, down); break;
        case ot::btn::Down: lsdj_->key(Key::Down, down); break;
        case ot::btn::Left: lsdj_->key(Key::Left, down); break;
        case ot::btn::Right: lsdj_->key(Key::Right, down); break;
        case ot::btn::Insert: lsdj_->key(Key::A, down); break;       // A
        case ot::btn::Delete: lsdj_->key(Key::B, down); break;       // B
        case ot::btn::Shift:
            shift_ = down;
            if (!down && (popupKey_ == ot::btn::Note || popupKey_ == ot::btn::InstrParams)) closePopup();
            lsdj_->key(Key::Select, down);
            break;                                                                      // Select
        case ot::btn::Play: lsdj_->key(Key::Start, down); break;
        case ot::btn::Rec: if (down) lsdj_->toggleRecMode(); break;      // Phrase page: Rec mode on/off
        case ot::btn::Copy: if (down) lsdj_->copyKey(shift_); break;
        case ot::btn::Help: if (down) lsdj_->openHelp(); break;
        case ot::btn::Master: if (down && lsdjA_->syncLinked()) split_ = !split_; break;
        case ot::btn::SampleLoader: if (down) lsdj_->openLibrary(); break;
        case ot::btn::Song: if (down) { if (shift_) swapConsole(); else lsdj_->jump(Scr::Song); } break;
        case ot::btn::Pattern: if (down) lsdj_->jump(Scr::Chain); break;
        case ot::btn::Note:                                                             // Shift + Note (held): the Phrase page's commands
            if (down && shift_) { lsdj_->showPopup(gb::Lsdj::kPhraseSheet); popupKey_ = id; }
            else if (down) lsdj_->jump(Scr::Phrase);
            else if (popupKey_ == id) closePopup();
            break;
        case ot::btn::Instr: if (down) lsdj_->jump(Scr::Instr); break;
        case ot::btn::InstrParams:                                                      // Shift + Instr params (held): the Table page's commands
            if (down && shift_) { lsdj_->showPopup(gb::Lsdj::kTableSheet); popupKey_ = id; }
            else if (down) lsdj_->jump(Scr::Table);
            else if (popupKey_ == id) closePopup();
            break;
        case ot::btn::Fx1: if (down) lsdj_->jump(Scr::Synth); break;
        case ot::btn::Fx2: if (down) lsdj_->jump(Scr::Wave); break;
        case ot::btn::Config: if (down) lsdj_->jump(Scr::Project); break;
        case ot::btn::File: if (down) lsdj_->openFiles(); break;
        case ot::btn::Performance: if (down) lsdj_->jump(Scr::Groove); break;
        default: return false;
    }
    return true;
}

void GbScreen::encoder(int delta) {
    if (!lsdj_) return;
    if (volHeld_ >= 0) { (volHeld_ ? lsdjB_ : lsdjA_)->adjustVolume(delta); return; }
    lsdj_->encoder(delta);
}
bool GbScreen::pad(int p, bool down, uint8_t) {
    if (!lsdj_) return true;
    if (shift_) { if (down) lsdj_->setPadOctave(p / ot::kPadCols - 1); return true; }    // Shift + a pad row picks the octave: -1 .. +2
    if (down) lsdj_->padDown(p); else lsdj_->padUp(p);
    return true;
}

}  // namespace ui
}  // namespace atw
