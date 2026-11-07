// Two Game Boys side by side: tracks 1-4 play song A, tracks 5-8 song B. Both start and stop together, each keeps its own
// tempo and its own per-channel left/right panning, and the two stereo outputs are summed.
#pragma once

#include <cstdint>

#include "gb/player.h"

namespace gb {

class Dual {
public:
    Dual(Player& a, Player& b) : a_(a), b_(b) {}
    void start(int rowA, int rowB);
    // One console alone (SYNC off), or both with the virtual link cable: the console that started is the master and the other follows its tempo.
    void startOne(int console, int row);
    void startLinked(int master, int rowA, int rowB);
    void startLoop(int console, int ch, int phrase);   // one console, one channel looping one phrase (live recording)
    void stop();
    bool running() const { return a_.running() || b_.running(); }
    // `n` stereo frames at `rate` Hz, 16-bit interleaved: A plus B, each at 0.7 so two loud consoles do not clip.
    void render(int16_t* out, int n, int rate);
    // True once both consoles' channels have all come round to the top of the song again (the end of one pass).
    bool looped() const { return (!a_.running() || a_.songLooped()) && (!b_.running() || b_.songLooped()); }
    Player& player(int i) { return i ? b_ : a_; }
    bool live() const { return a_.live() || b_.live(); }      // previewed notes sounding without a song

private:
    Player& a_;
    Player& b_;
    int follow_ = -1;                       // the console whose tempo the other one follows, -1 = none
};

}  // namespace gb
