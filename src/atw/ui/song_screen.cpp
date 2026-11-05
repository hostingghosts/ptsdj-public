#include "atw/ui/song_screen.h"

#include <cstdio>

#include "atw/app.h"
#include "ui/buttons.h"

namespace atw {
namespace ui {

namespace {
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}

// One BPM step is one video frame per row, so the nearest tempo the Atari can play is the nearest whole speed.
void SongScreen::setSpeed(int speed) {
    Ui& u = ui();
    Song& s = u.song();
    s.speed = uint8_t(clampi(speed, 1, 255));
    u.app().engine.setLiveSpeed(s.speed);    // also while playing
    edited();
}

void SongScreen::tap() {
    Ui& u = ui();
    const uint32_t now = u.now();
    if (tapCount_ > 0 && now - taps_[tapCount_ - 1] > 2000) tapCount_ = 0;   // a pause starts a new count
    if (tapCount_ == 5) {
        for (int i = 1; i < 5; ++i) taps_[i - 1] = taps_[i];
        tapCount_ = 4;
    }
    taps_[tapCount_++] = now;
    if (tapCount_ < 2) {
        u.status("Tap tempo: keep tapping", 1500);
        return;
    }
    const double beatMs = double(taps_[tapCount_ - 1] - taps_[0]) / (tapCount_ - 1);   // one tap = one beat = four quarter beats
    const double bpm = 60000.0 / beatMs;
    int best = 1;
    double bestErr = 1e9;
    for (int sp = 1; sp <= 255; ++sp) {
        const double e = bpmOfPair(sp, u.song().funk, u.song().region, u.song().engineSpeed) - bpm;
        if ((e < 0 ? -e : e) < bestErr) {
            bestErr = e < 0 ? -e : e;
            best = sp;
        }
    }
    setSpeed(best);
    char buf[40];
    std::snprintf(buf, sizeof buf, "Tapped %d BPM", bpmOfPair(best, u.song().funk, u.song().region, u.song().engineSpeed));
    u.status(buf, 1500);
}

// FUNK -/+. Plain: the odd rows get one frame shorter / longer. With "FUNK KEEPS TEMPO" (Config) the even rows take the opposite step, so a pair of rows lasts
// the same: funk 0 (off) counts as funk = speed, and a pair that comes out even again is off.
void SongScreen::nudgeFunk(int d) {
    Ui& u = ui();
    Song& s = u.song();
    if (!u.funkKeepsTempo) {
        if (d < 0 && s.funk > 0) --s.funk;
        else if (d > 0 && s.funk < 255) ++s.funk;
        else return;
        edited();
        u.status(s.funk ? "Funk: odd rows are shorter or longer" : "Funk off", 1200);
        return;
    }
    const int funk = s.funk ? s.funk : s.speed;
    const int newFunk = funk + d, newSpeed = s.speed - d;
    if (newFunk < 1 || newSpeed < 1 || newFunk > 255 || newSpeed > 255) {
        u.status("No room: a row would be 0 frames or more than 255", 1600);
        return;
    }
    s.speed = uint8_t(newSpeed);
    s.funk = newFunk == newSpeed ? 0 : uint8_t(newFunk);
    u.app().engine.setLiveSpeed(s.speed);
    edited();
    char m[64];
    if (s.funk) std::snprintf(m, sizeof m, "Even rows %d, odd rows %d frames: the tempo stays", s.speed, s.funk);
    else std::snprintf(m, sizeof m, "Straight: every row %d frames", s.speed);
    u.status(m, 1600);
}

void SongScreen::edited() { ui().app().markDirty(); }

void SongScreen::labels(const char* out[8]) {
    static char bpm[16];
    std::snprintf(bpm, sizeof bpm, "TAP %d", bpmOfPair(ui().song().speed, ui().song().funk, ui().song().region, ui().song().engineSpeed));
    out[0] = "RESTART";
    out[1] = "EDIT PAT";
    out[2] = "BPM -";
    out[3] = "BPM +";
    out[4] = "FUNK -";
    out[5] = "FUNK +";
    out[6] = "TUNE";
    out[7] = bpm;   // tap here to set the tempo
}

// Highest pattern a track cell offers: the ones that exist plus the next, empty one (to start a new pattern from the grid).
static int maxPattern(const Song& s) {
    int n = 1;                                   // the last pattern with notes in it, and one more (the order list's own references do not count)
    for (int i = 0; i < kPatternCount; ++i) if (!s.pattern(i).empty()) n = i + 1;
    return clampi(n, 0, kPatternCount - 1);
}

void SongScreen::encoder(int d) {
    Ui& u = ui();
    Song& s = u.song();
    OrderEntry& e = s.order[u.orderPos];
    if (field_ == kFieldPattern) {
        e.pattern = uint8_t(clampi(e.pattern + d, 0, s.patternsCreated() - 1));   // only patterns that exist
    } else if (field_ == kFieldRepeat) {
        e.repeat = uint8_t(clampi(e.repeat + d, 1, 255));
    } else {                                     // a track's cell: out, the entry's pattern, then pattern 0, 1, 2 ...
        uint8_t& c = e.cell[field_ - kFieldTrack];
        const int at = c == OrderEntry::kOut ? 0 : (c == OrderEntry::kSame ? 1 : 2 + c);
        const int to = clampi(at + d, 0, 2 + maxPattern(s));
        c = to == 0 ? OrderEntry::kOut : (to == 1 ? OrderEntry::kSame : uint8_t(to - 2));
    }
    edited();
}

bool SongScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    Ui& u = ui();
    Song& s = u.song();
    switch (id) {
        case b::Up: if (u.orderPos > 0) --u.orderPos; return true;
        case b::Down: if (u.orderPos + 1 < s.orderLen) ++u.orderPos; return true;
        case b::Left: if (field_ > 0) --field_; return true;
        case b::Right: if (field_ < kFields - 1) ++field_; return true;
        case b::EncPush:                             // on a track cell: take the track out of this row / put it back
            if (field_ >= kFieldTrack) {
                uint8_t& c = s.order[u.orderPos].cell[field_ - kFieldTrack];
                c = c == OrderEntry::kOut ? OrderEntry::kSame : OrderEntry::kOut;
                edited();
                u.status(c == OrderEntry::kOut ? "Track out of this row (its notes stay in the pattern)" : "Track back in", 1600);
                return true;
            }
            return false;
        case b::Insert:
            if (s.orderLen < kOrderLen) {
                for (int i = s.orderLen; i > u.orderPos; --i) s.order[i] = s.order[i - 1];
                s.orderLen++;
                s.order[u.orderPos + 1].tune = 0;                 // the copy keeps no tune mark
                edited();
            }
            return true;
        case b::Delete:
            if (s.orderLen > 1) {
                for (int i = u.orderPos; i + 1 < s.orderLen; ++i) s.order[i] = s.order[i + 1];
                s.orderLen--;
                if (u.orderPos >= s.orderLen) u.orderPos = uint8_t(s.orderLen - 1);
                if (s.restartPos >= s.orderLen) s.restartPos = 0;
                edited();
            }
            return true;
        case b::F1: s.restartPos = u.orderPos; edited(); u.status("Restart position set"); return true;
        case b::F2: {                                // the pattern under the cursor: a track cell's own pattern, else the entry's
            const OrderEntry& e = s.order[u.orderPos];
            const int p = field_ >= kFieldTrack ? e.trackPattern(field_ - kFieldTrack) : -1;
            u.pattern = uint8_t(p >= 0 ? p : e.pattern);
            if (field_ >= kFieldTrack) u.channel = field_ - kFieldTrack;
            u.show(ScreenId::kPattern);
            return true;
        }
        case b::F3: setSpeed(s.speed + 1); return true;   // slower: a longer row
        case b::F4: setSpeed(s.speed - 1); return true;
        case b::F5: nudgeFunk(-1); return true;
        case b::F6: nudgeFunk(1); return true;
        case b::F7:                                  // a tune of its own starts at this entry (entry 0 always starts tune 0)
            if (u.orderPos > 0) {
                s.order[u.orderPos].tune = s.order[u.orderPos].tune ? 0 : 1;
                edited();
                u.status(s.order[u.orderPos].tune ? "TUNE marked: exported games can start the song from here by number" : "Tune mark removed", 2200);
            } else {
                u.status("Entry 0 is always tune 0. Mark a later entry to start another tune (games only)", 2200);
            }
            return true;
        case b::F8: tap(); return true;
        default: return false;
    }
}

void SongScreen::draw(ot::Gfx& g) {
    Ui& u = ui();
    const Song& s = u.song();
    const AudioEngine& e = u.app().engine;
    char buf[128];
    g.text(24, kContentY + 6, kMono, col::kDim, "POS  PAT  REP    T1  T2  T3  T4");
    const int rows = 16;
    const int top = u.orderPos >= rows ? u.orderPos - rows + 1 : 0;
    for (int i = 0; i < rows && top + i < s.orderLen; ++i) {
        const int idx = top + i;
        const OrderEntry& oe = s.order[idx];
        const int y = kContentY + 30 + i * 22;
        const bool cur = idx == u.orderPos, play = e.playing() && e.orderPos() == idx;
        g.fill(16, y - 2, 456, 20, cur ? col::kPanelHi : (play ? col::kPlayRow : col::kPanel));
        std::snprintf(buf, sizeof buf, "%02X%c", unsigned(idx), idx == s.restartPos ? '>' : ' ');
        g.text(24, y + 1, kMono, play ? col::kGood : col::kText, buf);
        std::snprintf(buf, sizeof buf, "%02X", unsigned(oe.pattern));
        g.text(80, y + 1, kMono, cur && field_ == kFieldPattern ? col::kEdit : col::kNote, buf);
        std::snprintf(buf, sizeof buf, "x%u", unsigned(oe.repeat));
        g.text(128, y + 1, kMono, cur && field_ == kFieldRepeat ? col::kEdit : col::kText, buf);
        for (int t = 0; t < kTracks; ++t) {       // a track cell: its pattern (dim when it just follows the entry) or -- when taken out
            const int p = oe.trackPattern(t);
            const bool sel = cur && field_ == kFieldTrack + t;
            if (p < 0) std::snprintf(buf, sizeof buf, "--");
            else std::snprintf(buf, sizeof buf, "%02X", unsigned(p));
            const uint32_t colr = sel ? col::kEdit : (p < 0 ? col::kBad : (oe.cell[t] == OrderEntry::kSame ? col::kDim : col::kNote));
            g.text(192 + t * 56, y + 1, kMono, colr, buf);
        }
        if (idx == 0 || oe.tune) {
            int n = 0;
            for (int k = 1; k <= idx; ++k) n += s.order[k].tune ? 1 : 0;
            std::snprintf(buf, sizeof buf, "T%d", n);
            g.text(430, y + 1, kMono, col::kAccent2, buf);
        }
    }
    char funk[24];
    if (s.funk) std::snprintf(funk, sizeof funk, "funk %u", unsigned(s.funk)); else std::snprintf(funk, sizeof funk, "funk off");
    std::snprintf(buf, sizeof buf, "%u entries  restart %02X  %d BPM (%u frames/row)  %s", unsigned(s.orderLen), unsigned(s.restartPos),
                  bpmOfPair(s.speed, s.funk, s.region, s.engineSpeed), unsigned(s.speed), funk);
    g.text(16, kBarY - 30, kMono, col::kDim, buf);
    const char* help[] = {"HOW THE SONG PAGE WORKS", "", "Up/Down: row   Left/Right: field", "jog: change the field",
                          "PAT = the pattern all tracks play", "T1-T4 = one track's pattern; dim = same", "as PAT, -- = track taken out (its", "notes stay in the pattern).",
                          "Jog push on a track: out / back in", "A shorter pattern loops under a", "longer one in the same row.", "",
                          "F1 restart   F2 open the pattern", "F3/F4 slower/faster   F8 tap", "F7 TUNE mark (games only)"};
    for (int i = 0; i < int(sizeof help / sizeof *help); ++i) g.text(486, kContentY + 6 + i * 20, kMono, i == 0 ? col::kText : col::kDim, help[i]);
}

}  // namespace ui
}  // namespace atw
