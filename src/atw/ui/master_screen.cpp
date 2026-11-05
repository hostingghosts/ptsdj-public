#include "atw/ui/master_screen.h"

#include <cstdio>

#include "atw/app.h"
#include "ui/buttons.h"

namespace atw {
namespace ui {

void MasterScreen::labels(const char* out[8]) {
    out[0] = "TRACK 1";
    out[1] = "TRACK 2";
    out[2] = "TRACK 3";
    out[3] = "TRACK 4";
}

bool MasterScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    if (id == b::Up) { encoder(5); return true; }
    if (id == b::Down) { encoder(-5); return true; }
    return false;
}

void MasterScreen::encoder(int d) {
    Ui& u = ui();
    for (int t = 0; t < kTracks; ++t)
        if (u.fHeld(t)) {                       // a bottom button held: the jog sets that track's volume
            const int v = int(u.song().trackVolume[t]) + d * 2;
            u.song().trackVolume[t] = uint8_t(v < 0 ? 0 : (v > 100 ? 100 : v));
            u.app().markDirty();
            return;
        }
    const int v = int(u.masterVolume) + d * 2;
    u.masterVolume = uint8_t(v < 0 ? 0 : (v > 100 ? 100 : v));
    u.app().engine.setMasterVolume(u.masterVolume);
}

void MasterScreen::draw(ot::Gfx& g) {
    Ui& u = ui();
    AudioEngine& e = u.app().engine;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%u %%", unsigned(u.masterVolume));
    listRow(g, 16, kContentY + 10, 480, "MASTER VOLUME", buf, true);

    const int top = kContentY + 60, h = 200, w = 70;
    for (int t = 0; t < 4; ++t) {
        const int x = t * 100 + 3 + (94 - w) / 2;           // over the button that sets it (F1..F4)
        const bool has = u.song().trackUsed(t);
        const int now = has ? e.trackLevel(t) : 0;
        if (now >= lvl_[t]) lvl_[t] = now;
        else if (lvl_[t] > 0 && (u.frames() & 3) == 0) --lvl_[t];
        g.fill(x, top, w, h, col::kPanel);
        const int bar = lvl_[t] * h / 15;
        if (bar > 0) g.fill(x, top + h - bar, w, bar, e.muted(t) || !has ? col::kDim : (lvl_[t] > 13 ? col::kWarn : col::kGood));
        std::snprintf(buf, sizeof buf, "TRACK %d", t + 1);
        g.text(x, top + h + 8, kMono, has ? col::kAccent1 : col::kDim, buf);
        std::snprintf(buf, sizeof buf, "%u %%", unsigned(u.song().trackVolume[t]));
        g.text(x, top + h + 30, kMono, u.fHeld(t) ? col::kEdit : col::kText, buf);
        std::snprintf(buf, sizeof buf, "%X", unsigned(lvl_[t]));
        g.text(x + w / 2 - 4, top - 18, kMono, col::kText, buf);
    }
    // output peak, held for a second
    const int p = e.takePeak();
    if (p >= peak_ || u.now() - peakAt_ > 1000) {
        peak_ = p;
        peakAt_ = u.now();
    }
    const int px = 440, pw = 40;
    g.fill(px, top, pw, h, col::kPanel);
    const int pb = peak_ * h / 32767;
    if (pb > 0) g.fill(px, top + h - pb, pw, pb, peak_ > 30000 ? col::kBad : col::kGood);
    g.text(px - 4, top + h + 8, kMono, col::kText, "OUTPUT");
    std::snprintf(buf, sizeof buf, "%s  %s", u.song().chip == atari::Chip::Pokey ? "POKEY" : "TIA", u.song().region == atari::Region::NTSC ? "NTSC" : "PAL");
    g.text(560, top, kMono, col::kDim, buf);
    g.text(560, top + 24, kMono, col::kDim, "jog: master volume");
    g.text(560, top + 44, kMono, col::kDim, "hold F1-F4 + jog:");
    g.text(560, top + 64, kMono, col::kDim, "that track's volume");
}

}  // namespace ui
}  // namespace atw
