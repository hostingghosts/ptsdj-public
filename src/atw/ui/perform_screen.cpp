#include "atw/ui/perform_screen.h"

#include <cstdio>

#include "atw/app.h"
#include "ui/buttons.h"

namespace atw {
namespace ui {

void PerformScreen::labels(const char* out[8]) {
    const AudioEngine& e = ui().app().engine;
    const bool solo = ui().shift;                        // F1-F4: mute; hold Shift for solo
    for (int t = 0; t < 8; ++t) out[t] = nullptr;
    for (int t = 0; t < 4; ++t) {
        if (!ui().song().trackUsed(t)) continue;
        if (solo) std::snprintf(lab_[t], sizeof lab_[t], "%s %u", e.solo(t) ? "UNSOLO" : "SOLO", unsigned(t + 1) % 10);
        else std::snprintf(lab_[t], sizeof lab_[t], "%s %u", e.muted(t) ? "UNMUTE" : "MUTE", unsigned(t + 1) % 10);
        out[t] = lab_[t];
    }
    out[4] = "PAT -";
    out[5] = "PAT +";
    if (e.playing() && e.queuedPattern() >= 0) {
        std::snprintf(lab_[4], sizeof lab_[4], "NEXT %02X", unsigned(e.queuedPattern()));
        out[6] = lab_[4];                                // tap: cancel
    }
}

void PerformScreen::setVolume(int t, int v) {
    Ui& u = ui();
    v = v < 0 ? 0 : (v > 15 ? 15 : v);
    if (v == vol_[t]) return;
    vol_[t] = uint8_t(v);
    AudioEngine& e = u.app().engine;
    e.setTrackVolume(t, uint8_t(v));
    if (!u.edit || !e.playing() || e.sequencerMode() != Sequencer::Mode::kSong) return;   // recording: only while the whole song plays
    Pattern& p = u.song().pattern(e.pattern());
    const int row = e.row();
    if (row < 0 || row >= p.rows) return;
    Cell& c = p.cells[row][t];
    Effect* fx = c.fx[0].type == atari::FX_VOLUME || !c.fx[0].type ? &c.fx[0] : (c.fx[1].type == atari::FX_VOLUME || !c.fx[1].type ? &c.fx[1] : nullptr);
    if (!fx) return;                                    // both effect slots taken by other effects
    *fx = {atari::FX_VOLUME, uint8_t(v)};
    u.app().markDirty();
}

void PerformScreen::encoder(int d) { setVolume(track_, int(vol_[track_]) + d); }

bool PerformScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    Ui& u = ui();
    AudioEngine& e = u.app().engine;
    const Song& sg = u.song();
    if (!sg.trackUsed(track_)) track_ = sg.slotTrack(0);
    auto next = [&](int d) {
        for (int i = 0; i < 4; ++i) {
            track_ = (track_ + d + 4) % 4;
            if (sg.trackUsed(track_)) return;
        }
    };
    switch (id) {
        case b::Left: next(-1); return true;
        case b::Right: next(1); return true;
        case b::Up: encoder(1); return true;
        case b::Down: encoder(-1); return true;
        case b::F1: case b::F2: case b::F3: case b::F4: {
            const int t = id - b::F1;
            if (!sg.trackUsed(t)) return true;
            if (u.shift) e.setSolo(t, !e.solo(t));
            else e.setMuted(t, !e.muted(t));
            return true;
        }
        case b::F5: u.changePattern(-1); return true;       // while playing: starts at the top when the pattern has ended
        case b::F6: u.changePattern(1); return true;
        case b::F7:
            if (e.playing() && e.queuedPattern() >= 0) { e.queuePattern(-1); u.status("Pattern change cancelled", 1200); }
            return true;
        default: return false;
    }
}

void PerformScreen::draw(ot::Gfx& g) {
    Ui& u = ui();
    AudioEngine& e = u.app().engine;
    if (!u.song().trackUsed(track_)) track_ = u.song().slotTrack(0);
    char buf[40];
    const int top = kContentY + 52, h = 200, w = 120, gap = 40;
    g.text(16, kContentY + 6, kMono, u.edit && e.playing() ? col::kEdit : col::kDim,
           u.edit ? "REC: volume changes are written as V effects while the song plays" : "Rec on: volume changes are written as V effects while the song plays");
    const int shownPattern = e.playing() ? e.pattern() : u.pattern;
    if (e.playing() && e.queuedPattern() >= 0) std::snprintf(buf, sizeof buf, "PATTERN %02X  NEXT %02X", unsigned(shownPattern), unsigned(e.queuedPattern()));
    else std::snprintf(buf, sizeof buf, "PATTERN %02X", unsigned(shownPattern));
    g.text(16, kContentY + 22, kMono, col::kAccent2, buf);
    g.text(240, kContentY + 22, kMono, col::kDim, "F1-F4 mute (Shift: solo)  F5/F6 pattern -/+");
    for (int t = 0; t < 4; ++t) {
        const int x = 40 + t * (w + gap);
        const bool has = u.song().trackUsed(t);
        g.fill(x, top, w, h, has && t == track_ ? col::kPanelHi : col::kPanel);
        if (!has) continue;
        const int lvl = e.trackLevel(t);
        const int bar = vol_[t] * h / 15;
        g.fill(x + 8, top + h - bar, 24, bar, col::kInst);                   // the fader: what the note is set to
        const int meter = lvl * h / 15;
        if (meter > 0) g.fill(x + w - 32, top + h - meter, 24, meter, e.muted(t) ? col::kDim : col::kGood);   // the meter: what comes out
        std::snprintf(buf, sizeof buf, "TRACK %d", t + 1);
        g.text(x, top + h + 8, kMono, t == track_ ? col::kText : col::kAccent1, buf);
        std::snprintf(buf, sizeof buf, "V%X", unsigned(vol_[t]));
        g.text(x + 8, top + 8, kMono, col::kText, buf);
        if (e.muted(t)) g.text(x + 8, top + h + 30, kMono, col::kWarn, "MUTE");
        if (e.solo(t)) g.text(x + 64, top + h + 30, kMono, col::kGood, "SOLO");
    }
}

}  // namespace ui
}  // namespace atw
