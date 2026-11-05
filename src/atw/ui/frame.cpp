// GB tracker -- colours and drawing helpers shared by the screens (screen.h).
#include "atw/ui/screen.h"

namespace atw {
namespace ui {

namespace col {
// Game Boy greens (the DMG palette: 0F380F, 306230, 8BAC0F, 9BBC0F) and a few signal colours.
uint32_t kBg = 0x0F380F, kPanel = 0x1B4A1B, kPanelHi = 0x306230, kRowHi = 0x153F15, kText = 0x9BBC0F, kDim = 0x6E8C14, kFaint = 0x2A5A2A,
         kCursor = 0x8BAC0F, kEdit = 0xD83A2B, kPlayRow = 0x24501E, kNote = 0xB4D21A, kInst = 0x8BAC0F, kCmd = 0xB4D21A, kAccent1 = 0x9BBC0F,
         kAccent2 = 0x6E8C14, kWarn = 0xE0D040, kBad = 0xE05040, kGood = 0x9BBC0F, kOnCursor = 0x0F380F;
}  // namespace col

char hexDigit(int v) { return "0123456789ABCDEF"[v & 15]; }

void hex2(char* out, uint8_t v) {
    out[0] = hexDigit(v >> 4);
    out[1] = hexDigit(v);
}

void listRow(ot::Gfx& g, int x, int y, int w, const char* label, const char* value, bool selected, uint32_t valueColour) {
    g.fill(x, y - 2, w, 22, selected ? col::kPanelHi : col::kPanel);
    if (selected) g.fill(x, y - 2, 3, 22, col::kCursor);
    g.text(x + 10, y + 1, kMono, selected ? col::kText : col::kDim, label);
    g.text(x + w - 8, y + 1, kMono, valueColour, value, ot::IDisplay::kOptRightX);
}

void meter(ot::Gfx& g, int x, int y, int w, int h, float frac, uint32_t c) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    g.fill(x, y, w, h, col::kFaint);
    const int f = int(float(w) * frac);
    if (f > 0) g.fill(x, y, f, h, c);
}

}  // namespace ui
}  // namespace atw
