// Two Game Boys side by side: tracks 1-4 play song A, tracks 5-8 song B. Both start and stop together, each keeps its own
// tempo and its own per-channel left/right panning, and the two stereo outputs are summed.
#pragma once

#include <cstdint>

#include "gb/player.h"

namespace gb {

// The last step of the mix: a look-ahead peak limiter, set from the Master Mix page. The mix of both consoles is made `drive` louder (default +9 dB over the chips' own
// level), then any peak that would pass the ceiling is turned down smoothly before it arrives (the gain falls over the 1.4 ms ahead of the peak and rises again over
// the release time, 125 ms by default), so loud passages get quieter instead of distorting and nothing is clipped or rounded. Quiet material is only amplified.
// Off, the mix passes at the chips' own level (that cannot clip) and only the 1.4 ms delay is left.
class OutputLimiter {
public:
    struct Settings {
        bool on = true;
        int driveHalfDb = 18;                     // 0 .. 36: the gain before the limiter in half dB (18 = +9.0 dB)
        int ceilHalfDb = -1;                      // -24 .. -1: the highest peak out, half dB below full scale (-1 = -0.5 dBFS)
        int releaseMs = 125;                      // 25 .. 500, in steps of 25: how long the gain takes to come back from fully down to unity
        bool operator==(const Settings& o) const { return on == o.on && driveHalfDb == o.driveHalfDb && ceilHalfDb == o.ceilHalfDb && releaseMs == o.releaseMs; }
        bool operator!=(const Settings& o) const { return !(*this == o); }
        void clamp();                             // into the ranges above
    };
    static constexpr int kLook = 64;              // frames of look-ahead (the output is delayed by kLook - 1 frames)
    OutputLimiter() { set(Settings()); reset(); }
    // New settings take hold at once for the ceiling and the release; the drive glides to its new value in a few milliseconds so a turn of the knob does not click.
    void set(const Settings& s);
    const Settings& settings() const { return s_; }
    float drive() const { return tgt_; }          // the gain before the limiter, linear
    float ceiling() const { return ceil_; }       // the highest peak, 1.0 = full scale
    void reset();
    // One stereo frame in full-scale units (1.0 = 32768) before the drive; the louder, limited frame comes out delayed.
    void process(float& l, float& r);
    float gain() const { return float(sum_) / float(kLook * 65536); }   // the reduction being applied now (1.0 = none)
private:
    Settings s_;
    float tgt_ = 1.f, gain_ = 1.f, ceil_ = 1.f;    // the drive wanted, the drive now, the ceiling (1e9 when off)
    uint32_t rel_ = 12;                            // the most the gain may rise in one frame (Q16)
    float dl_[kLook], dr_[kLook];                  // the delay line
    uint32_t qv_[kLook], qi_[kLook];               // sliding minimum of the needed gain (Q16) over the last kLook frames: values and their frame numbers
    uint32_t mbuf_[kLook];                         // the gain steps (Q16) the smoother averages
    uint32_t n_ = 0, head_ = 0, tail_ = 0, m_ = 65536, sum_ = 0;
};

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
    void setMix(const OutputLimiter::Settings& s) { lim_.set(s); }          // the limiter, from the Master Mix page
    const OutputLimiter& limiter() const { return lim_; }
    void setFollow(int master) { follow_ = master; }
    void stop();
    bool running() const { return a_.running() || b_.running(); }
    // `n` stereo frames at `rate` Hz, 16-bit interleaved: A plus B, each at 0.7, then the OutputLimiter.
    void render(int16_t* out, int n, int rate);
    // True once both consoles' channels have all come round to the top of the song again (the end of one pass).
    bool looped() const { return (!a_.running() || a_.songLooped()) && (!b_.running() || b_.songLooped()); }
    Player& player(int i) { return i ? b_ : a_; }
    bool live() const { return a_.live() || b_.live(); }      // previewed notes sounding without a song

private:
    Player& a_;
    Player& b_;
    int vol_[2] = {15, 15};
    int follow_ = -1;                       // the console whose tempo the other one follows, -1 = none
    OutputLimiter lim_;
};

}  // namespace gb
