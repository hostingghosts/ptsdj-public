#include "gb/ui/lcd.h"

#include <cstring>

namespace gb {

namespace {
#include "gb/ui/font.inc"

uint8_t rgb332(uint32_t c) {
    const int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    return uint8_t(((r * 7 + 127) / 255) << 5 | ((g * 7 + 127) / 255) << 2 | ((b * 3 + 127) / 255));
}
const uint8_t* glyphOf(char c) {
    if (c >= 'a' && c <= 'z') c = char(c - 32);
    for (size_t i = 0; i < sizeof kChars; ++i)
        if (kChars[i] == c) return kGlyphs[i];
    return kGlyphs[0];
}
}  // namespace

constexpr uint32_t Lcd::kPalettes[Lcd::kLooks][4];

void Lcd::clear(uint8_t s) { std::memset(p_, s, sizeof p_); }

void Lcd::rect(int x, int y, int w, int h, uint8_t s) {
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) px(i, j, s);
}

void Lcd::frame(int x, int y, int w, int h, uint8_t s) {
    rect(x, y, w, 1, s); rect(x, y + h - 1, w, 1, s); rect(x, y, 1, h, s); rect(x + w - 1, y, 1, h, s);
}

void Lcd::glyphAt(int x, int y, char c, bool inv, uint8_t fg, uint8_t bg) {
    const uint8_t* g = glyphOf(c);
    for (int j = 0; j < 8; ++j)
        for (int i = 0; i < 8; ++i) {
            const bool on = (g[j] >> (7 - i)) & 1;
            px(x + i, y + j, (on != inv) ? fg : bg);
        }
}

void Lcd::glyph(int col, int row, char c, bool inv) { glyphAt(col * 8, row * 8, c, inv, 3, 0); }

int Lcd::text(int col, int row, const char* s, bool inv) {
    for (; *s; ++s) glyph(col++, row, *s, inv);
    return col;
}

int Lcd::boldText(int col, int row, const char* s) {
    for (; *s; ++s, ++col) {
        glyph(col, row, *s);
        const uint8_t* g = glyphOf(*s);
        for (int j = 0; j < 8; ++j)
            for (int i = 0; i < 8; ++i)
                if ((g[j] >> (7 - i)) & 1) px(col * 8 + i + 1, row * 8 + j, 3);
    }
    return col;
}

void Lcd::hex2(int col, int row, unsigned v, bool inv) {
    char b[3] = {"0123456789ABCDEF"[(v >> 4) & 15], "0123456789ABCDEF"[v & 15], 0};
    text(col, row, b, inv);
}

void Lcd::prepare(ot::IDisplay& d, uint32_t ram) {
    // The SPI upload (28.8 KB) is the costly part of a frame: skip it when this slot already holds exactly these pixels in this look.
    const int slot = ram == kRamA ? 0 : 1;
    uint32_t h = 2166136261u ^ uint32_t(look_);
    const uint32_t* w = reinterpret_cast<const uint32_t*>(p_);
    for (size_t i = 0; i < sizeof p_ / 4; ++i) h = (h ^ w[i]) * 16777619u;
    if (uploaded_ && slotHash_[slot] == h && slotValid_[slot]) return;
    uint8_t lut[4];
    for (int i = 0; i < 4; ++i) lut[i] = rgb332(kPalettes[look_][i]);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) rgb332_[y * kW + x] = lut[p_[y][x] & 3];
    uploaded_ = d.writeRamG(ram, rgb332_, sizeof rgb332_);
    slotHash_[slot] = h;
    slotValid_[slot] = uploaded_;
}

void Lcd::draw(ot::IDisplay& d, int x, int y, int scale, uint32_t ram) {
    if (uploaded_) d.drawRgb332(int16_t(x), int16_t(y), ram, kW, kH, uint8_t(scale));
}

void Lcd::bigGlyph(int x, int y, char c, int scale, uint8_t shade) {
    const uint8_t* g = glyphOf(c);
    for (int j = 0; j < 8; ++j)
        for (int i = 0; i < 8; ++i)
            if ((g[j] >> (7 - i)) & 1) rect(x + i * scale, y + j * scale, scale, scale, shade);
}

namespace {
// The power-on logo font: heavy letters with two-pixel strokes and clipped corners, in the manner of the Game Boy's boot screen.
struct LogoGlyph { char c; int w; const char* row[8]; };
const LogoGlyph kLogo[] = {
    {'P', 7, {"######.", "###..##", "##...##", "###..##", "######.", "##.....", "##.....", "##....."}},
    {'T', 6, {"######", "######", "..##..", "..##..", "..##..", "..##..", "..##..", "..##.."}},
    {'S', 7, {".######", "###..##", "###....", ".#####.", "....###", "##...##", "###..##", "######."}},
    {'D', 7, {"#####..", "###.###", "###..##", "###..##", "###..##", "###..##", "###.###", "#####.."}},
    {'J', 6, {"...###", "...###", "...###", "...###", "...###", "##.###", "##.###", ".####."}},
};
const LogoGlyph* logoOf(char c) { for (const LogoGlyph& g : kLogo) if (g.c == c) return &g; return nullptr; }
}  // namespace

int Lcd::logoWidth(const char* s, int scale) {
    int w = 0;
    for (; *s; ++s) { const LogoGlyph* g = logoOf(*s); w += ((g ? g->w : 4) + 1) * scale; }
    return w > 0 ? w - scale : 0;
}

int Lcd::logoGlyph(int x, int y, char c, int scale, uint8_t shade) {
    const LogoGlyph* g = logoOf(c);
    if (!g) return x + 5 * scale;
    for (int j = 0; j < 8; ++j)
        for (int i = 0; i < g->w; ++i)
            if (g->row[j][i] == '#') rect(x + i * scale, y + j * scale, scale, scale, shade);
    return x + (g->w + 1) * scale;
}

}  // namespace gb
