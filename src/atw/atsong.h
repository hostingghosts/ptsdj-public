// Atari tracker -- song data model (docs/ATARI_PLAN.md).
//
// Four tracks (POKEY) or two (TIA), rows of NOTE | INSTRUMENT | VOLUME | FX1 | FX2, 48 RMT-style instruments per chip, one order list
// with a repeat count per entry. Plain data only; the sound comes from the engine of core/src (reached as `atari/...`), and
// conversions to and from the core `atari::Song` give the exporters and the Mac app session.
#pragma once

#include <cstdint>
#include <vector>

#include "atari/instrument.h"
#include "atari/song.h"

namespace atw {

constexpr int kTracks = 4;
constexpr int kMaxRows = 128;
constexpr int kDefaultRows = 32;
constexpr int kPatternCount = 64;
constexpr int kInstrumentCount = 48;
constexpr int kOrderLen = 255;
constexpr int kNameLen = 24;

namespace note {
constexpr uint8_t kEmpty = 0;
constexpr uint8_t kFirst = 1;          // note value n is chip note n - 1 (C-1 = 1)
constexpr uint8_t kLast = 62;          // chip note 61
constexpr uint8_t kOff = 63;           // "===" note off
inline bool isNote(uint8_t n) { return n >= kFirst && n <= kLast; }
}  // namespace note

// One effect: `type` is the core FxType (atari::FX_*), 0 = none.
struct Effect {
    uint8_t type = 0;
    uint8_t value = 0;
};

// One row of one track.
struct Cell {
    uint8_t note = note::kEmpty;       // 0 empty, 1..62 notes, 63 off
    uint8_t inst = 0;                  // 0 none, 1..48
    uint8_t vol = 0;                   // 0 none, 1..16 = volume 0..15
    Effect fx[2];
    bool empty() const { return !note && !inst && !vol && !fx[0].type && !fx[1].type; }
};

struct Pattern {
    uint8_t rows = kDefaultRows;       // 1..kMaxRows
    Cell cells[kMaxRows][kTracks];
    void clear(uint8_t length = kDefaultRows);
    bool empty() const;
};

// The Envelope page's ADSR shaping (as in the Mac app): it writes the instrument's envelope, one step per video frame: attack up to the peak,
// decay down to the sustain level, then a looping sustain step. Kept with the song so the page shows the same settings again.
struct Adsr {
    bool on = false;
    uint8_t attack = 2, decay = 6, sustain = 10, peak = 15;   // frames, frames, level, level (frames 0..15, levels 0..15)
    uint8_t dist = 10;                                        // AUDC distortion of every step (even, 0..14)
    uint8_t curve = 0;                                        // 0 linear, 1 fast start, 2 slow start
};
// The envelope the settings make (volume per step, loop step); false when the shape is empty.
int adsrSteps(const Adsr& a, uint8_t vol[32]);

// One row of the song grid: the pattern (and what each of the four tracks plays), repeated `repeat` times.
struct OrderEntry {
    static constexpr uint8_t kSame = 0xFE;   // cell: this track plays the entry's pattern
    static constexpr uint8_t kOut = 0xFF;    // cell: this track is taken out (silent) while the entry plays; its notes stay in the pattern
    uint8_t pattern = 0;               // the pattern of every track whose cell is kSame
    uint8_t repeat = 1;                // 1..255 plays before the next entry
    uint8_t tune = 0;                  // 1 = a tune of its own starts here (a game can start it; each tune loops over its own entries)
    uint8_t cell[kTracks] = {kSame, kSame, kSame, kSame};   // per pattern track: kSame, kOut or another pattern
    // The pattern track `t` plays in this entry, -1 when it is taken out.
    int trackPattern(int t) const { return cell[t] == kSame ? pattern : (cell[t] == kOut ? -1 : cell[t]); }
    bool uses(int p) const { for (int t = 0; t < kTracks; ++t) if (trackPattern(t) == p) return true; return false; }
    bool plain() const { return cell[0] == kSame && cell[1] == kSame && cell[2] == kSame && cell[3] == kSame; }
};

// What the exporters and the Mac session make of a grid row: one pattern of the exported song, each track taken from a pattern of the
// Tracker (0xFF = taken out) and looped up to `rows` (the longest pattern of the row). A row without cells is just its pattern.
struct PlanPattern {
    uint8_t src[kTracks] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t rows = 16;
    bool operator==(const PlanPattern& o) const { return src[0] == o.src[0] && src[1] == o.src[1] && src[2] == o.src[2] && src[3] == o.src[3]; }
};

struct Song {
    char name[kNameLen + 1] = {};
    char author[kNameLen + 1] = {};
    atari::Chip chip = atari::Chip::Pokey;
    atari::Region region = atari::Region::NTSC;
    uint8_t speed = 6;                 // video frames per row at the start (1..255)
    uint8_t releaseFrames = 6;         // POKEY note-off tail, 0..15
    uint8_t sfxCount = 0;              // sound effects (a game plays them on a channel it names): a pattern and a pattern track each, at most 16
    uint8_t sfxPattern[16] = {};
    uint8_t sfxTrack[16] = {};
    uint8_t engineSpeed = 1;           // player ticks per video frame: 1, 2 or 4 (speeds, funk, delays, envelopes all count ticks)
    uint8_t stepsPerBeat = 4;          // rows per beat, 1, 2, 4 .. 64: row shading, metronome and count-in (not the tempo of the exported music)
    uint8_t funk = 0;                  // funk speed: odd rows last this many frames (0 = like every other row)
    uint8_t tiaTune = 0;               // TIA note tables: 0 concert pitch, 1 optimised per waveform (core divcompute.h)
    uint8_t orderLen = 1;
    uint8_t restartPos = 0;            // order position the song loops back to
    OrderEntry order[kOrderLen];
    uint8_t trackVolume[kTracks] = {100, 100, 100, 100};   // mixer level, 0..100 %
    uint8_t tiaTrack[2] = {0, 1};      // TIA: which pattern tracks feed its two channels (the other two are greyed out)
    Adsr adsr[kInstrumentCount];       // POKEY instruments only
    atari::PokeyInstrument pokey[kInstrumentCount];
    atari::TiaInstrument tia[kInstrumentCount];
    // kPatternCount patterns, owned by the app (SDRAM on the device).
    Pattern* patterns = nullptr;

