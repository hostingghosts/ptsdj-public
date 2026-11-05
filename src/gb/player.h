// LSDJ player: drives the sequencer, instruments and commands against a gb::Apu.
// Timing: tick rate 0.4 x tempo Hz; envelopes and slides run on a 360 Hz clock (measured on LSDJ 9.4.2).
#pragma once

#include <cstdint>

#include "gb/apu.h"
#include "gb/seq.h"
#include "gb/song.h"

namespace gb {

// Phrase / table command bytes (A=1 ... measured: D=4, H=8; the rest follow LSDJ's letter order).
enum Cmd : uint8_t {
    kCmdNone = 0, kCmdA, kCmdB, kCmdC, kCmdD, kCmdE, kCmdF, kCmdG, kCmdH, kCmdK, kCmdL, kCmdM, kCmdO, kCmdP, kCmdR, kCmdS, kCmdT,
    kCmdV, kCmdW, kCmdZ
};

class Player {
public:
    explicit Player(Song& s) : s_(s), seq_(s) {}
    void start(int row = 0);
    void stop() { running_ = false; }
    bool running() const { return running_ && seq_.playing(); }
    void run(uint32_t cycles);                       // advance the whole machine
    Apu& apu() { return apu_; }
    const Seq& seq() const { return seq_; }
    bool voiceOn(int c) const { return v_[c].on; }
    void setTempo(int t) { tempo_ = t; }
    int tempo() const { return tempo_; }
    // Audio: `n` stereo frames at `rate` Hz, 16-bit interleaved. Each output sample is the box-filtered mix.
    void render(int16_t* out, int n, int rate);
    void mute(int ch, bool m) { muted_[ch] = m; }

private:
    struct Voice {
        uint8_t instr = 0;
        int note = 0;           // after transposes
        int vol = 0;            // current envelope volume (pulse/noise)
        int envCount = 0;
        bool on = false;
        int pending = -1;       // ticks until a delayed note starts
        StepEvent delayed{};
        int kill = -1;          // ticks until the note is killed
        uint8_t pan = 3;        // bit0 left, bit1 right
    };
    void tick();
    void sub360();
    void onStep(const StepEvent& e);
    void noteOn(int ch, const StepEvent& e);
    void killNote(int ch);
    void writePan();
    uint8_t* instr(int i) { return s_.instrument(i & 63); }

    Song& s_;
    Seq seq_;
    Apu apu_;
    Voice v_[4];
    bool muted_[4] = {false, false, false, false};
    bool running_ = false;
    int tempo_ = 128;
    double nextTick_ = 0, next360_ = 0;
    long long now_ = 0;
    // render state
    double carry_ = 0;
    float hpL_ = 0, hpR_ = 0, prevL_ = 0, prevR_ = 0;
    bool hpInit_ = false;
};

}  // namespace gb
