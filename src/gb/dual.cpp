#include "gb/dual.h"

#include <algorithm>

namespace gb {

void Dual::start(int rowA, int rowB) {
    follow_ = -1;
    a_.start(rowA);
    b_.start(rowB);
}

void Dual::startOne(int console, int row) {
    follow_ = -1;
    (console ? b_ : a_).start(row);
    (console ? a_ : b_).stop();
}

void Dual::startLoop(int console, int ch, int phrase) {
    follow_ = -1;
    (console ? b_ : a_).start(0, ch, phrase);
    (console ? a_ : b_).stop();
}

void Dual::startOverdub(int console, int ch, int phrase, int rowA, int rowB, int master) {
    Player& me = console ? b_ : a_;
    Player& other = console ? a_ : b_;
    if (master >= 0) other.start(console ? rowA : rowB); else other.stop();
    me.start(console ? rowB : rowA, ch, phrase, true);
    follow_ = master;
    if (master >= 0) (master ? a_ : b_).setTempo(player(master).tempo());
}

void Dual::startLinked(int master, int rowA, int rowB) {
    start(rowA, rowB);
    follow_ = master;
    (master ? a_ : b_).setTempo(player(master).tempo());
}

void Dual::stop() {
    a_.stop();
    b_.stop();
}

void Dual::render(int16_t* out, int n, int rate) {
    int16_t ta[2 * 512], tb[2 * 512];
    while (n > 0) {
        if (follow_ >= 0) (follow_ ? a_ : b_).setTempo(player(follow_).tempo());
        const int k = std::min(n, 512);
        a_.render(ta, k, rate);
        b_.render(tb, k, rate);
        for (int i = 0; i < 2 * k; ++i) out[i] = int16_t(std::clamp((int(ta[i]) * vol_[0] + int(tb[i]) * vol_[1]) * 7 / 150, -32767, 32767));
        out += 2 * k;
        n -= k;
    }
}

}  // namespace gb
