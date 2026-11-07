// LSDJ player: drives the sequencer, instruments and commands against a gb::Apu.
// Timing: tick rate 0.4 x tempo Hz; envelopes and slides run on a 360 Hz clock (measured on LSDJ 9.4.2).
#pragma once

#include <cstdint>

#include "gb/apu.h"
#include "gb/kits.h"
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
    // loopCh >= 0: that channel loops one phrase; with withSong the other channels play the song from `row` behind it (Rec + Play overdub),
    // without it they stay silent.
    void start(int row = 0, int loopCh = -1, int loopPhrase = 0, bool withSong = false);

    // Stopping silences the chip: a console stopped while the other keeps playing is still rendered and must not hold its last notes.
    void stop() { running_ = false; live_ = false; seq_.stopAll(); apu_.reset(); }
    void bootChime(int stage);                       // the power-on "ba-ding": stage 0 the low note, 1 the high one (starts the live engine)
    bool running() const { return running_ && seq_.playing(); }
    void run(uint32_t cycles);                       // advance the whole machine
    Apu& apu() { return apu_; }
    const Seq& seq() const { return seq_; }
    bool voiceOn(int c) const { return v_[c].on; }
    bool songLooped() const { return seq_.allLooped(); }   // every active channel has reached song row 0 again
    void setTempo(int t) { tempo_ = t; }
    void setTickPhase(int r) { tickAcc_ = 896 - tempo_ + r; }   // before the first run: sub-steps already counted toward the first tick (the ROM's leftover accumulator; tools only)
    int tempo() const { return tempo_; }
    // Audio: `n` stereo frames at `rate` Hz, 16-bit interleaved. Each output sample is the box-filtered mix.
    void render(int16_t* out, int n, int rate);
    void mute(int ch, bool m);                       // a muted channel is cut at once and starts no new notes
    bool muted(int ch) const { return muted_[ch]; }
    void setKits(const KitSource* k) { kits_ = k; }
    // Live play: a pad plays an instrument on a channel with or without a song running. kScratch plays the 16 bytes of scratchInstr()
    // (the Synth and Wave screens preview a wave that way) instead of a song instrument.
    static constexpr int kScratch = 64;
    uint8_t* scratchInstr() { return scratch_; }
    void previewOn(int ch, int note, int instrIdx);
    void previewOff(int ch);
    bool live() const { return live_; }              // the engine runs for previewed notes although no song plays
    void setWaveHook(void (*f)(int frame, int tick)) { waveHook_ = f; }

