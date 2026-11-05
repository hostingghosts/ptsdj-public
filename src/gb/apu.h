// Game Boy (DMG) sound unit, written from the hardware behaviour (Pan Docs level) and checked sample-for-sample
// against the reference emulator with tools/oracle. Event driven: advance() jumps from timer event to timer event
// and integrates the mix, so cost follows the note rates, not the 4.19 MHz clock.
#pragma once

#include <cstdint>

namespace gb {

constexpr uint32_t kClock = 4194304;   // T-cycles per second

class Apu {
public:
    Apu() { reset(); }
    void reset();
    void write(uint16_t addr, uint8_t v);          // 0xFF10..0xFF3F
    uint8_t read(uint16_t addr) const;

    // Run `cycles` T-cycles. The mix is integrated into the accumulators (value x cycles).
    void advance(uint32_t cycles);
    // Instantaneous mix per side: sum of (2 x level - 15) of the routed DAC-on channels, times (master volume + 1).
    void mixNow(int& l, int& r) const;
    // Take (and clear) the integral since the last call: average level over `cycles` for each side.
    void takeAverage(uint32_t cycles, float& l, float& r);

    bool on() const { return power_; }
    int channelLevel(int ch) const;                // digital 0..15 of one channel (for meters/tests)

private:
    struct Pulse {
        bool enabled = false, dac = false, lenEn = false;
        uint8_t duty = 0, step = 0, vol = 0, envInit = 0, envPace = 0, envTimer = 0;
        bool envUp = false, envActive = false;
        uint16_t length = 0, freq = 0;
        int32_t timer = 0;
        // sweep (channel 1)
        uint8_t swPace = 0, swShift = 0, swTimer = 0;
        bool swDown = false, swOn = false;
        uint16_t shadow = 0;
    };
    struct Wave {
        bool enabled = false, dac = false, lenEn = false;
        uint8_t volCode = 0, pos = 0;
        uint16_t length = 0, freq = 0;
        int32_t timer = 0;
        uint8_t ram[16] = {};
    };
    struct Noise {
        bool enabled = false, dac = false, lenEn = false;
        uint8_t vol = 0, envInit = 0, envPace = 0, envTimer = 0, shift = 0, div = 0;
        bool envUp = false, narrow = false, envActive = false;
        uint16_t length = 0, lfsr = 0x7FFF;
        int32_t timer = 0;
    };

    void trigger1();
    void trigger2();
    void trigger3();
    void trigger4();
    void frameStep();
    void lengthTick();
    void sweepTick();
    void envTick();
    static void envTickOne(uint8_t& vol, uint8_t& timer, uint8_t pace, bool up, bool& active, bool enabled);
    bool sweepCalc(bool apply);
    int32_t pulsePeriod(const Pulse& p) const { return int32_t(2048 - p.freq) * 4; }
    int32_t wavePeriod() const { return int32_t(2048 - w_.freq) * 2; }
    int32_t noisePeriod() const;
    int pulseOut(const Pulse& p) const;
    int waveOut() const;
    int noiseOut() const;
    void recompute();                              // cached mix after any state change

    Pulse p1_, p2_;
    Wave w_;
    Noise n_;
    bool power_ = false;
    uint8_t nr50_ = 0, nr51_ = 0, nr10_ = 0;
    int32_t fsTimer_ = 8192;
    uint8_t fsStep_ = 0;
    int mixL_ = 0, mixR_ = 0;
    int64_t accL_ = 0, accR_ = 0;
};

}  // namespace gb
