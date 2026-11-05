// Atari song model and sequencer: effect letters, conversions to and from the core song, and the sequencer playing the core test songs
// sample for sample like the reference sequencer of core/src (the engine the exported ROMs follow).
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../../core/tests/testsongs.h"
#include "atw/audio.h"
#include "atw/atsong.h"
#include "atw/sequencer.h"
#include "test.h"

using namespace atw;

namespace {

struct Box {
    std::unique_ptr<Pattern[]> pats{new Pattern[kPatternCount]};
    std::unique_ptr<Song> song{new Song};
    Box() {
        song->patterns = pats.get();
        song->clear();
    }
};

bool sameStep(const atari::Step& a, const atari::Step& b) {
    return a.note == b.note && a.instrument == b.instrument && a.vol == b.vol && a.fx[0].type == b.fx[0].type && a.fx[0].value == b.fx[0].value &&
           a.fx[1].type == b.fx[1].type && a.fx[1].value == b.fx[1].value;
}

std::vector<std::pair<std::string, atari::Song>> allCoreSongs() {
    const char* dir = std::getenv("SIDT_ROOT");   // no RMT folder needed: the "rmt" song is skipped when there is none
    (void)dir;
    auto v = atari::testsongs::pokey::allSongs("");
    for (auto& s : atari::testsongs::tia::allSongs()) v.push_back({"tia " + s.first, s.second});
    return v;
}

// A TIA core song plays the tracks tiaTrack[] names; the Atari song keeps those two as tracks 0 and 1.
const atari::Step& coreStep(const atari::Song& s, int pattern, int track, int row) { return s.patterns[(size_t)pattern].track[s.trackSource(track)][(size_t)row]; }

}  // namespace

TEST(effect_letters_follow_the_naming_rule) {
    uint8_t list[16];
    for (atari::Chip chip : {atari::Chip::Pokey, atari::Chip::Tia}) {
        const int n = fxList(chip, list);
        CHECK(n == (chip == atari::Chip::Pokey ? 13 : 11));
        for (int i = 0; i < n; i++) {
            const char letter = fxLetter(list[i]);
            CHECK(letter != '\0');
            CHECK(fxFromLetter(letter, chip) == list[i]);                // the letter finds its effect again
            for (int j = i + 1; j < n; j++) CHECK(fxLetter(list[i]) != fxLetter(list[j]));
        }
    }
    CHECK(fxLetter(atari::FX_VOLUME) == 'V' && fxLetter(atari::FX_VIBRATO) == 'v');          // V taken: vibrato is lower case
    CHECK(fxLetter(atari::FX_SLIDE_UP) == 'S' && fxLetter(atari::FX_SLIDE_DOWN) == 's');
    CHECK(fxLetter(atari::FX_ARP) == 'A' && fxLetter(atari::FX_AUDCTL) == 'a');
    CHECK(fxLetter(atari::FX_DELAY) == 'D' && fxLetter(atari::FX_RETRIG) == 'R' && fxLetter(atari::FX_VOLSLIDE) == 'L' && fxLetter(atari::FX_PORTA) == 'P');
    CHECK(fxLetter(atari::FX_SPEED) == 'T' && fxLetter(atari::FX_BREAK) == 'B' && fxLetter(atari::FX_CUT) == 'C');
    CHECK(!fxAvailable(atari::FX_VIBRATO, atari::Chip::Tia) && !fxAvailable(atari::FX_AUDCTL, atari::Chip::Tia));      // POKEY only
    CHECK(fxFromLetter('v', atari::Chip::Tia) == 0 && fxFromLetter('a', atari::Chip::Tia) == 0 && fxFromLetter('Q', atari::Chip::Pokey) == 0);
    char name[4];
    noteName(1, name);
    CHECK(!std::strcmp(name, "C-1"));
    noteName(62, name);
    CHECK(!std::strcmp(name, "C#6") || !std::strcmp(name, "C-6") || name[2] == '6');
    noteName(note::kOff, name);
    CHECK(!std::strcmp(name, "==="));
    noteName(0, name);
    CHECK(!std::strcmp(name, "---"));
}