private:
    struct Voice {
        uint8_t instr = 0;
        int note = 0;           // after transposes
        int vol = 0;            // current envelope volume (pulse/noise)
        int envCount = 0, envSteps = 0;
        bool on = false;
        int pending = -1;       // ticks until a delayed note starts
        StepEvent delayed{};
        int kill = -1;          // ticks until the note is killed
        uint8_t pan = 3;        // bit0 left, bit1 right
        int base = 0;           // note with chain and project transposes
        int tTrans = 0;         // current table transpose
        int fade = -1;          // release nibble override (E command), -1 = instrument's
        int baseVol = 0;        // volume last programmed (start volume, E, table amplitude): a retrigger reloads this, not the decayed volume
        int retrigDv = 0;       // R: volume change per retrigger (signed first digit)
        int tbl = -1;           // running table or -1
        int tGen = 0;           // counts table starts, so a table row that runs A does not overwrite the new table's position
        uint8_t stepRow[32] = {};   // row each per-note table stops at: the ROM resumes it there when its instrument comes back after another one
        int stepTbl = -1;       // table kept between notes while the instrument runs it per note (byte 5 bit 3)
        bool tStep = false, tStepNow = false;   // step mode: a row runs only on the tick a note starts
        int tRow = 0, tWait = 0, eRow = 0, eWait = 0;
        int noteTick = -1;      // tick of the last note start
        int envStage = 1;       // instrument envelope stage: 1 towards byte 9's level, 2 towards byte 10's, 3 to silence
        uint8_t tGroove = 0xFF; int tGroovePos = 0;   // table groove and its position (each row takes the next entry)
        uint8_t hop[16] = {};
        int vibSpeed = 0, vibDepth = 0;   // V command (depth 0 = off)
        double vibPhase = 0;
        double vibSemis = 0;
        int startedTick = -1;                                         // tick the note started on
        bool hwEnv = false;                                           // E is fading the volume (the normal envelope stages are off)
        int ePace = 0; bool eUp = false; double eAcc = 0;            // E: one volume step per pace * 65536 cycles, up when the digit is 8 or more
        int chordAge = 0;                                             // ticks since the chord started (byte 8 sets how many each note lasts)
        int bendDelay = 0;                                            // sub-steps to wait before a slide starts moving
        bool bendTgt = false;                                         // L: the accumulator slides to 0 and stops there
        int bendRate = 0; int16_t bendAcc = 0;      // P: rate and accumulator in 1/256 semitone (16 bit, as the ROM)                          // P
        int chord[3] = {0, 0, 0}; bool chordOn = false; int chordCnt = 0, chordOfs = 0;   // C
        int kPos = 0, kChunks = 0, kLimit = 0, kBeg[2] = {0, 0}, kEnd[2] = {0, 0}, kFreq = 0x749; bool kPlay = false, kLoop = false, kHas[2] = {false, false}; Kit kit[2];   // kit samples being streamed
        double wPos = 0; int wDir = 1, wCnt = 0; bool wAnim = false;   // wave frame animation
        int noiseS = 0, shapeOfs = 0; double shapeAcc = 0; bool noiseFresh = false;           // noise: P value, and the S / P shape offset in index steps
        int retrig = -1, retrigCnt = 0;                              // R: period in ticks, -1 off
        uint8_t lastCmd[3] = {0, 0, 0}, lastVal[3] = {0, 0, 0};   // for Z: phrase, table column 1, table column 2 (ROM keeps them apart)                          // for Z
        int fine = -1, tune2 = 0;                                    // F override (fine -1 = use the instrument byte)
    };
    void tick();
    void sub360();
    void onStep(const StepEvent& e);
    void noteOn(int ch, const StepEvent& e);
    void killNote(int ch);
    void command(int ch, uint8_t cmd, uint8_t val, bool fromTable, bool again = false);   // again: Z re-applies it, keeping running phases
    void startTable(int ch, int t);
    void retrigVolume(int ch);
    void startSlide(int ch, int val);
    static int pitchIndex(int ch, int n);
    static int bendRateOf(uint8_t val);
    static uint16_t pitchToFreq(const uint16_t* tab, int total, int16_t& bendAcc);
    static int waveIndex(int n);
    void tableTick(int ch);
    void setVolume(int ch, int target);
    void refreshPitch(int ch);
    void writeDuty(int ch, int duty);
    void retrigger(int ch);
    void loadWave(int frame);
    void kitChunk();
    void startKit(const StepEvent& e);
    void waveTick();
    void masterVol(uint8_t val);
    unsigned rng();                       // LSDJ's generator: S = 17 * S + 0x5C93 (16 bit), the result is the high byte
    int randMax(int m) { return m ? int(rng() % unsigned(m + 1)) : 0; }   // 0..m, no generator call for 0
    uint16_t rngState_ = 0x0052;
    bool inA_ = false;                    // a table row is already being run on behalf of A (no recursion)
    int slot_ = 0;                        // which Z memory a command belongs to: 0 phrase, 1 table column 1, 2 table column 2
    void writePan();
    uint8_t* instr(int i) { return i == kScratch ? scratch_ : s_.instrument(i & 63); }
    uint8_t scratch_[16] = {};
    bool live_ = false;
    void powerLive();

    Song& s_;
    Seq seq_;
    Apu apu_;
    Voice v_[4];
    bool muted_[4] = {false, false, false, false};
    const KitSource* kits_ = nullptr;
    long long nextKit_ = 0;
    uint8_t nr50_ = 0x77;
    void (*waveHook_)(int, int) = nullptr;
    int tickNo_ = 0;
    bool running_ = false;
    int tempo_ = 128;
    long long next360_ = 0;                  // next sub-step (every 11704 cycles); ticks come from it: tickAcc_ += tempo, a tick at 896
    int tickAcc_ = 0;
    long long now_ = 0;
    // render state
    uint32_t carryFx_ = 0;                // render: fractional cycles carried, 16.16 fixed point (no doubles: the Tracker's FPU is single precision)
    float hpL_ = 0, hpR_ = 0, prevL_ = 0, prevR_ = 0;
    bool hpInit_ = false;
};

}  // namespace gb