    int trackCount() const { return chip == atari::Chip::Pokey ? 4 : 2; }   // chip voices
    // The pattern always has four tracks. Chip voice ('slot') of a pattern track, -1 when the chip does not play it (TIA: two of the four).
    int trackSlot(int track) const {
        if (chip == atari::Chip::Pokey) return track;
        return track == tiaTrack[0] ? 0 : (track == tiaTrack[1] ? 1 : -1);
    }
    bool trackUsed(int track) const { return trackSlot(track) >= 0; }
    int slotTrack(int slot) const { return chip == atari::Chip::Pokey ? slot : tiaTrack[slot & 1]; }
    // How many patterns exist: up to the last one that has notes or is used by the order list (at least one). The Song screen offers only these.
    int patternsCreated() const {
        int n = 1;
        for (int i = 0; i < kPatternCount; ++i) if (!patterns[i].empty()) n = i + 1;
        for (int i = 0; i < orderLen && i < kOrderLen; ++i) {
            if (order[i].pattern + 1 > n) n = order[i].pattern + 1;
            for (int t = 0; t < kTracks; ++t) if (order[i].trackPattern(t) + 1 > n) n = order[i].trackPattern(t) + 1;
        }
        return n;
    }
    // Rows an order entry lasts: the longest pattern any of its tracks plays (a shorter one loops); its own pattern when every track is out.
    int entryRows(const OrderEntry& e) const {
        int rows = 0;
        for (int t = 0; t < kTracks; ++t) {
            const int p = e.trackPattern(t);
            if (p >= 0 && p < kPatternCount && patterns[p].rows > rows) rows = patterns[p].rows;
        }
        return rows ? rows : patterns[e.pattern % kPatternCount].rows;
    }
    // The PlanPattern of an order entry / of a plain pattern.
    PlanPattern planOf(const OrderEntry& e) const {
        PlanPattern pp;
        for (int t = 0; t < kTracks; ++t) {
            const int p = e.trackPattern(t);
            pp.src[t] = p >= 0 && p < kPatternCount ? uint8_t(p) : uint8_t(0xFF);
        }
        pp.rows = uint8_t(entryRows(e));
        return pp;
    }
    PlanPattern planOf(int pattern) const {
        PlanPattern pp;
        for (int t = 0; t < kTracks; ++t) pp.src[t] = uint8_t(pattern % kPatternCount);
        pp.rows = patterns[pattern % kPatternCount].rows;
        return pp;
    }
    int sfxFind(int pattern, int track) const {
        for (int i = 0; i < sfxCount; ++i) if (sfxPattern[i] == pattern && sfxTrack[i] == track) return i;
        return -1;
    }
    Pattern& pattern(int i) { return patterns[i % kPatternCount]; }
    const Pattern& pattern(int i) const { return patterns[i % kPatternCount]; }
    // Empty song: one order entry (pattern 0), the starter instruments (own sounds, then the RMT instruments), patterns cleared.
    void clear();
    // Starter instrument for slot i (the built-in sounds, then the RMT instruments, else a plain pure tone / square).
    void resetInstrument(int i);
};

// Tempo as the Mac app shows it: beats per minute at four rows per beat and the nominal 60 / 50 frames a second. A row lasts a whole
// number of video frames, so the choices are 900 / speed (NTSC) or 750 / speed (PAL): 900, 450, 300, 225, 180, 150, ...
// Beats per minute for a speed: a beat lasts 4 * speed ticks however many rows it is cut into (Song::stepsPerBeat).
inline int bpmOfSpeed(int speed, atari::Region region, int engineSpeed = 1) { return (int(region == atari::Region::PAL ? 750 : 900) * engineSpeed + (speed > 0 ? speed / 2 : 0)) / (speed > 0 ? speed : 1); }

// Beats per minute with funk counted: a pair of rows lasts speed + funk ticks, so a row averages half of that (funk 0 = off: the plain bpmOfSpeed).
inline int bpmOfPair(int speed, int funk, atari::Region region, int engineSpeed = 1) {
    if (funk <= 0) return bpmOfSpeed(speed, region, engineSpeed);
    const int pair = speed + funk;
    return (int(region == atari::Region::PAL ? 750 : 900) * engineSpeed * 2 + pair / 2) / (pair > 0 ? pair : 1);
}

// POKEY has one AUDCTL, so some instrument settings belong to particular tracks (bit 0 = track 1 ... bit 3 = track 4):
//   16-bit join 1+2 (AUDCTL $10): track 2 only; join 3+4 ($08): track 4 only (the high channel of the pair plays it)
//   1.79 MHz on channel 1 ($40): track 1 only; on channel 3 ($20): track 3 only (a joined pair uses the pair's own clock bit)
//   automatic high-pass filter in an envelope step: tracks 1 and 2 only (it needs channel 3 / 4 as its partner)
// An instrument on another track plays at the wrong pitch or without its filter, so the Tracker does not offer it there.
uint8_t instrumentTrackMask(const atari::PokeyInstrument& in);
// "tracks 2 and 4" for a mask of allowed tracks.
void trackMaskText(uint8_t mask, char out[24]);

// The steps of track `t` of a PlanPattern, `rows` of them: the source pattern looped; a track taken out gets just a note-off on row 0,
// so what it was playing stops (the sequencer does the same). Track volumes are not applied.
void planTrackSteps(const Song& s, const PlanPattern& pp, int t, atari::Step* out);

// ---- cell <-> core step ----------------------------------------------------------------------------------------------
atari::Step toStep(const Cell& c);
Cell fromStep(const atari::Step& s);

// ---- the core song (exporters, Mac session) ----------------------------------------------------------------------------
// The order list becomes the playlist (a repeat is written out); `restartPos` the loop point. TIA keeps tracks 0 and 1.
bool toCore(const Song& s, atari::Song& out);
// A core song into `out` (its patterns must exist): patterns the playlist uses are packed from 0, repeats of the same pattern in a row
// become one entry. False when it has more than kPatternCount patterns or order entries.
bool fromCore(const atari::Song& s, Song& out);

// ---- effect letters ----------------------------------------------------------------------------------------------------
// The first letter of the effect's name, lower case when it is taken, left out when the chip cannot use it (docs/ATARI_PLAN.md):
//   V volume, S slide up, s slide down, A arpeggio, v vibrato (POKEY), T tempo, B break, a AUDCTL (POKEY), C cut.
bool fxAvailable(uint8_t type, atari::Chip chip);
char fxLetter(uint8_t type);                       // '\0' for an unknown type
// The Master screen's track volume applied to a step: a note (or an explicit volume) gets its volume scaled, and so does a V effect. Used by the live
// sequencer, the WAV export and the ROM / assembly exports, so what you hear is what is exported.
atari::Step withTrackVolume(atari::Step s, int percent);
const char* fxName(uint8_t type);                  // "Volume" ...
const char* fxParamHelp(uint8_t type);             // "0-F level" ...
uint8_t fxFromLetter(char letter, atari::Chip chip);   // 0 when the letter is not an effect of that chip
// Effects of a chip in the order the picker lists them.
int fxList(atari::Chip chip, uint8_t out[16]);

// Note value (1..62) -> "C-1" (3 chars + NUL); off "==="; empty "---".
void noteName(uint8_t note, char out[4]);

}  // namespace atw
