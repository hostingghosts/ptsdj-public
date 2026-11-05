// Atari tracker -- colours and drawing helpers shared by the screens (screen.h).
#include "atw/ui/screen.h"

namespace atw {
namespace ui {

namespace col {
// Warm black panels, Atari orange for the cursor and the logo, Atari blue for the second logo word and instruments.
uint32_t kBg = 0x0F0C0A, kPanel = 0x1D1916, kPanelHi = 0x322A24, kRowHi = 0x261F1A, kText = 0xF0E6D8, kDim = 0x7A6C5E, kFaint = 0x3E352D,
         kCursor = 0xD2541E, kEdit = 0xD83A2B, kPlayRow = 0x3A2A14, kNote = 0xF8F0E0, kInst = 0x6FC3E8, kCmd = 0xF2C230, kAccent1 = 0xF08A24,
         kAccent2 = 0x4FA3D8, kWarn = 0xF2C230, kBad = 0xFF5A4A, kGood = 0x6FD08C, kOnCursor = 0xFFFFFF;
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
