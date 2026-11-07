// A 160 x 144 four-shade screen like the Game Boy's, with an 8 x 8 tile font. The LSDJ-style screens draw into it; present()
// sends it to the FT81x as one RGB332 bitmap magnified 3x (a tiny display list, whatever is on screen).
#pragma once

#include <cstdint>

#include "hal_api.h"

namespace gb {

class Lcd {
public:
    static constexpr int kW = 200, kH = 144, kCols = 25, kRows = 18;   // a little wider than the Game Boy's 160 so the pages have room
    // Shades 0 (lightest) .. 3 (darkest). Looks: the DMG greens, the Pocket's greys, amber, ice.
    static constexpr int kLooks = 4;
    static constexpr uint32_t kPalettes[kLooks][4] = {{0x9BBC0F, 0x71A00F, 0x306230, 0x0F380F}, {0xD8D8D0, 0xA0A09C, 0x585858, 0x181818},
                                                      {0xF8D878, 0xC09838, 0x784818, 0x201008}, {0xD0F0F8, 0x78B8D8, 0x305880, 0x081830}};
    static const char* lookName(int i) { static const char* const n[kLooks] = {"GREEN", "GRAY", "AMBER", "ICE"}; return n[i & 3]; }
    void setLook(int i) { look_ = i & 3; }
    int look() const { return look_; }

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
    void draw(ot::IDisplay& d, int x = 100, int y = 24, int scale = 3);
    void setRam(uint32_t addr) { ram_ = addr; }   // RAM_G slot of this display (two displays side by side need two)
    // A glyph scaled up (the splash's block letters).
    void bigGlyph(int x, int y, char c, int scale, uint8_t shade);

private:
    uint8_t p_[kH][kW] = {};
    uint8_t rgb332_[kW * kH];
    bool uploaded_ = false;
    int look_ = 0;
    uint32_t ram_ = 384000;   // the frame buffer slot the game screens of the stock firmware use
};

}  // namespace gb
