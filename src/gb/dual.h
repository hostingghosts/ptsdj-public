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
    // Rec + Play: the console's channel `ch` loops `phrase` while the songs play behind it from rowA / rowB. master >= 0 links the consoles
    // (the other console starts too and follows the master's tempo); otherwise only `console` plays.
    void startOverdub(int console, int ch, int phrase, int rowA, int rowB, int master);
    void startSolo(int console, int ch, int phrase, int chain);   // only this console, only this track: one phrase (chain < 0) or one chain, looping
    void setVolume(int console, int v) { vol_[console ? 1 : 0] = v < 0 ? 0 : v > 15 ? 15 : v; }   // 0-15, the Master page
    int volume(int console) const { return vol_[console ? 1 : 0]; }
    void setFollow(int master) { follow_ = master; }
    void stop();
    bool running() const { return a_.running() || b_.running(); }
    // `n` stereo frames at `rate` Hz, 16-bit interleaved: A plus B, each at 0.7 so two loud consoles do not clip.
    void render(int16_t* out, int n, int rate);
    // True once both consoles' channels have all come round to the top of the song again (the end of one pass).
    bool looped() const { return (!a_.running() || a_.songLooped()) && (!b_.running() || b_.songLooped()); }
    Player& player(int i) { return i ? b_ : a_; }
    // The last step of the mix: the output is 1.6 times louder than the chips' own level, and anything that would pass 60 % of full scale is rounded off
    // smoothly (a soft knee) instead of being cut flat, so loud passages do not distort. `mixed` is the sum of both consoles at 0.7 each.
    static int16_t finish(int mixed);
    bool live() const { return a_.live() || b_.live(); }      // previewed notes sounding without a song

private:
    Player& a_;
    Player& b_;
    int vol_[2] = {15, 15};
    int follow_ = -1;                       // the console whose tempo the other one follows, -1 = none
};

}  // namespace gb
