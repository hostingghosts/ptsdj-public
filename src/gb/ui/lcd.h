// A 160 x 144 four-shade screen like the Game Boy's, with an 8 x 8 tile font. The LSDJ-style screens draw into it; present()
// sends it to the FT81x as one RGB332 bitmap magnified 3x (a tiny display list, whatever is on screen).
#pragma once

#include <cstdint>

#include "hal_api.h"

namespace gb {

class Lcd {
public:
    static constexpr int kW = 160, kH = 144, kCols = 20, kRows = 18;
    // Shades 0 (lightest) .. 3 (darkest), the DMG greens.
    static constexpr uint32_t kPalette[4] = {0x9BBC0F, 0x71A00F, 0x306230, 0x0F380F};

    void clear(uint8_t shade = 0);
    void px(int x, int y, uint8_t s) { if (unsigned(x) < kW && unsigned(y) < kH) p_[y][x] = s; }
    uint8_t at(int x, int y) const { return p_[y][x]; }
    void rect(int x, int y, int w, int h, uint8_t s);
    void frame(int x, int y, int w, int h, uint8_t s);
    // Tile text: (col,row) in 8 px cells. inv = light glyph on a dark cell. Returns the column after the text.
    int text(int col, int row, const char* s, bool inv = false);
    void glyph(int col, int row, char c, bool inv = false);
    void glyphAt(int x, int y, char c, bool inv = false, uint8_t fg = 3, uint8_t bg = 0);   // pixel position, any shades
    void hex2(int col, int row, unsigned v, bool inv = false);
    // Upload and draw (call prepare() before the display list starts, draw() inside it).
    void prepare(ot::IDisplay& d);
    void draw(ot::IDisplay& d, int x = 160, int y = 24);

private:
    uint8_t p_[kH][kW] = {};
    uint8_t rgb332_[kW * kH];
    bool uploaded_ = false;
};

}  // namespace gb
