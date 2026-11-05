// Atari tracker -- undo / redo.
//
// Every input event is bracketed by begin() / end(): begin() copies what the current screen edits (the pattern on the Pattern screen, the
// instrument on the Instrument screen, the song settings and order list on Song and Settings), end() keeps that copy as one undo step if the
// event edited the song (App::markDirty). Edits of the same object less than kMergeMs apart are one step, so turning the jog is one undo.
// Undo swaps a step with the live object, so the same entry then serves redo; a new edit drops the redo steps. Memory is bounded
// (kMaxBytes, kMaxSteps): the oldest steps go first. Plain heap, no external memory.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "atw/atsong.h"

namespace atw {

class Undo {
public:
    static constexpr uint32_t kMergeMs = 400;
    static constexpr int kMaxSteps = 64;
    static constexpr uint32_t kMaxBytes = 96 * 1024;

    enum class Kind : uint8_t { kNone, kPattern, kInstrument, kHead };
    struct Target {
        Kind kind = Kind::kNone;
        uint8_t index = 0;      // pattern, or instrument slot 0..47
        atari::Chip chip = atari::Chip::Pokey;   // instruments: which chip's slot
    };

    void bind(Song* s) { song_ = s; }
    void reset();                                        // song replaced: no history
    void begin(Target t);                                // before an input event
    void end(bool edited, uint32_t nowMs);               // after it
    bool undo(Target* applied);                          // false: nothing to undo
    bool redo(Target* applied);
    int undoSteps() const { return int(undo_.size()); }
    int redoSteps() const { return int(redo_.size()); }

private:
    struct Head {
        char name[kNameLen + 1], author[kNameLen + 1];
        atari::Chip chip;
        atari::Region region;
        uint8_t speed, releaseFrames, tiaTune, funk, stepsPerBeat, engineSpeed, sfxCount, orderLen, restartPos;
        OrderEntry order[kOrderLen];
        uint8_t trackVolume[kTracks];
        uint8_t sfxPattern[16], sfxTrack[16];
        uint8_t tiaTrack[2];
    };
    struct Entry {
        Target t;
        uint8_t rows = 0;
        std::vector<Cell> cells;                         // rows x kTracks
        std::unique_ptr<Head> head;
        atari::PokeyInstrument pokey;
        atari::TiaInstrument tia;
        uint32_t bytes() const;
    };
    bool capture(Target t, Entry& e) const;
    void apply(const Entry& e);
    bool swapWith(std::vector<Entry>& from, std::vector<Entry>& to, Target* applied);
    void trim();

    Song* song_ = nullptr;
    std::vector<Entry> undo_, redo_;
    Entry pending_;
    bool havePending_ = false;
    uint32_t lastMs_ = 0;
};

}  // namespace atw