TEST(cells_and_steps_convert_both_ways) {
    int bad = 0;
    for (int note = -1; note <= 62; note++)
        for (int ins = -1; ins < 48; ins += 7)
            for (int vol = -1; vol <= 15; vol += 4) {
                atari::Step s;
                s.note = note;
                s.instrument = ins;
                s.vol = vol;
                s.fx[0] = {atari::FX_ARP, 0x37};
                s.fx[1] = {atari::FX_CUT, 9};
                if (note == 62) s.note = atari::kNoteOff;
                if (!sameStep(toStep(fromStep(s)), s)) bad++;
            }
    CHECK(bad == 0);
    CHECK(fromStep(atari::Step()).empty());
}

TEST(core_songs_survive_the_conversion) {
    int songs = 0;
    for (auto& [name, core] : allCoreSongs()) {
        if (core.patterns.size() > (size_t)kPatternCount) continue;
        Box a, b;
        CHECK(fromCore(core, *a.song));
        atari::Song back;
        CHECK(toCore(*a.song, back));
        CHECK(back.chip == core.chip && back.region == core.region && back.speed == core.speed);
        CHECK(back.playlist.size() == core.playlist.size());
        int bad = 0;
        for (size_t i = 0; i < core.playlist.size() && i < back.playlist.size(); i++) {
            const atari::Pattern& pa = core.patterns[core.playlist[i]];
            const atari::Pattern& pb = back.patterns[back.playlist[i]];
            if (pa.length != pb.length) { bad++; continue; }
            for (int t = 0; t < core.tracks(); t++)
                for (int r = 0; r < pa.length; r++)
                    if (!sameStep(coreStep(core, core.playlist[i], t, r), pb.track[t][(size_t)r])) bad++;
        }
        CHECK(bad == 0);
        CHECK(back.loopTo == core.loopTo);
        songs++;
    }
    CHECK(songs >= 20);
    std::printf("  conversion: %d core songs round-trip\n", songs);
}

TEST(sequencer_plays_core_songs_like_the_reference) {
    int songs = 0;
    long samples = 0;
    for (auto& [name, core] : allCoreSongs()) {
        if (!core.tunes.empty()) continue;      // tunes only exist in the exports: the Tracker plays the whole order list
        if (core.patterns.size() > (size_t)kPatternCount) continue;
        const int frames = core.chip == atari::Chip::Pokey ? 120 : 300;
        const uint32_t perFrame = 735;                                       // 44,100 Hz at 60 frames a second
        const uint32_t n = (uint32_t)frames * perFrame;
        // reference: the core engine with the core sequencer
        atari::Engine ref;
        ref.init(core.chip, core.region, 44100.0, core.releaseFrames, core.pokeyIns.data(), core.pokeyIns.size(), core.tiaIns.data(), core.tiaIns.size());
        struct Ctx { std::unique_ptr<atari::SongSequencer> seq; bool started = false; } ctx;
        ctx.seq.reset(new atari::SongSequencer(core));
        ref.setFrameHook([](void* c, atari::Engine& e) { Ctx* x = (Ctx*)c; if (x->started) x->seq->end(); x->started = true; x->seq->begin(e.bank()); }, &ctx);
        atari::PcmConverter pcm;
        pcm.init(ref.fullScale());
        std::vector<int16_t> want;
        std::vector<int32_t> lvl(128);
        for (uint32_t done = 0; done < n; done += 128) {
            ref.render(lvl.data(), 128);
            for (int i = 0; i < 128; i++) want.push_back(pcm.process(lvl[(size_t)i]));
        }
        // ours: the Atari song, the sequencer and the audio engine
        Box box;
        CHECK(fromCore(core, *box.song));
        AudioEngine ae;
        ae.init(box.song.get(), 44100000);
        ae.playSong(0);
        std::vector<int16_t> got;
        int16_t blk[256];
        for (uint32_t done = 0; done < n; done += 128) {
            ae.fill(blk, 128);
            for (int i = 0; i < 128; i++) got.push_back(blk[2 * i]);
        }
        size_t bad = 0;
        for (size_t i = 0; i < want.size(); i++) bad += want[i] != got[i];
        CHECK(bad == 0);
        if (bad) std::printf("  %s: %zu of %zu samples differ\n", name.c_str(), bad, want.size());
        songs++;
        samples += (long)want.size();
    }
    std::printf("  sequencer: %d songs, %ld samples identical to the core sequencer\n", songs, samples);
}

