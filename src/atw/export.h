// Atari tracker -- exports to the SD card (AtariTracker/Export/), built on the core exporters (core/src/romdata.h, the same code the PC
// tests check against the 6502 players):
//   WAV        the order list played once at 44.1 kHz, 16-bit mono, by a second engine, so playing and editing are not disturbed
//   Assembly   <title>_asm/ with the song data (DASM), the 6502 player and a README.txt
//   .a78       playable Atari 7800 cartridge (POKEY at $4000 or TIA)
//   .bin       playable Atari 2600 4K ROM (TIA only)
// The song is the order list (repeats written out, the restart position is the loop point). WAV renders in small steps from the main loop
// (service()), so the screen keeps drawing a progress bar and the export can be cancelled; the other exports finish inside start().
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "atw/atsong.h"
#include "atw/audio.h"
#include "atari/romdata.h"
#include "hal_api.h"

namespace atw {

enum class ExportKind : uint8_t { kWav, kAsm, kA78, kBin2600 };

// The order list as the exported players see it: the patterns it uses (numbered in order of first use), the playlist with repeats written
// out, and the loop point. False (with a reason) when the playlist would be longer than the players' 254 entries.
struct ExportPlan {
    static constexpr int kMaxPatterns = 64;   // the players address 64 patterns
    PlanPattern patterns[kMaxPatterns];       // the exported song's patterns: one per distinct grid row (its tracks' patterns, looped)
    int patternCount = 0;
    // Index of `pp` in patterns[], added when new; -1 when the 64 are used up.
    int patternIndex(const PlanPattern& pp) {
        for (int i = 0; i < patternCount; ++i) if (patterns[i] == pp) return i;
        if (patternCount >= kMaxPatterns) return -1;
        patterns[patternCount] = pp;
        return patternCount++;
    }
    uint8_t playlist[254];
    int playlistLength = 0;
    int loopTo = 0;
    std::vector<std::pair<int, int>> sfx;     // sound effects: (index into patterns[], chip track)
    std::vector<int> tunes;                   // playlist indices where tunes 1, 2 ... start (entries marked as a tune start)
};
bool planExport(const Song& song, ExportPlan& plan, std::string* error);
// Just one pattern, played once and looping on itself (the "export this pattern" choice).
void planPattern(const Song& song, int pattern, ExportPlan& plan);

// The song's patterns for the core builders (exporters, Mac session), one pattern and one track at a time.
struct SongPatterns final : atari::PatternProvider {
    SongPatterns(const Song& s, const ExportPlan& p) : song(s), plan(p) {}
    bool begin(size_t index, int& rows) override {
        if (index >= size_t(plan.patternCount)) return false;
        cur = &plan.patterns[index];
        rows = cur->rows;
        return true;
    }
    void track(int t, std::vector<atari::Step>& out) override {
        out.assign(size_t(cur->rows), atari::Step());
        planTrackSteps(song, *cur, t, out.data());
        for (int r = 0; r < cur->rows; ++r) out[size_t(r)] = withTrackVolume(out[size_t(r)], song.trackVolume[t]);
    }
    const Song& song;
    const ExportPlan& plan;
    const PlanPattern* cur = nullptr;
};

class Exporter {
public:
    Exporter();
    ~Exporter();
    void bind(Song* song, ot::IStorage* fs) { song_ = song; fs_ = fs; }
    // Starts an export. For WAV it returns true at once and service() does the work; for the rest the file is finished (or failed) on return.
    // `title` names the files. False: message() says why (nothing is left half written).
    // `onlyPattern` >= 0 exports just that pattern (as a loop of one) instead of the whole order list.
    bool start(ExportKind kind, const char* title, int onlyPattern = -1);
    void service();                       // WAV: renders a few thousand samples; call every main-loop pass while busy()
    void cancel();
    bool busy() const { return job_ != nullptr; }
    int percent() const { return percent_; }
    const char* message() const { return message_.c_str(); }   // the result of the last export, or why it failed
    const char* lastPath() const { return path_; }
    static const char* extension(ExportKind k);
    // Where start() with the same arguments will write (a folder for the assembly export), so the screen can ask before replacing it.
    void targetPath(ExportKind kind, const char* title, int onlyPattern, char* out, size_t n) const;

private:
    struct WavJob;
    bool fail(const char* text);
    bool verify(const char* path, uint32_t* bytes = nullptr);   // read the file back: it is on the card and not empty
    bool makePlan(ExportPlan& plan, std::string* err) const;
    bool startWav(const char* title);
    bool buildRomFile(ExportKind kind, const char* title);
    bool buildAsm(const char* title);
    void finishWav(bool ok, const char* text);

    Song* song_ = nullptr;
    ot::IStorage* fs_ = nullptr;
    std::unique_ptr<WavJob> job_;
    int percent_ = 0;
    int onlyPattern_ = -1;
    std::string message_;
    char path_[96] = {};
};

}  // namespace atw
