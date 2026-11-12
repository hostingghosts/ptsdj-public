#include "gb/ui/mixpage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gb {

namespace {
// A bar: a frame of the dark shade with the fraction filled in.
void bar(Lcd& l, int x, int y, int w, float f) {
    l.frame(x, y, w, 6, 3);
    l.rect(x + 1, y + 1, w - 2, 4, 0);
    const int fw = int(std::clamp(f, 0.f, 1.f) * float(w - 2) + 0.5f);
    if (fw > 0) l.rect(x + 1, y + 1, fw, 4, 3);
}
}  // namespace

// Rows 4 to 12 are the settings in signal order, the limiter and then the master volume; the two meters sit under them.
void drawMixPage(Lcd& l, const MixView& v) {
    l.clear(0);
    l.text(0, 0, "MASTER MIX");
    l.text(0, 2, "LIMITER > MASTER VOL");
    static const char* const name[kMixRows] = {"LIMITER", "DRIVE", "CEILING", "RELEASE", "MASTER"};
    static const int rowOf[kMixRows] = {4, 6, 8, 10, 12};
    char val[kMixRows][24];
    std::snprintf(val[0], sizeof val[0], "%s", v.m.on ? "ON" : "OFF");
    std::snprintf(val[1], sizeof val[1], "+%d.%dDB", v.m.driveHalfDb / 2, v.m.driveHalfDb % 2 * 5);
    std::snprintf(val[2], sizeof val[2], "-%d.%dDB", -v.m.ceilHalfDb / 2, -v.m.ceilHalfDb % 2 * 5);
    std::snprintf(val[3], sizeof val[3], "%dMS", v.m.releaseMs);
    const int tenths = (v.masterMax - v.master) * 15;
    if (v.master == 0) std::snprintf(val[4], sizeof val[4], "MUTE");
    else std::snprintf(val[4], sizeof val[4], "%s%d.%dDB", tenths ? "-" : "", tenths / 10, tenths % 10);
    const float frac[kMixRows] = {v.m.on ? 1.f : 0.f, float(v.m.driveHalfDb) / 36.f, float(v.m.ceilHalfDb + 24) / 23.f, float(v.m.releaseMs - 25) / 475.f,
                                  float(v.master) / float(v.masterMax)};
    for (int i = 0; i < kMixRows; ++i) {
        const int r = rowOf[i];
        const bool sel = i == v.row;
        if (sel) l.glyph(0, r, '>');
        l.text(1, r, name[i]);
        l.text(9, r, val[i], sel);                      // the value under the cursor is inverted, as on the other pages
        bar(l, 144, r * 8 + 1, 48, frac[i]);
    }
    char t[24];
    l.text(1, 14, "REDUC");
    bar(l, 56, 14 * 8 + 1, 80, -v.reductionDb / 12.f);
    std::snprintf(t, sizeof t, "%.1fDB", v.reductionDb > -0.05f ? 0.0 : double(v.reductionDb));
    l.text(18, 14, t);
    l.text(1, 16, "OUT");
    bar(l, 56, 16 * 8 + 1, 80, (v.outDb + 48.f) / 48.f);
    if (v.outDb <= -90.f) std::snprintf(t, sizeof t, "-INF");
    else std::snprintf(t, sizeof t, "%.1fDB", double(v.outDb));
    l.text(18, 16, t);
}

}  // namespace gb
