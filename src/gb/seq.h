// LSDJ sequencer: song rows -> chains -> phrases -> steps, one tick at a time (tick rate = 0.4 x tempo Hz).
// Rules checked against the real ROM with tools/oracle/seqcheck.py.
#pragma once

#include <cstdint>

#include "gb/song.h"

namespace gb {

struct StepEvent {
    int channel;          // 0..3
    uint8_t phrase, step; // where it came from
    uint8_t note;         // 0 = none; 1 = C2
    uint8_t instr;        // 0xFF = none
    uint8_t cmd, val;
    int8_t transpose;     // chain transpose
};

class Seq {
public:
    explicit Seq(Song& s) : s_(s) {}
    void start(int row = 0);                  // play all channels from a song row
    // Advance one tick; calls f(StepEvent) for every channel step that begins on this tick.
    template <class F> void tick(F&& f) {
        for (int c = 0; c < 4; ++c) {
            Ch& h = ch_[c];
            if (!h.active) continue;
            if (h.wait > 0 && --h.wait > 0) continue;
            StepEvent e{c, h.phrase, uint8_t(h.step), s_.note(h.phrase, h.step), s_.phraseInstr(h.phrase, h.step),
                        s_.phraseCmd(h.phrase, h.step), s_.phraseVal(h.phrase, h.step), int8_t(s_.chainTrans(h.chain, h.cpos))};
            f(e);
            h.wait = grooveTicks(h);
            advance(h);
        }
    }
    bool playing() const { return ch_[0].active || ch_[1].active || ch_[2].active || ch_[3].active; }
    int row(int c) const { return ch_[c].row; }

private:
    struct Ch {
        bool active = false;
        int row = 0, cpos = 0, step = 0, gpos = 0, wait = 0;
        uint8_t chain = 0xFF, phrase = 0xFF, groove = 0;
    };
    int grooveTicks(Ch& h);
    void advance(Ch& h);
    bool enterRow(Ch& h, int row);
    bool enterChainPos(Ch& h, int pos);

    Song& s_;
    Ch ch_[4];
};

}  // namespace gb