namespace {
struct Trace {
    std::vector<int> order, pattern, row;
};
// Run the sequencer alone for `frames` frames and record where it is at the start of each.
Trace run(Song& song, int frames, bool pattern, int start = 0, int startRow = 0) {
    atari::Engine eng;
    eng.init(song.chip, song.region, 44100.0, 6, song.pokey, kInstrumentCount, song.tia, kInstrumentCount);
    Sequencer seq;
    seq.init(&song);
    if (pattern) seq.playPattern(start, startRow);
    else seq.playSong(start, startRow);
    Trace t;
    for (int f = 0; f < frames; f++) {
        seq.onFrame(eng.bank());
        t.order.push_back(seq.orderPos());
        t.pattern.push_back(seq.pattern());
        t.row.push_back(seq.row());
    }
    return t;
}
}  // namespace

TEST(order_repeats_and_restart) {
    Box box;
    Song& s = *box.song;
    s.speed = 2;
    s.pattern(0).clear(2);                       // 2 rows = 4 frames
    s.pattern(1).clear(3);                       // 3 rows = 6 frames
    s.orderLen = 2;
    s.order[0] = {0, 2};                         // pattern 0 twice
    s.order[1] = {1, 1};
    s.restartPos = 1;                            // the song loops on the second entry
    Trace t = run(s, 4 + 4 + 6 + 6 + 6, false);
    // frames 0-3 pattern 0, 4-7 pattern 0 again (order pos 0), 8-13 pattern 1, then pattern 1 again from the restart position
    for (int f = 0; f < 8; f++) CHECK(t.pattern[(size_t)f] == 0 && t.order[(size_t)f] == 0);
    for (int f = 8; f < 14; f++) CHECK(t.pattern[(size_t)f] == 1 && t.order[(size_t)f] == 1);
    for (int f = 14; f < 20; f++) CHECK(t.pattern[(size_t)f] == 1 && t.order[(size_t)f] == 1);
    CHECK(t.row[0] == 0 && t.row[2] == 1 && t.row[4] == 0 && t.row[6] == 1);
}

TEST(break_and_tempo_effects) {
    Box box;
    Song& s = *box.song;
    s.speed = 4;
    s.pattern(0).clear(8);
    s.pattern(0).cells[2][0].fx[0] = {atari::FX_BREAK, 0};        // pattern 0 ends after row 2
    s.pattern(1).clear(4);
    s.pattern(1).cells[0][1].fx[0] = {atari::FX_SPEED, 2};        // tempo 2 frames per row, from the first row of pattern 1
    s.orderLen = 2;
    s.order[0] = {0, 1};
    s.order[1] = {1, 1};
    Trace t = run(s, 12 + 8 + 4, false);
    for (int f = 0; f < 12; f++) CHECK(t.pattern[(size_t)f] == 0 && t.row[(size_t)f] == f / 4);   // 3 rows of 4 frames
    for (int f = 12; f < 20; f++) CHECK(t.pattern[(size_t)f] == 1 && t.row[(size_t)f] == (f - 12) / 2);   // 4 rows of 2 frames
    CHECK(t.pattern[20] == 0);                                   // restart position 0
}

