#include "gb/seq.h"

namespace gb {

bool Seq::enterChainPos(Ch& h, int pos) {
    if (pos >= 16) return false;
    const uint8_t p = s_.chainPhrase(h.chain, pos);
    if (p == 0xFF) return false;
    h.cpos = pos;
    h.phrase = p;
    h.step = 0;
    return true;
}

bool Seq::enterRow(Ch& h, int row) {
    if (row >= kSongRows) return false;
    const uint8_t c = s_.songCell(row, int(&h - ch_));
    if (c == 0xFF) return false;
    h.row = row;
    h.chain = c;
    return enterChainPos(h, 0);
}

void Seq::start(int row) {
    for (int c = 0; c < 4; ++c) {
        Ch& h = ch_[c];
        h = Ch();
        h.active = enterRow(h, row);
        h.wait = 0;
    }
}

int Seq::grooveTicks(Ch& h) {
    const uint8_t* g = s_.groove(h.groove);
    int t = g[h.gpos & 15];
    h.gpos = (h.gpos + 1) & 15;
    if (g[h.gpos] == 0) h.gpos = 0;      // a groove ends at its first zero tick count
    return t ? t : 1;
}

bool Seq::hopNext(Ch& h) {
    if (enterChainPos(h, h.cpos + 1)) return true;
    if (enterRow(h, h.row + 1)) return true;
    h.looped = true;
    if (enterRow(h, 0)) return true;
    h.active = false;
    return false;
}

void Seq::advance(Ch& h) {
    if (++h.step < 16) return;
    if (enterChainPos(h, h.cpos + 1)) return;
    if (enterRow(h, h.row + 1)) return;
    // past the last row: loop to the first row
    h.looped = true;
    if (!enterRow(h, 0)) h.active = false;
}

}  // namespace gb