TEST(pattern_mode_loops_one_pattern_and_starts_on_a_row) {
    Box box;
    Song& s = *box.song;
    s.speed = 3;
    s.pattern(5).clear(4);
    Trace t = run(s, 30, true, 5, 2);
    CHECK(t.pattern[0] == 5 && t.row[0] == 2);
    CHECK(t.row[3] == 3 && t.row[6] == 0 && t.row[9] == 1);      // row 3, wraps to row 0 of the same pattern
    for (size_t f = 0; f < t.pattern.size(); f++) CHECK(t.pattern[f] == 5);
}

TEST(tia_plays_two_tracks_only) {
    Box box;
    Song& s = *box.song;
    s.chip = atari::Chip::Tia;
    s.speed = 5;
    s.pattern(0).clear(4);
    s.pattern(0).cells[0][3].fx[0] = {atari::FX_SPEED, 1};        // on a hidden track: ignored
    s.pattern(0).cells[0][1].fx[0] = {atari::FX_SPEED, 2};        // on track 2: applies
    Trace t = run(s, 8, false);
    CHECK(t.row[0] == 0 && t.row[1] == 0 && t.row[2] == 1 && t.row[4] == 2);   // 2 frames per row, not 1
}

TEST(stop_releases_and_restart_works) {
    Box box;
    Song& s = *box.song;
    s.speed = 2;
    s.pattern(0).clear(4);
    s.pattern(0).cells[0][0].note = 25;
    s.pattern(0).cells[0][0].inst = 1;
    AudioEngine ae;
    ae.init(&s, 44100000);
    ae.playSong(0);
    int16_t blk[256];
    long energy = 0;
    for (int i = 0; i < 40; i++) { ae.fill(blk, 128); for (int k = 0; k < 128; k++) energy += std::abs(blk[2 * k]); }
    CHECK(ae.playing() && energy > 1000);
    ae.stop();
    for (int i = 0; i < 80; i++) ae.fill(blk, 128);               // release tail
    CHECK(!ae.playing());
    long after = 0;
    for (int i = 0; i < 20; i++) { ae.fill(blk, 128); for (int k = 0; k < 128; k++) after += std::abs(blk[2 * k]); }
    CHECK(after == 0);
    ae.playSong(0);
    energy = 0;
    for (int i = 0; i < 20; i++) { ae.fill(blk, 128); for (int k = 0; k < 128; k++) energy += std::abs(blk[2 * k]); }
    CHECK(ae.playing() && energy > 1000);
}

TEST(pads_play_a_voice_and_release) {
    Box box;
    AudioEngine ae;
    ae.init(box.song.get(), 44100000);
    ae.noteOn(2, 30, 1);
    int16_t blk[256];
    long on = 0, off = 0;
    for (int i = 0; i < 20; i++) { ae.fill(blk, 128); for (int k = 0; k < 128; k++) on += std::abs(blk[2 * k]); }
    ae.noteOff(2);
    for (int i = 0; i < 100; i++) ae.fill(blk, 128);
    for (int i = 0; i < 10; i++) { ae.fill(blk, 128); for (int k = 0; k < 128; k++) off += std::abs(blk[2 * k]); }
    CHECK(on > 1000 && off == 0);
    ae.noteOn(3, 30, 1);                                          // TIA would drop a hidden track; POKEY plays all four
    ae.fill(blk, 128);
    ae.hold();                                                     // while held the engine is silent
    ae.fill(blk, 128);
    long held = 0;
    for (int k = 0; k < 128; k++) held += std::abs(blk[2 * k]);
    CHECK(held == 0);
    ae.release();
}

// A song grid row gives each track its own pattern (shorter ones loop) or takes it out. The exported song and the Tracker's own playback
// must agree sample for sample, as they do for plain order lists.
TEST(grid_rows_play_like_their_export) {
    Box box;
    Song& sg = *box.song;
    sg.pattern(0).clear(16);
    sg.pattern(1).clear(8);
    sg.pattern(2).clear(4);
    for (int r = 0; r < 16; r += 2) { sg.pattern(0).cells[r][0].note = uint8_t(20 + r); sg.pattern(0).cells[r][0].inst = 1; sg.pattern(0).cells[r][2].note = uint8_t(30 + r); sg.pattern(0).cells[r][2].inst = 2; }
    for (int r = 0; r < 8; r += 2) { sg.pattern(1).cells[r][1].note = uint8_t(40 + r); sg.pattern(1).cells[r][1].inst = 3; sg.pattern(1).cells[r][3].note = uint8_t(25 + r); sg.pattern(1).cells[r][3].inst = 1; }
    for (int r = 0; r < 4; ++r) { sg.pattern(2).cells[r][0].note = uint8_t(35 + r); sg.pattern(2).cells[r][0].inst = 4; }
    sg.orderLen = 4;
    sg.order[0].pattern = 0;                                        // track 3 of pattern 0 is taken out, track 2 plays the 8-row pattern 1 (loops 2x)
    sg.order[0].cell[1] = 1;
    sg.order[0].cell[2] = OrderEntry::kOut;
    sg.order[1].pattern = 1;                                        // tracks 1 and 3 play patterns 2 (4 rows, loops) and 0 (16 rows: the row is 16 long)
    sg.order[1].cell[0] = 2;
    sg.order[1].cell[2] = 0;
    sg.order[1].cell[1] = OrderEntry::kOut;
    sg.order[2].pattern = 0;                                        // a plain entry in between
    sg.order[2].repeat = 2;
    sg.order[3].pattern = 2;
    sg.order[3].cell[3] = 1;
    sg.restartPos = 0;
    CHECK(sg.entryRows(sg.order[0]) == 16 && sg.entryRows(sg.order[1]) == 16 && sg.entryRows(sg.order[3]) == 8);
    atari::Song core;
    CHECK(toCore(sg, core));
    const uint32_t n = 735u * 16u * 6u * 10u;                       // 10 rounds of rows at speed 6: more than the whole song
    atari::Engine ref;
    ref.init(core.chip, core.region, 44100.0, core.releaseFrames, core.pokeyIns.data(), core.pokeyIns.size(), core.tiaIns.data(), core.tiaIns.size());
    struct Ctx { std::unique_ptr<atari::SongSequencer> seq; bool started = false; } ctx;
    ctx.seq.reset(new atari::SongSequencer(core));
    ref.setFrameHook([](void* c, atari::Engine& e) { Ctx* x = (Ctx*)c; if (x->started) x->seq->end(); x->started = true; x->seq->begin(e.bank()); }, &ctx);
    atari::PcmConverter pcm;
    pcm.init(ref.fullScale());
    std::vector<int16_t> want;
    std::vector<int32_t> lvl(128);
    for (uint32_t done = 0; done < n; done += 128) {
        ref.render(lvl.data(), 128);
        for (int i = 0; i < 128; i++) want.push_back(pcm.process(lvl[(size_t)i]));
    }
    AudioEngine ae;
    ae.init(&sg, 44100000);
    ae.playSong(0);
    std::vector<int16_t> got;
    int16_t blk[256];
    for (uint32_t done = 0; done < n; done += 128) {
        ae.fill(blk, 128);
        for (int i = 0; i < 128; i++) got.push_back(blk[2 * i]);
    }
    size_t bad = 0;
    long energy = 0;
    for (size_t i = 0; i < want.size(); i++) { bad += want[i] != got[i]; energy += std::abs(want[i]); }
    CHECK(bad == 0);
    CHECK(energy > 100000);
    if (bad) std::printf("  grid: %zu of %zu samples differ\n", bad, want.size());
    // the plan the exporters use: one exported pattern per distinct row, plain rows are just their pattern
    CHECK(core.patterns.size() == 4 && core.playlist.size() == 5);
}
