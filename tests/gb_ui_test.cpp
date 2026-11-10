// GB app + UI host tests over fake HALs: boot, the blank first screen, the power-hold bar, the silent audio callback and the
// FT81x display-list budget (2048 words per frame; a 100-word margin below the hardware limit is kept free).
#include <cstdio>
#include <cstring>
#include <vector>
#include <deque>

#include "atw/app.h"
#include "gb/ui/lsdj.h"
#include "atw/midi_in.h"
#include "mem_storage.h"
#include "test.h"
#include "ui/buttons.h"

using namespace atw;
namespace b = ot::btn;

namespace {

// Display that estimates the FT81x display-list words of each frame (CMD_TEXT: 4 words of setup + one VERTEX2II per visible glyph, +2
// when x >= 512; every other call 1 word) and records the first clear colour and the number of frames.
struct CountingDisplay final : ot::IDisplay {
    uint32_t words = 0, maxWords = 0, frames = 0, texts = 0, fills = 0;
    uint8_t clearR = 0, clearG = 0, clearB = 0;
    uint16_t width() const override { return 800; }
    uint16_t height() const override { return 480; }
    void dlStart() override { words = 0; texts = 0; fills = 0; }
    void clearColorRGB(uint8_t r, uint8_t g, uint8_t bl) override { ++words; clearR = r; clearG = g; clearB = bl; }
    void clear(bool, bool, bool) override { ++words; }
    void colorRGB(uint8_t, uint8_t, uint8_t) override { ++words; }
    void colorA(uint8_t) override { ++words; }
    void lineWidth(uint16_t) override { ++words; }
    void pointSize(uint16_t) override { ++words; }
    void scissorXY(uint16_t, uint16_t) override { ++words; }
    void scissorSize(uint16_t, uint16_t) override { ++words; }
    void begin(Prim) override { ++words; ++fills; }
    void end() override { ++words; }
    void vertex2f(int32_t, int32_t) override { ++words; }
    void vertex2ii(uint16_t, uint16_t, uint8_t, uint8_t) override { ++words; }
    void text(int16_t x, int16_t, uint8_t, uint16_t, const char* s) override {
        ++texts;
        words += 4 + (x >= 512 ? 2 : 0);
        for (; *s; ++s)
            if (*s != ' ') words += x >= 512 ? 2 : 1;
    }
    void number(int16_t, int16_t, uint8_t, uint16_t, int32_t) override { words += 10; }
    void swap() override {
        ++words;
        ++frames;
        if (words > maxWords) maxWords = words;
    }
};

struct FakeInput final : ot::IInput {
    std::deque<ot::InputEvent> q;
    bool poll(ot::InputEvent& ev) override {
        if (q.empty()) return false;
        ev = q.front();
        q.pop_front();
        return true;
    }
};

struct FakeClock final : ot::IClock {
    uint32_t ms = 0;
    uint32_t micros() const override { return ms * 1000; }
    uint32_t millis() const override { return ms; }
};

struct FakeAudio final : ot::IAudioOut {
    FillFn fn = nullptr;
    void* ctx = nullptr;
    bool started = false;
    void setCallback(FillFn f, void* c) override { fn = f; ctx = c; }
    bool start() override { started = true; return true; }
    void stop() override { started = false; }
};

bool gMuted = false;
void muteStub(bool on) { gMuted = on; }

static int gLineAmp = 0, gInputSel = -1;                       // a fake line input: a sine of this size, and the source last selected
static const int16_t* fakeLine(void*) {
    static int16_t blk[256];
    static double ph = 0;
    for (int i = 0; i < 128; ++i) { blk[2 * i] = blk[2 * i + 1] = int16_t(std::sin(ph) * gLineAmp); ph += 6.2831853 * 220 / 44117.0; }
    return blk;
}
static bool fakeSelect(void*, uint8_t src, uint8_t) { gInputSel = src; return true; }
static int gFwCalls = 0;
static char gFwName[64];
static bool fwStub(ot::IStorage&, const char* image) { ++gFwCalls; std::snprintf(gFwName, sizeof gFwName, "%s", image); return true; }

struct Rig {
    CountingDisplay display;
    FakeInput input;
    FakeClock clock;
    FakeAudio audio;
    App app;
    explicit Rig(bool withAudio = true, ot::IStorage* st = nullptr) {
        static std::vector<uint8_t> big(3u << 20);
        Platform p;
        p.storage = st;
        p.bigMem = big.data();
        p.bigMemSize = uint32_t(big.size());
        p.display = &display;
        p.input = &input;
        p.clock = &clock;
        p.audio = withAudio ? &audio : nullptr;
        p.muteOutput = &muteStub;
        p.requestFirmwareUpdate = &fwStub;
        p.lineIn = &fakeLine;
        p.selectInput = &fakeSelect;
        gMuted = false;
        CHECK(app.init(p));
        app.boot();
    }
    void loop(int ms = 20) {
        for (int i = 0; i < ms; ++i) {
            ++clock.ms;
            app.loop();
        }
    }
    void push(ot::InputEvent::Type t, uint8_t id) {
        ot::InputEvent e;
        e.type = t;
        e.id = id;
        input.q.push_back(e);
    }
};

}  // namespace

TEST(init_needs_display_input_and_clock) {
    App a;
    Platform p;
    CHECK(!a.init(p));
    CountingDisplay d;
    FakeInput in;
    FakeClock c;
    p.display = &d;
    p.input = &in;
    CHECK(!a.init(p));   // no clock
    p.clock = &c;
    CHECK(a.init(p));
}

TEST(first_screen_is_the_song_screen_on_the_game_boy_lcd) {
    Rig r;
    r.loop(100);
    CHECK(r.display.frames > 0);
    CHECK_EQ(int(r.app.lsdj.screen()), int(gb::Scr::Song));
    CHECK(r.display.texts <= 3);                       // only the lens legend, BATTERY and the ? over F8: the screen itself is one bitmap
    CHECK(r.display.maxWords < 100);                   // and the display list stays tiny
}

TEST(splash_comes_first_then_the_lcd) {
    Rig r;
    r.app.ui.showSplash(300);
    r.loop(100);
    CHECK(r.display.texts > 0);
    r.loop(400);
    CHECK(r.display.texts <= 3);
}

TEST(keys_edit_the_song_and_play_starts_the_player) {
    Rig r;
    r.loop(50);
    ot::InputEvent e;
    auto tap = [&](uint8_t id) { e.type = ot::InputEvent::kButtonDown; e.id = id; r.input.q.push_back(e); e.type = ot::InputEvent::kButtonUp; r.input.q.push_back(e); r.loop(30); };
    tap(b::Insert);                                    // A on an empty song cell inserts a chain
    CHECK_EQ(int(r.app.song.songCell(0, 0)), 0);
    tap(b::Right);
    CHECK_EQ(r.app.lsdj.songCh, 1);
    r.app.song.chainPhrase(0, 0) = 0;                  // give chain 0 a phrase with a note so there is something to play
    r.app.song.note(0, 0) = 25;
    tap(b::Play);
    CHECK(r.app.player.running());
    tap(b::Play);
    CHECK(!r.app.player.running());
}

TEST(audio_is_started_and_silent_until_play) {
    Rig r;
    CHECK(r.audio.started);
    CHECK(r.audio.fn != nullptr);
    int16_t buf[2 * ot::IAudioOut::kBlockFrames];
    std::memset(buf, 0x55, sizeof buf);
    r.audio.fn(buf, ot::IAudioOut::kBlockFrames, r.audio.ctx);
    for (int16_t s : buf) CHECK_EQ(int(s), 0);
}

TEST(app_runs_without_audio_output) {
    Rig r(false);
    r.loop(50);
    r.app.quiesce();
    CHECK(r.display.frames > 0);
}

TEST(power_hold_shows_the_shutdown_bar_and_mutes_then_cancel_restores) {
    Rig r;
    r.loop(50);
    r.push(ot::InputEvent::kButtonDown, b::Power);
    r.loop(100);
    CHECK(r.display.texts > 0);      // "SHUTTING DOWN"
    CHECK(!gMuted);
    r.loop(1300);                    // past 85 % of the hold
    CHECK(gMuted);
    r.push(ot::InputEvent::kButtonUp, b::Power);
    r.loop(50);
    CHECK(!gMuted);
    CHECK(r.display.texts <= 3);
}

TEST(display_list_stays_within_budget) {
    Rig r;
    r.app.ui.showSplash(200);
    r.loop(100);
    r.push(ot::InputEvent::kButtonDown, b::Power);
    r.loop(1000);
    CHECK(r.display.maxWords > 0 && r.display.maxWords <= 1900);
}

TEST(midi_parser_notes_with_running_status) {
    MidiIn m;
    MidiIn::Msg msg;
    CHECK(!m.feed(0x90, msg));
    CHECK(!m.feed(60, msg));
    CHECK(m.feed(100, msg));
    CHECK(msg.type == MidiIn::Type::kNoteOn && msg.note == 60 && msg.velocity == 100);
    CHECK(!m.feed(0xF8, msg));       // a real-time byte in the middle is skipped
    CHECK(!m.feed(62, msg));
    CHECK(m.feed(90, msg));          // running status
    CHECK(msg.type == MidiIn::Type::kNoteOn && msg.note == 62);
    CHECK_EQ(MidiIn::cellNote(60), 37);
}

namespace {
// A one-note song on a channel: chain 0 / phrase 0 on song row 0, the note on step 0 with instrument 0 (a loud pulse), panned as asked.
void oneNote(gb::Song& s, int ch, int note, uint8_t pan) {
    for (int r = 0; r < 256; ++r) for (int c = 0; c < 4; ++c) s.songCell(r, c) = 0xFF;
    for (int c = 0; c < 16; ++c) s.chainPhrase(0, c) = c == 0 ? 0 : 0xFF;
    s.songCell(0, ch) = 0;
    s.note(0, 0) = uint8_t(note);
    s.phraseInstr(0, 0) = 0;
    uint8_t* in = s.instrument(0);
    in[0] = ch == 3 ? 3 : ch == 2 ? 1 : 0;
    in[1] = 0xF0; in[4] = 0x7F; in[5] = 1; in[7] = uint8_t(0x80 | pan);
    s.at(gb::off::kTempo) = 255;
}
}  // namespace

TEST(two_consoles_start_together_and_mix) {
    Rig r;
    oneNote(r.app.song, 0, 25, 1);    // A: pulse 1, left only
    oneNote(r.app.songB, 1, 37, 2);   // B: pulse 2, right only
    r.app.dual.start(0, 0);
    CHECK(r.app.player.running() && r.app.playerB.running());
    int16_t out[2 * 2048];
    r.app.pump();                     // the main loop renders ahead; the audio callback only copies
    r.app.fill(out, 2048);
    int peakL = 0, peakR = 0;
    for (int i = 0; i < 2048; ++i) { peakL = std::max(peakL, std::abs(int(out[2 * i]))); peakR = std::max(peakR, std::abs(int(out[2 * i + 1]))); }
    CHECK(peakL > 500);   // console A is heard on the left
    CHECK(peakR > 500);   // console B on the right
    CHECK(r.app.player.voiceOn(0));
    CHECK(r.app.playerB.voiceOn(1));
    CHECK(!r.app.player.voiceOn(1));   // each console only has its own notes
}

TEST(start_key_starts_both_consoles_and_song_key_switches_screen) {
    Rig r;
    oneNote(r.app.song, 0, 25, 3);
    oneNote(r.app.songB, 2, 30, 3);
    r.app.song.at(gb::off::kSync) = 1;       // both projects on SYNC = LSDJ: the virtual link cable
    r.app.songB.at(gb::off::kSync) = 1;
    r.app.lsdj.key(gb::Key::Start, true);
    r.app.lsdj.key(gb::Key::Start, false);
    CHECK(r.app.player.running());
    CHECK(r.app.playerB.running());
    r.app.lsdjB.key(gb::Key::Start, true);   // Start on the other console stops everything
    CHECK(!r.app.dual.running());
}

TEST(linked_session_saves_and_loads_as_two_files_and_exports_a_wav) {
    MemStorage st;
    Rig r(true, &st);
    oneNote(r.app.song, 0, 25, 3);
    oneNote(r.app.songB, 3, 49, 3);
    CHECK(r.app.saveLinked("DEMO"));
    CHECK(st.files.count("GB/Songs/DEMO_A.lsdsng") == 1);
    CHECK(st.files.count("GB/Songs/DEMO_B.lsdsng") == 1);
    r.app.songB.songCell(0, 3) = 0xFF;
    CHECK(r.app.loadLinked("DEMO"));
    CHECK_EQ(int(r.app.songB.songCell(0, 3)), 0);
    CHECK(r.app.exportWav("GB/out.wav", 22050, 10));
    const auto& w = st.files["GB/out.wav"];
    CHECK(w.size() > 44u && std::memcmp(w.data(), "RIFF", 4) == 0);
    CHECK(!r.app.dual.running());
}

TEST(wav_export_stops_when_the_song_loops) {
    MemStorage st;
    Rig r(true, &st);
    oneNote(r.app.song, 0, 25, 3);
    for (int r2 = 0; r2 < 256; ++r2) r.app.songB.songCell(r2, 0) = 0xFF;   // console B stays silent
    r.app.song.at(gb::off::kTempo) = 255;
    CHECK(r.app.exportWav("GB/loop.wav", 22050, 600));
    // one pass is 16 steps of 6 ticks at 102 ticks/s (about 0.94 s): far less than the 600 s cap
    CHECK(st.files["GB/loop.wav"].size() < 22050u * 4u * 3u);
    CHECK(st.files["GB/loop.wav"].size() > 22050u * 4u / 2u);
}

TEST(project_screen_saves_names_and_exports) {
    MemStorage st;
    Rig r(true, &st);
    oneNote(r.app.song, 0, 25, 3);
    r.app.lsdj.show(gb::Scr::Project);
    auto tap = [&](gb::Key k) { r.app.lsdj.key(k, true); r.app.lsdj.key(k, false); };
    for (int i = 0; i < 10; ++i) tap(gb::Key::Down);           // SAVE SONG
    tap(gb::Key::A);
    CHECK(std::strcmp(r.app.lsdj.status(), "SAVED") == 0);
    CHECK(st.files.count("GB/Saves/A.srm") == 1);              // the song went into Game Boy A's bank under its project name
    tap(gb::Key::Down); tap(gb::Key::Down); tap(gb::Key::Down); tap(gb::Key::A);   // EXPORT WAV (after LOAD and NEW SONG)
    CHECK(std::strcmp(r.app.lsdj.status(), "WAV DONE") == 0);
    CHECK(st.files["GB/Export/SONG.wav"].size() > 44u);
    tap(gb::Key::Down); tap(gb::Key::A);                       // EXPORT LSDJ
    CHECK(std::strcmp(r.app.lsdj.status(), "EXPORT DONE") == 0);
    CHECK(st.files.count("GB/Export/SONGA.lsdsng") == 1 && st.files.count("GB/Export/PTSDJ_B.srm") == 1);
}

namespace {
void tapKey(gb::Lsdj& l, gb::Key k) { l.key(k, true); l.key(k, false); }
void holdTap(gb::Lsdj& l, gb::Key mod, gb::Key k) { l.key(mod, true); l.key(k, true); l.key(k, false); l.key(mod, false); }
int sampleOf(gb::Song& s, int wave, int i) { const uint8_t b = s.wave(wave)[(i & 31) >> 1]; return (i & 1) ? (b & 15) : (b >> 4); }
}  // namespace

TEST(wave_screen_edits_samples_like_lsdj) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    for (int i = 0; i < 16; ++i) r.app.song.wave(0)[i] = 0x55;
    l.show(gb::Scr::Wave);
    tapKey(l, gb::Key::Up);                                   // the cursor starts on sample 1; Up raises the dot (value - 1)
    CHECK_EQ(sampleOf(r.app.song, 0, 1), 4);
    tapKey(l, gb::Key::Right); tapKey(l, gb::Key::Down);      // sample 2, value + 1
    CHECK_EQ(sampleOf(r.app.song, 0, 2), 6);
    holdTap(l, gb::Key::A, gb::Key::Up);                      // A + up flips the sample: 15 - 6
    CHECK_EQ(sampleOf(r.app.song, 0, 2), 9);
    holdTap(l, gb::Key::A, gb::Key::Right);                   // A + right draws: the next sample takes this value
    CHECK_EQ(l.waveCol, 2);
    CHECK_EQ(sampleOf(r.app.song, 0, 3), 9);
    holdTap(l, gb::Key::B, gb::Key::Right);                   // B + right changes the wave
    CHECK_EQ(l.waveNo, 1);
    holdTap(l, gb::Key::B, gb::Key::Up);                      // B + up: +16
    CHECK_EQ(l.waveNo, 17);
}

TEST(wave_screen_copies_and_pastes_a_wave) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    for (int i = 0; i < 16; ++i) { r.app.song.wave(0)[i] = uint8_t(i * 17); r.app.song.wave(1)[i] = 0; }
    l.show(gb::Scr::Wave);
    tapKey(l, gb::Key::B);                                    // a lone B tap copies
    holdTap(l, gb::Key::B, gb::Key::Right);                   // go to wave 1
    holdTap(l, gb::Key::Select, gb::Key::A);                  // Select + A pastes
    for (int i = 0; i < 16; ++i) CHECK_EQ(int(r.app.song.wave(1)[i]), i * 17);
}

TEST(synth_screen_edits_parameters_with_lsdj_ranges) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    l.show(gb::Scr::Synth);
    uint8_t* p = r.app.song.synth(0);
    for (int i = 0; i < 16; ++i) p[i] = 0;
    p[5] = 0x10; p[6] = 0xFF; p[9] = 0x10; p[10] = 0xFF;
    holdTap(l, gb::Key::A, gb::Key::Right);                   // SIGNAL + 1
    CHECK_EQ(int(p[0]), 1);
    tapKey(l, gb::Key::Down); holdTap(l, gb::Key::A, gb::Key::Right);   // FILTER + 1
    CHECK_EQ(int(p[1]), 1);
    tapKey(l, gb::Key::Down); holdTap(l, gb::Key::A, gb::Key::Right);   // DIST
    CHECK_EQ(int(p[3]), 1);
    tapKey(l, gb::Key::Down); holdTap(l, gb::Key::A, gb::Key::Right);   // PHASE mode
    CHECK_EQ(int(p[4]), 1);
    tapKey(l, gb::Key::Down); holdTap(l, gb::Key::A, gb::Key::Up);      // VOLUME, end column: +16
    CHECK_EQ(int(p[9]), 0x20);
    tapKey(l, gb::Key::Left); holdTap(l, gb::Key::A, gb::Key::Right);   // start column: +1
    CHECK_EQ(int(p[5]), 0x11);
    tapKey(l, gb::Key::Down); holdTap(l, gb::Key::A, gb::Key::Right);   // CUTOFF start already FF: stays
    CHECK_EQ(int(p[6]), 0xFF);
    tapKey(l, gb::Key::Down); holdTap(l, gb::Key::A, gb::Key::Up);      // Q start: +2 in the high nibble
    CHECK_EQ(int(p[2]), 0x20);
    for (int i = 0; i < 2; ++i) tapKey(l, gb::Key::Down);               // VSHIFT, LIMIT
    holdTap(l, gb::Key::A, gb::Key::Left);                              // LIMIT wraps
    CHECK_EQ(int(p[13]), 0xFF);
    holdTap(l, gb::Key::B, gb::Key::Right);                             // next synth
    CHECK_EQ(l.synth, 1);
}

TEST(synth_edit_regenerates_frames_when_the_rom_is_available) {
    FILE* f = std::fopen("../reference/oracle/run/lsdj.gb", "rb");
    if (!f) return;
    static std::vector<uint8_t> rom(1u << 20);
    rom.resize(std::fread(rom.data(), 1, rom.size(), f));
    std::fclose(f);
    Rig r;
    r.app.synthGen.setRom(rom.data(), rom.size());
    gb::Lsdj& l = r.app.lsdj;
    l.show(gb::Scr::Synth);
    holdTap(l, gb::Key::A, gb::Key::Right);                   // square
    CHECK_EQ(int(r.app.song.wave(0)[0]), 0x74);
    CHECK_EQ(int(r.app.song.wave(5)[1]), 0x54);
}

TEST(boot_loads_the_song_each_console_saved_last_in_steps_and_clears_the_banner) {
    MemStorage st;
    {
        Rig a(true, &st);
        oneNote(a.app.song, 0, 25, 3);
        oneNote(a.app.songB, 1, 30, 3);
        a.app.lsdj.setName("A");
        a.app.lsdjB.setName("B");
        CHECK(a.app.saveToBank(0));
        CHECK(a.app.saveToBank(1));
    }
    Rig r(true, &st);                                     // boot() only announces; nothing is loaded yet
    CHECK_EQ(int(r.app.songB.songCell(0, 1)), 0xFF);
    r.loop(10);                                           // a few main-loop passes do the steps (no ROM on this card: it ends at once)
    CHECK_EQ(int(r.app.song.songCell(0, 0)), 0);
    CHECK_EQ(int(r.app.songB.songCell(0, 1)), 0);
}

TEST(file_screen_lists_songs_and_loads_one_into_the_console) {
    MemStorage st;
    Rig r(true, &st);
    oneNote(r.app.song, 0, 25, 3);
    CHECK(r.app.saveSong("GB/Songs/BETA.lsdsng", "BETA", 0));
    oneNote(r.app.song, 2, 30, 3);
    CHECK(r.app.saveSong("GB/Songs/ALPHA.lsdsng", "ALPHA", 0));
    gb::Lsdj& l = r.app.lsdjB;                                // load into console B
    l.show(gb::Scr::Project);
    for (int i = 0; i < 11; ++i) tapKey(l, gb::Key::Down);    // LOAD SONG
    tapKey(l, gb::Key::A);
    CHECK(l.screen() == gb::Scr::File);
    CHECK_EQ(l.fileCount, 3);                                 // NEW SONG, then the two loose files
    tapKey(l, gb::Key::Down);
    tapKey(l, gb::Key::Down);                                 // sorted: ALPHA, BETA -> BETA (note on channel 0)
    tapKey(l, gb::Key::A);
    CHECK(l.screen() == gb::Scr::Project);
    CHECK_EQ(int(r.app.songB.songCell(0, 0)), 0);
    CHECK_EQ(int(r.app.songB.songCell(0, 2)), 0xFF);
}

TEST(shift_insert_mutes_a_channel_and_shift_delete_a_console) {
    gb::Song s; gb::Player p(s); gb::Lsdj ui(s, p);
    ui.key(gb::Key::Select, true); ui.key(gb::Key::A, true); ui.key(gb::Key::A, false); ui.key(gb::Key::Select, false);
    CHECK(p.muted(0)); CHECK(!p.muted(1));
    ui.key(gb::Key::Select, true); ui.key(gb::Key::B, true); ui.key(gb::Key::B, false); ui.key(gb::Key::Select, false);
    CHECK(p.muted(0) && p.muted(1) && p.muted(2) && p.muted(3));
    ui.key(gb::Key::Select, true); ui.key(gb::Key::B, true); ui.key(gb::Key::B, false); ui.key(gb::Key::Select, false);
    CHECK(!p.muted(0) && !p.muted(3));
}

TEST(audio_is_rendered_ahead_and_a_late_block_counts_once) {
    Rig r;
    oneNote(r.app.song, 0, 25, 3);
    r.app.dual.start(0, 0);
    r.app.flushRing();
    int16_t out[2 * 128];
    r.app.fill(out, 128);              // nothing rendered yet: silence and one late block
    for (int i = 0; i < 128; ++i) CHECK_EQ(int(out[2 * i]), 0);
    r.app.pump();
    r.app.fill(out, 128);
    int peak = 0;
    for (int i = 0; i < 128; ++i) peak = std::max(peak, std::abs(int(out[2 * i])));
    CHECK(peak > 0);
}

TEST(kit_files_replace_kit_slots_at_boot) {
    MemStorage st;
    std::vector<uint8_t> bank(gb::kKitBankSize, 0x11);
    bank[0] = 0x60; bank[1] = 0x40; bank[2] = 0x80; bank[3] = 0x40; bank[4] = 0; bank[5] = 0;   // two samples' offsets, then the end
    st.files["GB/Kits/0A_drums.kit"] = bank;
    st.files["GB/Kits/ZZ_bad.kit"] = bank;                                    // not a slot number
    st.files["GB/Kits/05_short.kit"] = std::vector<uint8_t>(100, 0);         // wrong size
    std::vector<uint8_t> notKit(gb::kKitBankSize, 0);
    st.files["GB/Kits/07_junk.kit"] = notKit;                                // right size, not a kit bank
    Rig r(true, &st);
    r.loop(40);
    CHECK_EQ(r.app.kitFilesLoaded(), 1);
    CHECK(r.app.kits.kit(0x0A).data != nullptr);
    CHECK_EQ(r.app.kits.kit(0x0A).data[2], 0x80);
    CHECK(r.app.kits.kit(0x07).data == nullptr);
    CHECK(r.app.kits.kit(0x05).data == nullptr);
}

TEST(without_sync_play_starts_one_console_and_with_sync_the_slave_follows_the_master_tempo) {
    Rig r;
    oneNote(r.app.song, 0, 25, 3);
    oneNote(r.app.songB, 1, 30, 3);
    r.app.song.at(gb::off::kTempo) = 100;
    r.app.songB.at(gb::off::kTempo) = 200;
    r.app.lsdjB.key(gb::Key::Start, true);
    r.app.lsdjB.key(gb::Key::Start, false);
    CHECK(r.app.playerB.running());
    CHECK(!r.app.player.running());               // SYNC off: Play on B plays B alone
    r.app.lsdjB.key(gb::Key::Start, true);
    r.app.lsdjB.key(gb::Key::Start, false);
    r.app.song.at(gb::off::kSync) = 1;
    r.app.songB.at(gb::off::kSync) = 1;
    r.app.lsdj.key(gb::Key::Start, true);
    r.app.lsdj.key(gb::Key::Start, false);
    int16_t out[2 * 512];
    r.app.dual.render(out, 512, 44100);
    CHECK(r.app.player.running() && r.app.playerB.running());
    CHECK_EQ(r.app.playerB.tempo(), 100);         // A started: B follows A's tempo
}

namespace {
void keyTap(Rig& r, uint8_t id) { r.push(ot::InputEvent::kButtonDown, id); r.push(ot::InputEvent::kButtonUp, id); r.loop(3); }
void padEvent(Rig& r, ot::InputEvent::Type t, int pad) {
    ot::InputEvent e;
    e.type = t;
    e.id = uint8_t(pad);
    e.velocity = 100;
    r.input.q.push_back(e);
    r.loop(3);
}
}  // namespace

TEST(page_keys_jump_to_pages_and_shift_song_swaps_consoles) {
    Rig r;
    keyTap(r, ot::btn::Note);         CHECK(r.app.lsdj.screen() == gb::Scr::Phrase);
    keyTap(r, ot::btn::Instr);        CHECK(r.app.lsdj.screen() == gb::Scr::Instr);
    keyTap(r, ot::btn::InstrParams);  CHECK(r.app.lsdj.screen() == gb::Scr::Table);
    keyTap(r, ot::btn::Pattern);      CHECK(r.app.lsdj.screen() == gb::Scr::Chain);
    keyTap(r, ot::btn::Fx1);          CHECK(r.app.lsdj.screen() == gb::Scr::Synth);
    keyTap(r, ot::btn::Fx2);          CHECK(r.app.lsdj.screen() == gb::Scr::Wave);
    keyTap(r, ot::btn::Config);       CHECK(r.app.lsdj.screen() == gb::Scr::Project);
    keyTap(r, ot::btn::Performance);  CHECK(r.app.lsdj.screen() == gb::Scr::Groove);
    keyTap(r, ot::btn::Song);         CHECK(r.app.lsdj.screen() == gb::Scr::Song);
    r.push(ot::InputEvent::kButtonDown, ot::btn::Shift);
    keyTap(r, ot::btn::Song);                                  // Shift + Song: console B on screen
    r.push(ot::InputEvent::kButtonUp, ot::btn::Shift);
    r.loop(3);
    keyTap(r, ot::btn::Note);
    CHECK(r.app.lsdjB.screen() == gb::Scr::Phrase);
    CHECK(r.app.lsdj.screen() == gb::Scr::Song);
}

TEST(a_pad_plays_the_cursor_channel_without_a_song_and_releasing_it_cuts_the_note) {
    Rig r;
    uint8_t* in = r.app.song.instrument(0);
    in[0] = 0; in[1] = 0xF0; in[4] = 0x7F; in[5] = 1; in[7] = 3;
    r.app.lsdj.songCh = 1;                                      // cursor on PU2
    padEvent(r, ot::InputEvent::kPadDown, 12);
    CHECK(r.app.player.voiceOn(1));
    CHECK(!r.app.player.voiceOn(0));
    CHECK(r.app.dual.live());
    r.loop(60);
    int16_t out[2 * 256];
    r.app.fill(out, 256);
    int peak = 0;
    for (int i = 0; i < 256; ++i) peak = std::max(peak, std::abs(int(out[2 * i])));
    CHECK(peak > 0);
    padEvent(r, ot::InputEvent::kPadUp, 12);
    CHECK(!r.app.player.voiceOn(1));
}

TEST(phrase_rec_mode_loops_the_phrase_and_records_pads_only_while_playing) {
    Rig r;
    for (int i = 0; i < 16; ++i) { r.app.song.note(0, i) = 0; r.app.song.phraseInstr(0, i) = 0xFF; }
    uint8_t* in = r.app.song.instrument(3);
    in[0] = 0; in[1] = 0xF0; in[4] = 0x7F; in[5] = 1; in[7] = 3;
    r.app.song.at(gb::off::kTempo) = 255;
    keyTap(r, ot::btn::Note);                                  // Phrase page, phrase 00, channel PU1
    r.app.lsdj.phrase = 0; r.app.lsdj.songCh = 0;
    padEvent(r, ot::InputEvent::kPadDown, 12);                 // Rec mode off: the pad inserts a note at the cursor as before
    padEvent(r, ot::InputEvent::kPadUp, 12);
    CHECK_EQ(int(r.app.song.note(0, 0)), 25);
    r.app.song.note(0, 0) = 0;
    r.app.lsdj.step = 0;
    keyTap(r, ot::btn::Rec);                                   // Rec mode on
    CHECK(r.app.lsdj.recMode());
    r.app.lsdj.encoder(3);                                     // the jog wheel picks the instrument
    CHECK_EQ(r.app.lsdj.instr, 1);
    r.app.lsdj.encoder(2);
    CHECK_EQ(r.app.lsdj.instr, 2);
    r.app.lsdj.encoder(1);
    CHECK_EQ(r.app.lsdj.instr, 3);
    padEvent(r, ot::InputEvent::kPadDown, 12);                 // not playing: audition only, nothing written
    padEvent(r, ot::InputEvent::kPadUp, 12);
    int notes = 0;
    for (int i = 0; i < 16; ++i) if (r.app.song.note(0, i)) ++notes;
    CHECK_EQ(notes, 0);
    keyTap(r, ot::btn::Play);                                  // Rec mode + Play: this phrase loops on PU1, recording
    r.loop(40);
    CHECK(r.app.lsdj.recording());
    CHECK(r.app.player.running());
    CHECK(!r.app.playerB.running());
    padEvent(r, ot::InputEvent::kPadDown, 12);
    notes = 0;
    for (int i = 0; i < 16; ++i) if (r.app.song.note(0, i) == 25) { ++notes; CHECK_EQ(int(r.app.song.phraseInstr(0, i)), 3); }
    CHECK_EQ(notes, 1);
    keyTap(r, ot::btn::Play);                                  // Play again stops and stops recording
    CHECK(!r.app.lsdj.recording());
    CHECK(!r.app.player.running());
}

TEST(pads_and_play_on_the_synth_and_wave_pages_never_start_the_song) {
    Rig r;
    oneNote(r.app.song, 0, 25, 3);                             // a song with a note on PU1
    keyTap(r, ot::btn::Play);
    r.loop(20);
    keyTap(r, ot::btn::Play);                                  // played and stopped: the sequencer must stay stopped
    CHECK(!r.app.player.running());
    keyTap(r, ot::btn::Fx2);                                   // Wave page
    r.app.lsdj.waveNo = 2;
    padEvent(r, ot::InputEvent::kPadDown, 20);
    r.loop(80);
    CHECK(r.app.player.voiceOn(2));
    CHECK(!r.app.player.voiceOn(0));                           // the song's PU1 note did not play
    padEvent(r, ot::InputEvent::kPadUp, 20);
    keyTap(r, ot::btn::Play);                                  // Play on the wave page auditions the wave, not the song
    CHECK(r.app.player.voiceOn(2));
    CHECK(!r.app.player.running());
    CHECK(!r.app.player.voiceOn(0));
    keyTap(r, ot::btn::Play);
    CHECK(!r.app.player.voiceOn(2));
}

TEST(export_lsdj_fills_each_games_bank_and_copies_it_as_srm) {
    MemStorage st;
    Rig r(true, &st);
    oneNote(r.app.song, 0, 25, 3);
    oneNote(r.app.songB, 3, 49, 3);
    r.app.song.at(gb::off::kSync) = 1;
    r.app.songB.at(gb::off::kSync) = 1;
    CHECK(r.app.exportFiles("S01"));
    CHECK(st.files.count("GB/Export/S01_A.lsdsng") == 1);
    CHECK(st.files.count("GB/Export/S01_B.lsdsng") == 1);
    CHECK_EQ(int(st.files["GB/Saves/A.srm"].size()), 0x20000);
    CHECK_EQ(int(st.files["GB/Saves/B.srm"].size()), 0x20000);
    CHECK(st.files["GB/Export/S01_A.srm"] == st.files["GB/Saves/A.srm"]);
    CHECK(st.files["GB/Export/S01_B.srm"] == st.files["GB/Saves/B.srm"]);
    CHECK_EQ(int(st.files["GB/Export/S01_B.srm"][gb::off::kSync]), 1);          // the working song at the start of the file keeps the SYNC setting
    gb::Song back;
    CHECK(back.readSav(st.files["GB/Export/S01_B.srm"].data(), st.files["GB/Export/S01_B.srm"].size()));
    CHECK_EQ(int(back.songCell(0, 3)), 0);
}

TEST(save_song_files_it_in_the_consoles_own_bank_and_load_lists_it) {
    MemStorage st;
    Rig r(true, &st);
    oneNote(r.app.song, 0, 25, 3);
    oneNote(r.app.songB, 3, 49, 3);
    r.app.lsdj.setName("TUNE");
    r.app.lsdjB.setName("DRUMS");
    CHECK(r.app.saveToBank(0));
    CHECK(r.app.saveToBank(1));
    CHECK_EQ(int(st.files.count("GB/Saves/A.srm")), 1);
    CHECK_EQ(int(st.files.count("GB/Saves/B.srm")), 1);
    r.app.lsdj.setName("TUNE2");                                // a second song in the same bank
    r.app.song.note(0, 0) = 30;
    CHECK(r.app.saveToBank(0));
    r.app.lsdj.setName("TUNE");
    r.app.song.note(0, 0) = 31;
    CHECK(r.app.saveToBank(0));                                 // the same name replaces
    gb::Lsdj& l = r.app.lsdj;
    l.openFiles();
    CHECK_EQ(l.fileCount, 3);                                   // NEW SONG, TUNE and TUNE2; DRUMS is on the other Game Boy
    r.app.song.note(0, 0) = 1;
    l.fileSel = 1;
    keyTap(r, ot::btn::Insert);                                 // load the first entry into this console
    CHECK_EQ(int(r.app.song.note(0, 0)), 31);
    CHECK(std::strcmp(l.name(), "TUNE") == 0);
    r.app.lsdjB.openFiles();
    CHECK_EQ(r.app.lsdjB.fileCount, 2);
}

TEST(loose_lsdsng_files_are_listed_after_the_bank_and_saved_into_it) {
    MemStorage st;
    Rig r(true, &st);
    oneNote(r.app.song, 0, 25, 3);
    r.app.lsdj.setName("OLD");
    CHECK(r.app.saveSong("GB/Songs/OLD.lsdsng", "OLD", 0));
    r.app.lsdj.setName("NEW");
    CHECK(r.app.saveToBank(0));
    r.app.lsdj.openFiles();
    CHECK_EQ(r.app.lsdj.fileCount, 3);                          // NEW SONG, NEW (bank) then +OLD (file)
    r.app.song.note(0, 0) = 1;
    r.app.lsdj.fileSel = 2;
    keyTap(r, ot::btn::Insert);
    CHECK_EQ(int(r.app.song.note(0, 0)), 25);
    CHECK(std::strcmp(r.app.lsdj.name(), "OLD") == 0);
    CHECK(r.app.saveToBank(0));                                 // now it is in the bank too
    r.app.lsdj.openFiles();
    CHECK_EQ(r.app.lsdj.fileCount, 4);
}

TEST(a_bank_song_can_be_erased_with_select_delete_and_a_second_insert) {
    MemStorage st;
    Rig r(true, &st);
    oneNote(r.app.song, 0, 25, 3);
    r.app.lsdj.setName("ONE");
    CHECK(r.app.saveToBank(0));
    r.app.lsdj.setName("TWO");
    CHECK(r.app.saveToBank(0));
    gb::Lsdj& l = r.app.lsdj;
    l.openFiles();
    CHECK_EQ(l.fileCount, 3);
    l.fileSel = 1;
    r.push(ot::InputEvent::kButtonDown, ot::btn::Shift);
    keyTap(r, ot::btn::Delete);
    r.push(ot::InputEvent::kButtonUp, ot::btn::Shift);
    CHECK_EQ(l.fileCount, 3);                                   // asked first
    keyTap(r, ot::btn::Insert);
    CHECK_EQ(l.fileCount, 2);
    CHECK(std::strcmp(l.status(), "ERASED") == 0);
}

TEST(a_full_bank_says_so_and_changes_nothing) {
    MemStorage st;
    Rig r(true, &st);
    r.loop(10);                                                 // the start-up steps first (nothing is saved yet)
    oneNote(r.app.song, 0, 25, 3);
    for (int i = 0; i < 32; ++i) {
        char nm[16];
        std::snprintf(nm, sizeof nm, "S%02d", i);
        r.app.lsdj.setName(nm);
        CHECK(r.app.saveToBank(0));
    }
    const std::vector<uint8_t> before = st.files["GB/Saves/A.srm"];
    r.app.lsdj.setName("EXTRA");
    gb::Lsdj& l = r.app.lsdj;
    keyTap(r, ot::btn::Config);
    l.projRow = 10;
    keyTap(r, ot::btn::Insert);
    CHECK(std::strstr(l.status(), "BANK FULL") != nullptr);
    CHECK(st.files["GB/Saves/A.srm"] == before);
}

TEST(wave_screen_pads_preview_the_wave_being_edited) {
    Rig r;
    keyTap(r, ot::btn::Fx2);                                  // Wave screen
    r.app.lsdj.waveNo = 5;
    for (int i = 0; i < 16; ++i) r.app.song.wave(5)[i] = uint8_t(i < 8 ? 0xFF : 0x00);
    padEvent(r, ot::InputEvent::kPadDown, 20);
    CHECK(r.app.player.voiceOn(2));
    r.loop(60);
    int16_t out[2 * 256];
    r.app.fill(out, 256);
    int peak = 0;
    for (int i = 0; i < 256; ++i) peak = std::max(peak, std::abs(int(out[2 * i])));
    CHECK(peak > 0);
}

TEST(hand_drawn_wave_asks_before_a_synth_edit_overwrites_it) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    l.show(gb::Scr::Wave);
    l.waveNo = 0x23;                                          // a wave of synth 2
    holdTap(l, gb::Key::A, gb::Key::Up);                      // drawing marks synth 2 as hand-drawn
    CHECK_EQ(int(r.app.song.at(gb::off::kHandDrawn + 1)), 1 << 2);
    l.synth = 2;
    l.show(gb::Scr::Synth);
    l.synthRow = 0;
    const uint8_t before = r.app.song.synth(2)[0];
    holdTap(l, gb::Key::A, gb::Key::Right);
    CHECK(l.promptOpen());
    CHECK_EQ(int(r.app.song.synth(2)[0]), int(before));       // nothing applied yet
    tapKey(l, gb::Key::A);                                    // NO is preselected: the question closes, the flag stays
    CHECK(!l.promptOpen());
    CHECK_EQ(int(r.app.song.at(gb::off::kHandDrawn + 1)), 1 << 2);
    holdTap(l, gb::Key::A, gb::Key::Right);
    tapKey(l, gb::Key::Left);                                 // YES
    tapKey(l, gb::Key::A);
    CHECK(!l.promptOpen());
    CHECK_EQ(int(r.app.song.at(gb::off::kHandDrawn + 1)), 0); // the flag is cleared; the edit itself still has to be made again
    CHECK_EQ(int(r.app.song.synth(2)[0]), int(before));
    holdTap(l, gb::Key::A, gb::Key::Right);
    CHECK(int(r.app.song.synth(2)[0]) != int(before));
}

TEST(stopping_one_console_silences_it_so_no_note_is_left_hanging) {
    Rig r;
    oneNote(r.app.song, 0, 25, 3);
    r.app.song.instrument(0)[4] = 0x7F;
    r.app.dual.start(0, 0);                                     // A plays a held note
    int16_t out[2 * 512];
    for (int i = 0; i < 20; ++i) r.app.dual.render(out, 512, 44100);
    r.app.dual.startOne(1, 0);                                  // B alone (it has no song): A must fall silent for good
    int peak = 0;
    for (int i = 0; i < 40; ++i) {
        r.app.dual.render(out, 512, 44100);
        if (i >= 10) for (int k = 0; k < 1024; ++k) peak = std::max(peak, std::abs(int(out[k])));
    }
    CHECK(peak < 40);
}

TEST(pads_follow_the_instrument_type_so_wave_and_kit_instruments_sound_on_the_wave_channel) {
    Rig r;
    uint8_t* w = r.app.song.instrument(5);
    w[0] = 1; w[1] = 0xA8; w[2] = 0x0F; w[3] = 0; w[7] = 3;      // a wave instrument
    keyTap(r, ot::btn::Note);
    r.app.lsdj.songCh = 0;                                      // cursor on PU1, instrument is a wave one
    keyTap(r, ot::btn::Rec);
    r.app.lsdj.instr = 5;
    padEvent(r, ot::InputEvent::kPadDown, 12);
    CHECK(r.app.player.voiceOn(2));
    CHECK(!r.app.player.voiceOn(0));
    padEvent(r, ot::InputEvent::kPadUp, 12);
}

TEST(rec_play_loops_the_phrase_while_the_rest_of_the_song_plays_behind_it) {
    Rig r;
    oneNote(r.app.song, 1, 30, 3);                             // PU2 plays a note at song row 0
    for (int i = 0; i < 16; ++i) { r.app.song.note(7, i) = 0; r.app.song.phraseInstr(7, i) = 0xFF; }
    keyTap(r, ot::btn::Note);
    r.app.lsdj.phrase = 7; r.app.lsdj.songCh = 0; r.app.lsdj.songRow = 0;
    keyTap(r, ot::btn::Rec);
    keyTap(r, ot::btn::Play);
    r.loop(10);
    CHECK(r.app.player.running());
    CHECK(r.app.player.seq().active(0) && r.app.player.seq().phrase(0) == 7);   // PU1 loops the phrase on screen
    CHECK(r.app.player.seq().active(1));                                         // PU2 plays the song behind it
    keyTap(r, ot::btn::Play);
    CHECK(!r.app.player.running());
}

TEST(instrument_names_are_typed_on_the_keyboard_and_stored_in_the_song) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    keyTap(r, ot::btn::Instr);
    l.instr = 2; l.instrRow = 0;
    l.key(gb::Key::A, true); l.key(gb::Key::A, false);          // Insert on the NAME row opens the keyboard
    CHECK(l.nameEditing());
    auto tap = [&](gb::Key k) { l.key(k, true); l.key(k, false); };
    tap(gb::Key::Down);                                         // row of A..J
    tap(gb::Key::A);                                            // A
    tap(gb::Key::Right); tap(gb::Key::Right); tap(gb::Key::A);   // C
    l.key(gb::Key::Start, true); l.key(gb::Key::Start, false);  // Play confirms
    CHECK(!l.nameEditing());
    CHECK_EQ(int(r.app.song.at(gb::off::kInstrNames + 2 * 5)), 'A');
    CHECK_EQ(int(r.app.song.at(gb::off::kInstrNames + 2 * 5 + 1)), 'C');
    CHECK_EQ(int(r.app.song.at(gb::off::kInstrNames + 2 * 5 + 2)), 0);
}

TEST(project_name_is_typed_and_names_the_saved_file) {
    MemStorage st;
    Rig r(true, &st);
    gb::Lsdj& l = r.app.lsdj;
    l.show(gb::Scr::Project);
    l.projRow = 0;
    l.key(gb::Key::A, true); l.key(gb::Key::A, false);
    CHECK(l.nameEditing());
    l.key(gb::Key::Down, true); l.key(gb::Key::Down, false);
    l.key(gb::Key::Down, true); l.key(gb::Key::Down, false);
    l.key(gb::Key::A, true); l.key(gb::Key::A, false);          // K
    l.key(gb::Key::Start, true); l.key(gb::Key::Start, false);
    CHECK(std::strcmp(l.name(), "K") == 0);
    l.projRow = 10;
    l.key(gb::Key::A, true); l.key(gb::Key::A, false);
    CHECK_EQ(int(st.files["GB/Saves/A.srm"].size()), 0x20000);
    l.openFiles();
    CHECK_EQ(l.fileCount, 2);
}

TEST(shift_insert_arrows_select_cells_copy_and_shift_copy_paste_on_the_same_page_only) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    for (int c = 0; c < 4; ++c) for (int rr = 0; rr < 8; ++rr) r.app.song.songCell(rr, c) = 0xFF;
    r.app.song.songCell(0, 0) = 3; r.app.song.songCell(1, 0) = 4; r.app.song.songCell(2, 0) = 5;
    keyTap(r, ot::btn::Song);
    l.songRow = 0; l.songCh = 0;
    r.push(ot::InputEvent::kButtonDown, ot::btn::Shift);        // Shift + Insert + Down, Down: three cells
    r.push(ot::InputEvent::kButtonDown, ot::btn::Insert);
    r.push(ot::InputEvent::kButtonDown, ot::btn::Down); r.push(ot::InputEvent::kButtonUp, ot::btn::Down);
    r.push(ot::InputEvent::kButtonDown, ot::btn::Down); r.push(ot::InputEvent::kButtonUp, ot::btn::Down);
    r.push(ot::InputEvent::kButtonUp, ot::btn::Insert);
    r.push(ot::InputEvent::kButtonUp, ot::btn::Shift);
    r.loop(3);
    CHECK(l.selecting());
    CHECK(!r.app.player.muted(0));                              // selecting is not the mute shortcut
    keyTap(r, ot::btn::Copy);
    l.songRow = 4; l.songCh = 2;
    r.app.lsdj.show(gb::Scr::Song);
    r.push(ot::InputEvent::kButtonDown, ot::btn::Shift);
    r.push(ot::InputEvent::kButtonDown, ot::btn::Copy); r.push(ot::InputEvent::kButtonUp, ot::btn::Copy);
    r.push(ot::InputEvent::kButtonUp, ot::btn::Shift);
    r.loop(3);
    int found = 0;
    for (int rr = 4; rr < 7; ++rr) if (r.app.song.songCell(rr, 2) != 0xFF) ++found;   // pasted as three chains in channel 3 (clones: new numbers)
    CHECK_EQ(found, 3);
    keyTap(r, ot::btn::Pattern);                                // another page: the clipboard does not apply there
    uint8_t before[32];
    for (int i = 0; i < 16; ++i) { before[i] = r.app.song.chainPhrase(l.chain, i); before[16 + i] = r.app.song.chainTrans(l.chain, i); }
    r.push(ot::InputEvent::kButtonDown, ot::btn::Shift);
    r.push(ot::InputEvent::kButtonDown, ot::btn::Copy); r.push(ot::InputEvent::kButtonUp, ot::btn::Copy);
    r.push(ot::InputEvent::kButtonUp, ot::btn::Shift);
    r.loop(3);
    for (int i = 0; i < 16; ++i) CHECK(r.app.song.chainPhrase(l.chain, i) == before[i] && r.app.song.chainTrans(l.chain, i) == before[16 + i]);
}

TEST(master_and_slave_sync_follow_the_master_tempo_and_the_master_page_key_splits_the_screen) {
    Rig r;
    gb::Lsdj& a = r.app.lsdj;
    gb::Lsdj& b2 = r.app.lsdjB;
    a.show(gb::Scr::Project); b2.show(gb::Scr::Project);
    a.projRow = 3; b2.projRow = 3;
    auto edit = [&](gb::Lsdj& l, gb::Key k) { l.key(gb::Key::A, true); l.key(k, true); l.key(k, false); l.key(gb::Key::A, false); };
    edit(a, gb::Key::Right);                                    // A: OFF -> MASTER
    edit(b2, gb::Key::Right);                                   // B: OFF -> MASTER, so A becomes SLAVE
    CHECK(r.app.song.at(gb::off::kSync) == 1 && r.app.songB.at(gb::off::kSync) == 1);
    edit(b2, gb::Key::Right);                                   // B: MASTER -> SLAVE, A becomes MASTER again
    a.projRow = 1;
    edit(a, gb::Key::Up);                                       // master tempo +16: the slave's project takes it
    CHECK_EQ(int(r.app.songB.at(gb::off::kTempo)), int(r.app.song.at(gb::off::kTempo)));
    const int before = r.app.song.at(gb::off::kTempo);
    b2.projRow = 1;
    edit(b2, gb::Key::Up);                                      // the slave cannot change it
    CHECK_EQ(int(r.app.song.at(gb::off::kTempo)), before);
    keyTap(r, ot::btn::Master);                                 // linked: the Master key splits the screen
    r.loop(10);
    r.push(ot::InputEvent::kButtonDown, 6);                     // F7 held: the jog wheel sets B's level
    r.loop(2);
    ot::InputEvent e; e.type = ot::InputEvent::kEncoder; e.delta = -2; r.input.q.push_back(e);
    r.loop(3);
    r.push(ot::InputEvent::kButtonUp, 6);
    r.loop(2);
    CHECK_EQ(r.app.dual.volume(1), 13);
    CHECK_EQ(r.app.dual.volume(0), 15);
}

TEST(f_keys_pick_the_console_mute_and_solo_tracks_and_set_levels) {
    Rig r;
    r.app.song.at(gb::off::kSync) = 1; r.app.songB.at(gb::off::kSync) = 1;
    keyTap(r, ot::btn::F3);                                     // mutes track 1 of song A
    CHECK(r.app.player.muted(0));
    keyTap(r, ot::btn::F3);
    CHECK(!r.app.player.muted(0));
    r.push(ot::InputEvent::kButtonDown, ot::btn::Shift);
    keyTap(r, ot::btn::F4);                                     // Shift + F4 solos track 2
    r.push(ot::InputEvent::kButtonUp, ot::btn::Shift);
    r.loop(3);
    CHECK(r.app.player.muted(0) && !r.app.player.muted(1) && r.app.player.muted(2) && r.app.player.muted(3));
    keyTap(r, ot::btn::Master);                                 // side by side: F8 is song B, and now selects it
    keyTap(r, ot::btn::F8);                                     // song B active: F5 now mutes track 5
    keyTap(r, ot::btn::F5);
    CHECK(r.app.playerB.muted(2));
    r.push(ot::InputEvent::kButtonDown, ot::btn::F2);           // F2 held + jog: A's level
    r.loop(2);
    ot::InputEvent e; e.type = ot::InputEvent::kEncoder; e.delta = -3; r.input.q.push_back(e);
    r.loop(3);
    r.push(ot::InputEvent::kButtonUp, ot::btn::F2);
    r.loop(2);
    CHECK(r.app.dual.volume(0) < 15);
    CHECK_EQ(r.app.dual.volume(1), 15);
}

TEST(help_opens_with_the_help_key_and_closes_with_delete) {
    Rig r;
    keyTap(r, ot::btn::Help);
    CHECK(r.app.lsdj.screen() == gb::Scr::Help);
    r.app.lsdj.key(gb::Key::Right, true); r.app.lsdj.key(gb::Key::Right, false);
    CHECK_EQ(r.app.lsdj.helpPage, 1);
    r.app.lsdj.key(gb::Key::B, true); r.app.lsdj.key(gb::Key::B, false);
    CHECK(r.app.lsdj.screen() == gb::Scr::Song);
}

TEST(clean_song_data_frees_unreferenced_chains_and_phrases_after_a_second_insert) {
    Rig r;
    gb::Song& s = r.app.song;
    oneNote(s, 0, 25, 3);
    s.note(9, 3) = 40; s.markPhrase(9);                         // a phrase nothing plays
    gb::Lsdj& l = r.app.lsdj;
    l.show(gb::Scr::Project);
    l.projRow = 8;
    l.key(gb::Key::A, true); l.key(gb::Key::A, false);          // asks first
    CHECK_EQ(int(s.note(9, 3)), 40);
    l.key(gb::Key::A, true); l.key(gb::Key::A, false);          // second Insert does it
    CHECK_EQ(int(s.note(9, 3)), 0);
    CHECK_EQ(int(s.note(0, 0)), 25);                            // the phrase the song plays stays
}

TEST(instrument_library_saves_and_loads_an_instrument_with_its_table) {
    MemStorage st;
    Rig r(true, &st);
    gb::Song& s = r.app.song;
    uint8_t* in = s.instrument(4);
    in[0] = 0; in[1] = 0xC3; in[6] = 0x20 | 5;                  // pulse, uses table 5
    s.at(uint16_t(gb::off::kTblEnv + 5 * 16 + 2)) = 0x77;
    s.at(uint16_t(gb::off::kInstrNames + 4 * 5)) = 'Z';
    gb::Lsdj& l = r.app.lsdj;
    l.instr = 4;
    keyTap(r, ot::btn::Instr);
    r.push(ot::InputEvent::kButtonDown, ot::btn::Shift);
    keyTap(r, ot::btn::Instr);                                  // Shift + Instr: the instrument library
    r.push(ot::InputEvent::kButtonUp, ot::btn::Shift);
    r.loop(3);
    CHECK(l.screen() == gb::Scr::Library);
    l.key(gb::Key::A, true); l.key(gb::Key::A, false);          // SAVE row: name keyboard (starts with the instrument's own name)
    CHECK(l.nameEditing());
    l.key(gb::Key::Start, true); l.key(gb::Key::Start, false);
    CHECK(st.files.count("GB/Instruments/Z.gbi") == 1);
    CHECK_EQ(l.libCount, 1);
    l.instr = 9;                                                // load it into instrument 9: the table moves to a free slot
    l.key(gb::Key::Down, true); l.key(gb::Key::Down, false);
    l.key(gb::Key::A, true); l.key(gb::Key::A, false);
    CHECK_EQ(int(s.instrument(9)[1]), 0xC3);
    CHECK_EQ(int(s.at(uint16_t(gb::off::kInstrNames + 9 * 5))), 'Z');
    const int t = s.instrument(9)[6] & 0x1F;
    CHECK(s.instrument(9)[6] & 0x20);
    CHECK(t != 5);
    CHECK_EQ(int(s.at(uint16_t(gb::off::kTblEnv + t * 16 + 2))), 0x77);
}


TEST(the_instrument_number_at_the_top_scrolls_through_instruments) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    keyTap(r, ot::btn::Instr);
    l.instrRow = 0; l.instr = 3;
    tapKey(l, gb::Key::Up);
    CHECK_EQ(l.instrRow, -1);
    tapKey(l, gb::Key::Right);
    CHECK_EQ(l.instr, 4);
    r.app.lsdj.encoder(2);
    CHECK_EQ(l.instr, 6);
    padEvent(r, ot::InputEvent::kPadDown, 20);                  // and the pads play the instrument on screen
    CHECK(r.app.player.voiceOn(0) || r.app.player.voiceOn(1) || r.app.player.voiceOn(2) || r.app.player.voiceOn(3));
    padEvent(r, ot::InputEvent::kPadUp, 20);
    tapKey(l, gb::Key::Down);
    CHECK_EQ(l.instrRow, 0);
}

TEST(delete_clears_a_cell_or_every_selected_cell_and_the_jog_wheel_changes_them_all) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    gb::Song& s = r.app.song;
    for (int c = 0; c < 4; ++c) for (int rr = 0; rr < 8; ++rr) s.songCell(rr, c) = 0xFF;
    s.songCell(0, 0) = 3; s.songCell(1, 0) = 4; s.songCell(2, 0) = 5;
    keyTap(r, ot::btn::Song);
    l.songRow = 1; l.songCh = 0;
    tapKey(l, gb::Key::B);                                      // a lone Delete tap clears the box
    CHECK_EQ(int(s.songCell(1, 0)), 0xFF);
    CHECK_EQ(int(s.songCell(0, 0)), 3);
    l.songRow = 0;                                              // Shift + Insert + Down, Down: three boxes
    l.key(gb::Key::Select, true); l.key(gb::Key::A, true);
    tapKey(l, gb::Key::Down); tapKey(l, gb::Key::Down);
    l.key(gb::Key::A, false); l.key(gb::Key::Select, false);
    CHECK(l.selecting());
    l.encoder(1);                                               // the jog wheel changes the filled boxes only
    CHECK_EQ(int(s.songCell(0, 0)), 4);
    CHECK_EQ(int(s.songCell(1, 0)), 0xFF);
    CHECK_EQ(int(s.songCell(2, 0)), 6);
    CHECK(l.selecting());
    tapKey(l, gb::Key::B);                                      // Delete clears them all
    CHECK_EQ(int(s.songCell(0, 0)), 0xFF);
    CHECK_EQ(int(s.songCell(2, 0)), 0xFF);
    CHECK(!l.selecting());
    keyTap(r, ot::btn::Note);                                   // a phrase: a note and its command value clear together
    l.phrase = 2; l.step = 4; l.phraseCol = 2;
    s.note(2, 4) = 30; s.phraseCmd(2, 4) = 5; s.phraseVal(2, 4) = 0x12;
    tapKey(l, gb::Key::B);
    CHECK_EQ(int(s.phraseCmd(2, 4)), 0);
    CHECK_EQ(int(s.phraseVal(2, 4)), 0);
    CHECK_EQ(int(s.note(2, 4)), 30);
}

TEST(delete_and_the_name_keyboard_and_help_use_the_same_exit_key) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    keyTap(r, ot::btn::Instr);
    l.instrRow = 0;
    tapKey(l, gb::Key::A);
    CHECK(l.nameEditing());
    l.key(gb::Key::Select, true); l.key(gb::Key::Select, false);      // Shift no longer cancels
    CHECK(l.nameEditing());
    tapKey(l, gb::Key::B);                                      // Delete = NO
    CHECK(!l.nameEditing());
    keyTap(r, ot::btn::Help);
    tapKey(l, gb::Key::A);                                      // only Delete leaves the help
    CHECK(l.screen() == gb::Scr::Help);
    tapKey(l, gb::Key::B);
    CHECK(l.screen() != gb::Scr::Help);
}

TEST(f1_and_f2_turn_the_pop_up_pages_with_a_free_hand) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    keyTap(r, ot::btn::Pattern);
    r.push(ot::InputEvent::kButtonDown, ot::btn::F8);
    r.loop(3);
    CHECK(l.popup() == gb::Lsdj::kPageHelp);
    CHECK(l.popupPages() >= 2);
    keyTap(r, ot::btn::F2);
    CHECK_EQ(l.popupPage(), 1);
    keyTap(r, ot::btn::F2);
    CHECK_EQ(l.popupPage(), std::min(2, l.popupPages() - 1));
    keyTap(r, ot::btn::F1);
    keyTap(r, ot::btn::F1);
    keyTap(r, ot::btn::F1);
    CHECK_EQ(l.popupPage(), 0);                                 // stops at the first page
    CHECK(&r.app.ui != nullptr);
    r.loop(3);                                                  // drawing with the page tab up must not crash
    r.push(ot::InputEvent::kButtonUp, ot::btn::F8);
    r.loop(3);
    CHECK(l.popup() == gb::Lsdj::kNoPopup);
    keyTap(r, ot::btn::F2);                                     // with no pop-up F2 is its usual self (the level key), not a page key
    CHECK_EQ(l.popupPage(), 0);
}

TEST(f8_explains_the_page_while_held_and_arrows_turn_its_pages) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    CHECK(gb::Lsdj::docsFit());
    keyTap(r, ot::btn::Pattern);
    r.push(ot::InputEvent::kButtonDown, ot::btn::F8);
    r.loop(3);
    CHECK(l.popup() == gb::Lsdj::kPageHelp);
    const int pages = l.popupPages();
    CHECK(pages >= 2);
    const int chainPos = l.chainPos;
    r.push(ot::InputEvent::kButtonDown, ot::btn::Right); r.push(ot::InputEvent::kButtonUp, ot::btn::Right);
    r.push(ot::InputEvent::kButtonDown, ot::btn::Down); r.push(ot::InputEvent::kButtonUp, ot::btn::Down);
    r.loop(3);
    CHECK_EQ(l.popupPage(), 1);
    CHECK_EQ(l.chainPos, chainPos);                             // the arrows turned pages instead of moving the cursor
    CHECK(l.screen() == gb::Scr::Chain);
    r.push(ot::InputEvent::kButtonUp, ot::btn::F8);
    r.loop(3);
    CHECK(l.popup() == gb::Lsdj::kNoPopup);
    for (gb::Scr sc : {gb::Scr::Song, gb::Scr::Phrase, gb::Scr::Instr, gb::Scr::Table, gb::Scr::Groove, gb::Scr::Project, gb::Scr::Synth, gb::Scr::Wave}) {
        l.show(sc);
        l.showPopup(gb::Lsdj::kPageHelp);
        CHECK(l.popupPages() >= 1);
        gb::Lcd lcd;
        l.draw(lcd);                                            // drawing every page must not crash
        l.hidePopup();
    }
}

TEST(shift_note_and_shift_instr_params_show_the_command_cheat_sheets_until_released) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    keyTap(r, ot::btn::Pattern);
    r.push(ot::InputEvent::kButtonDown, ot::btn::Shift);
    r.push(ot::InputEvent::kButtonDown, ot::btn::Note);
    r.loop(3);
    CHECK(l.popup() == gb::Lsdj::kPhraseSheet);
    CHECK(l.screen() == gb::Scr::Chain);                        // the page stays where it was
    r.push(ot::InputEvent::kButtonDown, ot::btn::Right); r.push(ot::InputEvent::kButtonUp, ot::btn::Right);
    r.loop(3);
    CHECK_EQ(l.popupPage(), 1);
    r.push(ot::InputEvent::kButtonUp, ot::btn::Note);
    r.loop(3);
    CHECK(l.popup() == gb::Lsdj::kNoPopup);
    r.push(ot::InputEvent::kButtonDown, ot::btn::InstrParams);
    r.loop(3);
    CHECK(l.popup() == gb::Lsdj::kTableSheet);
    r.push(ot::InputEvent::kButtonUp, ot::btn::Shift);          // letting go of Shift closes it too
    r.loop(3);
    CHECK(l.popup() == gb::Lsdj::kNoPopup);
    r.push(ot::InputEvent::kButtonUp, ot::btn::InstrParams);
    r.loop(3);
}

TEST(export_lsdj_can_write_song_a_song_b_or_both) {
    MemStorage st;
    Rig r(true, &st);
    oneNote(r.app.song, 0, 25, 3);
    oneNote(r.app.songB, 3, 49, 3);
    CHECK(r.app.exportFiles("X", 0));
    CHECK_EQ(int(st.files.count("GB/Export/X_A.lsdsng")), 1);
    CHECK_EQ(int(st.files.count("GB/Export/X_B.lsdsng")), 0);
    CHECK(r.app.exportFiles("Y", 1));
    CHECK_EQ(int(st.files.count("GB/Export/Y_B.srm")), 1);
    CHECK_EQ(int(st.files.count("GB/Export/Y_A.srm")), 0);
    gb::Lsdj& l = r.app.lsdj;
    keyTap(r, ot::btn::Config);
    l.projRow = 14;
    CHECK_EQ(l.exportSel, 2);                                   // A + B by default
        l.encoder(-1);                                              // the jog wheel picks it, like every other value
    CHECK_EQ(l.exportSel, 1);
    l.encoder(1);
    CHECK_EQ(l.exportSel, 2);
}

namespace {
struct LeadStub final : gb::Transport {
    gb::Player* p = nullptr;
    int lead = 0;
    void start(int, int row) override { p->start(row); }
    void startLoop(int, int ch, int phrase, int row) override { p->start(row, ch, phrase, true); }
    void stop() override { p->stop(); }
    bool running() const override { return p->running(); }
    bool action(Action, int) override { return true; }
    int listSongs(int) override { return 0; }
    const char* songName(int) override { return ""; }
    bool loadSongFile(int, int) override { return false; }
    bool deleteSongFile(int, int) override { return false; }
    int syncMode(int) override { return 0; }
    void setSyncMode(int, int) override {}
    void tempoChanged(int) override {}
    int listInstruments() override { return 0; }
    const char* instrumentFile(int) override { return ""; }
    bool saveInstrumentFile(const char*, const uint8_t*, int) override { return true; }
    int loadInstrumentFile(int, uint8_t*, int) override { return -1; }
    bool linked() override { return false; }
    int volume(int) override { return 15; }
    void setVolume(int, int) override {}
    int look() override { return 0; }
    void setLook(int) override {}
    int audioLeadMs() override { return lead; }
};
}  // namespace

TEST(recorded_pad_notes_land_where_they_were_heard_not_where_the_audio_queue_has_got_to) {
    gb::Song s;
    gb::Player p(s);
    gb::Lsdj l(s, p);
    LeadStub t; t.p = &p;
    l.setTransport(&t, 0);
    p.setTempo(100);                                            // a tick every 25 ms, a step every 150 ms
    for (int i = 0; i < 16; ++i) { s.note(7, i) = 0; s.phraseInstr(7, i) = 0xFF; }
    l.show(gb::Scr::Phrase);
    l.phrase = 7; l.songCh = 0;
    l.toggleRecMode();
    l.key(gb::Key::Start, true); l.key(gb::Key::Start, false);
    CHECK(p.running());
    int16_t buf[2 * 512];
    for (int i = 0; i < 40; ++i) p.render(buf, 512, 44100);
    auto where = [&](int lead) {
        for (int i = 0; i < 16; ++i) { s.note(7, i) = 0; s.phraseInstr(7, i) = 0xFF; }
        t.lead = lead;
        l.padDown(10);
        l.padUp(10);
        for (int i = 0; i < 16; ++i) if (s.note(7, i)) return i;
        return -1;
    };
    const int now = where(0), heard = where(148);
    CHECK(now >= 0 && heard >= 0);
    CHECK_EQ((now - heard + 16) % 16, 1);                       // a step's worth of lead moves the note one step back
}

TEST(the_power_on_chime_plays_the_recording_and_then_stops) {
    Rig r;
    r.app.bootChime(0);
    int peak = 0, early = 0, late = 0;
    int16_t out[2 * 512];
    for (int block = 0; block < 100; ++block) {              // about 1.16 s at 44.1 kHz
        r.loop(2);
        r.app.fill(out, 512);
        int m = 0;
        for (int i = 0; i < 512; ++i) m = std::max(m, std::abs(int(out[2 * i])));
        peak = std::max(peak, m);
        if (block == 40) early = m;                           // about 0.46 s: still ringing
        if (block == 90) late = m;                            // after the 0.82 s recording
    }
    CHECK(peak > 5000 && peak < 12000);                      // the recording peaks near 5000, played 1.5 times louder
    CHECK(early > 1500);
    CHECK_EQ(late, 0);
    r.app.bootChime(2);
}

TEST(export_lsdj_also_writes_the_rom_with_the_card_kits_in_the_banks_lsdj_reads_them_from) {
    MemStorage st;
    std::vector<uint8_t> rom(1u << 20);
    for (size_t i = 0; i < rom.size(); ++i) rom[i] = i / 0x4000 == 35 ? 0x55 : i / 0x4000 >= 34 ? 0xFF : uint8_t(i / 0x4000 + 1);   // bank b holds b + 1; banks 34 and 36 up are empty, 35 is something else
    st.files["GB/LSDJ.GB"] = rom;
    std::vector<uint8_t> kit(0x4000, 0xAA);
    kit[0] = 0x60; kit[1] = 0x40;
    st.files["GB/Kits/15_AMEN.kit"] = kit;                      // kit 0x15 is bank 34, which is empty
    st.files["GB/Kits/16_OTHER.kit"] = kit;                     // kit 0x16 is bank 35, which holds something else: it must stay
    st.files["GB/Kits/1A_AMEN.kit"] = kit;                      // kit 0x1A is bank 39 (n + 13), empty
    st.dirs.insert("GB/Kits");
    Rig r(true, &st);
    r.app.loadKits();
    CHECK_EQ(r.app.kitFilesLoaded(), 3);
    oneNote(r.app.song, 0, 25, 3);
    CHECK(r.app.exportFiles("K", 0));
    const auto& out = st.files["GB/Export/K_A.gb"];
    CHECK_EQ(int(out.size()), 1 << 20);
    CHECK_EQ(int(out[30 * 0x4000 + 5]), 31);                    // other banks are untouched
    CHECK_EQ(int(out[34 * 0x4000 + 5]), 0xAA);                  // kit 15 is in its bank
    CHECK_EQ(int(out[35 * 0x4000 + 5]), 0x55);                  // the occupied bank stays
    CHECK_EQ(int(out[39 * 0x4000 + 5]), 0xAA);                  // kit 1A is in bank 39, where LSDJ reads kit 1A
}

// ---- sample kits ----------------------------------------------------------------------------------------------------------------

namespace {
void putWav(MemStorage& st, const char* path, int rate, double hz, double seconds) {
    std::vector<uint8_t>& v = st.files[path];
    auto p32 = [&](uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i))); };
    auto p16 = [&](uint16_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); };
    const int frames = int(rate * seconds);
    for (char c : {'R', 'I', 'F', 'F'}) v.push_back(uint8_t(c));
    p32(uint32_t(36 + frames * 2));
    for (char c : {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}) v.push_back(uint8_t(c));
    p32(16); p16(1); p16(1); p32(uint32_t(rate)); p32(uint32_t(rate * 2)); p16(2); p16(16);
    for (char c : {'d', 'a', 't', 'a'}) v.push_back(uint8_t(c));
    p32(uint32_t(frames * 2));
    for (int i = 0; i < frames; ++i) p16(uint16_t(int16_t(std::sin(6.2831853 * hz * i / rate) * 12000)));
    st.dirs.insert("GB/Samples");
}
void tap(Rig& r, uint8_t b) { keyTap(r, b); }
}  // namespace

TEST(sample_loader_editor_and_kit_make_a_kit_that_the_console_plays_and_exports) {
    MemStorage st;
    std::vector<uint8_t> rom(1u << 20);
    for (size_t i = 0; i < rom.size(); ++i) rom[i] = i / 0x4000 >= 34 ? 0xFF : uint8_t(i / 0x4000 + 1);
    st.files["GB/LSDJ.GB"] = rom;
    putWav(st, "GB/Samples/KICK.wav", 44100, 80, 0.5);
    putWav(st, "GB/Samples/SNARE.wav", 22050, 300, 0.3);
    Rig r(true, &st);
    r.app.loadKits();
    gb::Lsdj& l = r.app.lsdj;
    tap(r, ot::btn::SampleLoader);
    CHECK(l.screen() == gb::Scr::Loader);
    CHECK_EQ(l.smpCount, 2);                                    // KICK.wav and SNARE.wav
    keyTap(r, ot::btn::Insert);                                 // Insert adds KICK to kit 1A at once
    CHECK(l.screen() == gb::Scr::Loader);
    CHECK(std::strstr(l.status(), "ADDED") != nullptr);
    CHECK_EQ(int(st.files.count("GB/Kits/A/15_KIT15.kit")), 1);     // saved at once for console A
    CHECK_EQ(int(st.files["GB/Kits/A/15_KIT15.kit"].size()), 0x4000);
    CHECK_EQ(int(st.files.count("GB/Kits/A/15_KIT15.src")), 1);     // it remembers where the sample came from (the check mark)
    CHECK(std::string(st.files["GB/Kits/A/15_KIT15.src"].begin(), st.files["GB/Kits/A/15_KIT15.src"].end()).find("KICK.wav") != std::string::npos);
    CHECK(r.app.kits.kit(0x15).data != nullptr);                // console A plays it
    CHECK(r.app.kitsB.kit(0x15).data == nullptr);               // console B does not
    gb::Lcd lcd0;
    l.draw(lcd0);
    keyTap(r, ot::btn::Insert);
    CHECK(std::strstr(l.status(), "ALREADY") != nullptr);       // not twice
    CHECK_EQ(gb::KitBank::count(r.app.kits.kit(0x15).data), 1);
    // SNARE: Shift + Insert opens it in the editor to cut it, then ADD TO KIT
    keyTap(r, ot::btn::Down);
    r.push(ot::InputEvent::kButtonDown, ot::btn::Shift);
    keyTap(r, ot::btn::Insert);
    r.push(ot::InputEvent::kButtonUp, ot::btn::Shift);
    r.loop(3);
    CHECK(l.screen() == gb::Scr::Editor);
    CHECK(l.smpLen > 1000);
    CHECK(std::strcmp(l.sampleName(), "SNA") == 0);
    const int len = l.smpEnd;
    l.smpRow = 1;                                               // END: the jog wheel trims it
    l.encoder(-3);
    CHECK_EQ(l.smpEnd, len - 3);
    l.smpRow = 6;                                               // ADD TO KIT
    keyTap(r, ot::btn::Insert);
    CHECK(std::strstr(l.status(), "ADDED") != nullptr);
    CHECK_EQ(gb::KitBank::count(r.app.kits.kit(0x15).data), 2);
    // the kit page lists both
    tap(r, ot::btn::SamplePlayback);
    CHECK(l.screen() == gb::Scr::Kit);
    CHECK_EQ(l.kitSlot, 0x15);
    gb::Lcd lcd;
    l.draw(lcd);
    padEvent(r, ot::InputEvent::kPadDown, 1);
    // exporting: console A's ROM has the kit in bank 34, console B's does not
    oneNote(r.app.song, 0, 25, 3);
    CHECK(r.app.exportFiles("Q", 2));
    const auto& a = st.files["GB/Export/Q_A.gb"];
    const auto& b = st.files["GB/Export/Q_B.gb"];
    CHECK_EQ(int(a.size()), 1 << 20);
    CHECK_EQ(int(a[34 * 0x4000]), 0x60);
    CHECK_EQ(int(b[34 * 0x4000]), 0xFF);
    for (const auto* rom : {&a, &b}) {                          // the header's global checksum matches what is in the file, as a real ROM's does
        uint32_t sum = 0;
        for (size_t i = 0; i < rom->size(); ++i) if (i != 0x14E && i != 0x14F) sum += (*rom)[i];
        CHECK_EQ(int((*rom)[0x14E] << 8 | (*rom)[0x14F]), int(sum & 0xFFFF));
    }
    // Delete on the loader takes KICK out again (the check mark goes), the second sample moves up
    tap(r, ot::btn::SampleLoader);
    l.smpSel = 0;
    keyTap(r, ot::btn::Delete);
    CHECK(std::strstr(l.status(), "REMOVED") != nullptr);
    CHECK_EQ(gb::KitBank::count(r.app.kits.kit(0x15).data), 1);
    keyTap(r, ot::btn::Delete);
    CHECK(std::strstr(l.status(), "NOT IN") != nullptr);
    // erasing the last sample on the kit page removes the kit's files
    tap(r, ot::btn::SamplePlayback);
    l.kitRow = 2;
    keyTap(r, ot::btn::Delete);
    CHECK(std::strstr(l.status(), "ERASE") != nullptr);
    keyTap(r, ot::btn::Insert);
    CHECK_EQ(int(st.files.count("GB/Kits/A/15_KIT15.kit")), 0);
    CHECK_EQ(int(st.files.count("GB/Kits/A/15_KIT15.src")), 0);
}

TEST(the_editors_samples_row_scrolls_the_kit_and_add_to_kit_replaces_a_sample_after_asking) {
    MemStorage st;
    putWav(st, "GB/Samples/KICK.wav", 44100, 80, 0.5);
    putWav(st, "GB/Samples/SNARE.wav", 22050, 300, 0.3);
    Rig r(true, &st);
    gb::Lsdj& l = r.app.lsdj;
    tap(r, ot::btn::SampleLoader);
    keyTap(r, ot::btn::Insert);                                 // KICK into kit 1A
    keyTap(r, ot::btn::Down);
    keyTap(r, ot::btn::Insert);                                 // SNARE too
    const uint8_t* kit = r.app.kits.kit(0x15).data;
    CHECK_EQ(gb::KitBank::count(kit), 2);
    const int kickLen = gb::KitBank::length(kit, 0);
    const int snareLen = gb::KitBank::length(kit, 1);
    tap(r, ot::btn::SampleEditor);
    CHECK(l.screen() == gb::Scr::Editor);
    l.smpRow = 4;                                               // the X/15 SAMPLES row: the jog wheel scrolls the kit
    l.encoder(1);
    CHECK_EQ(l.smpEditIdx, 0);
    CHECK_EQ(l.smpLen, kickLen - 16);                           // the kick, without its silent end chunk
    l.encoder(1);
    CHECK_EQ(l.smpEditIdx, 1);
    CHECK_EQ(l.smpLen, snareLen - 16);
    l.encoder(-1);                                              // back to the kick
    CHECK_EQ(l.smpEditIdx, 0);
    l.smpRow = 1;                                               // trim its end
    const int end = l.smpEnd;
    l.encoder(-10);
    CHECK_EQ(l.smpEnd, end - 10);
    l.smpRow = 6;                                               // REPLACE: asks first, nothing changes yet
    keyTap(r, ot::btn::Insert);
    gb::Lcd lcd;
    l.draw(lcd);
    CHECK_EQ(gb::KitBank::length(r.app.kits.kit(0x15).data, 0), kickLen);
    keyTap(r, ot::btn::Delete);                                 // BKSP says no
    CHECK_EQ(gb::KitBank::length(r.app.kits.kit(0x15).data, 0), kickLen);
    keyTap(r, ot::btn::Insert);
    keyTap(r, ot::btn::Insert);                                 // yes
    CHECK(std::strstr(l.status(), "REPLACED") != nullptr);
    kit = r.app.kits.kit(0x15).data;
    CHECK_EQ(gb::KitBank::count(kit), 2);                       // replaced, not added
    CHECK(gb::KitBank::length(kit, 0) < kickLen);
    CHECK_EQ(gb::KitBank::length(kit, 1), snareLen);            // the next one is untouched
}

TEST(the_recorder_page_plays_the_input_through_the_output_only_while_it_is_open) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    gLineAmp = 8000;
    int16_t out[2 * 128];
    auto peak = [&] { int m = 0; for (int b = 0; b < 8; ++b) { r.loop(20); r.app.fill(out, 128); for (int i = 0; i < 256; ++i) m = std::max(m, std::abs(int(out[i]))); } return m; };
    CHECK_EQ(peak(), 0);                                        // not on the page: the input is not heard
    tap(r, ot::btn::SampleRecorder);
    CHECK(peak() > 4000);                                       // heard, and the meter moves
    l.recRow = 2;
    keyTap(r, ot::btn::Insert);                                 // MONITOR OFF
    CHECK(!l.recMon);
    CHECK_EQ(peak(), 0);
    keyTap(r, ot::btn::Insert);
    CHECK(peak() > 4000);
    keyTap(r, ot::btn::Delete);                                 // leaves the page
    CHECK_EQ(peak(), 0);
    gLineAmp = 0;
}

TEST(delete_on_the_sample_editor_drops_a_new_sample_and_erases_a_kit_sample_after_asking) {
    MemStorage st;
    putWav(st, "GB/Samples/KICK.wav", 44100, 80, 0.5);
    putWav(st, "GB/Samples/SNARE.wav", 22050, 300, 0.3);
    Rig r(true, &st);
    gb::Lsdj& l = r.app.lsdj;
    tap(r, ot::btn::SampleLoader);
    auto cnt = [&] { const uint8_t* d = r.app.kits.kit(0x15).data; return d ? gb::KitBank::count(d) : 0; };
    keyTap(r, ot::btn::Insert);
    keyTap(r, ot::btn::Down);
    keyTap(r, ot::btn::Insert);
    const int c2 = cnt();                                       // the kit override is shared between tests, so count from here
    CHECK(c2 >= 2);
    tap(r, ot::btn::SampleEditor);
    l.smpRow = 4;
    l.encoder(c2);                                               // the kick, from the kit
    CHECK_EQ(l.smpEditIdx, c2 - 1);
    l.smpRow = 7;
    keyTap(r, ot::btn::Insert);                                 // asks first
    gb::Lcd lcd;
    l.draw(lcd);
    CHECK_EQ(cnt(), c2);
    keyTap(r, ot::btn::Delete);                                 // no
    CHECK_EQ(cnt(), c2);
    keyTap(r, ot::btn::Insert);
    keyTap(r, ot::btn::Insert);                                 // yes
    CHECK_EQ(cnt(), c2 - 1);
    CHECK_EQ(l.smpLen, 0);
    CHECK_EQ(l.smpEditIdx, -1);
    // a sample that is only in the editor goes at once
    tap(r, ot::btn::SampleLoader);
    keyTap(r, ot::btn::Insert);
    tap(r, ot::btn::SampleEditor);
    l.smpLen = 640; l.smpEnd = 40;
    l.smpRow = 7;
    keyTap(r, ot::btn::Insert);
    CHECK_EQ(l.smpLen, 0);
    CHECK(std::strstr(l.status(), "DELETED") != nullptr);
}

TEST(a_recorded_samples_name_can_be_typed_in_the_editor) {
    MemStorage st;
    putWav(st, "GB/Samples/KICK.wav", 44100, 80, 0.5);
    Rig r(true, &st);
    gb::Lsdj& l = r.app.lsdj;
    tap(r, ot::btn::SampleLoader);
    tap(r, ot::btn::SampleEditor);
    l.smpLen = 640; l.smpEnd = 40;
    std::memcpy(const_cast<char*>(l.sampleName()), "REC", 4);
    l.smpRow = 2;
    keyTap(r, ot::btn::Insert);                                 // the keyboard opens empty
    keyTap(r, ot::btn::Insert);                                 // first key: Q
    keyTap(r, ot::btn::Insert);
    keyTap(r, ot::btn::Insert);
    keyTap(r, ot::btn::Play);                                  // commit
    CHECK(std::strcmp(l.sampleName(), "REC") != 0);
    CHECK(std::strlen(l.sampleName()) == 3);
}

TEST(shift_insert_on_a_sample_of_your_kit_renames_it_from_the_kit_page) {
    MemStorage st;
    putWav(st, "GB/Samples/KICK.wav", 44100, 80, 0.5);
    Rig r(true, &st);
    gb::Lsdj& l = r.app.lsdj;
    tap(r, ot::btn::SampleLoader);
    keyTap(r, ot::btn::Insert);                                 // KICK into the kit
    const int idx = gb::KitBank::count(r.app.kits.kit(0x15).data) - 1;
    tap(r, ot::btn::SamplePlayback);
    CHECK(l.screen() == gb::Scr::Kit);
    l.kitRow = 2 + idx;
    r.push(ot::InputEvent::kButtonDown, ot::btn::Shift);
    keyTap(r, ot::btn::Insert);                                 // Shift + Insert: the name keyboard
    r.push(ot::InputEvent::kButtonUp, ot::btn::Shift);
    CHECK(l.nameEditing());
    keyTap(r, ot::btn::Insert);                                 // Q
    keyTap(r, ot::btn::Insert);
    keyTap(r, ot::btn::Insert);
    keyTap(r, ot::btn::Play);                                   // commit
    CHECK(!l.nameEditing());
    char nm[4];
    gb::KitBank::name(r.app.kits.kit(0x15).data, idx, nm);
    CHECK(std::strcmp(nm, "KIC") != 0);
    CHECK_EQ(int(std::strlen(nm)), 3);
}

TEST(the_editors_level_row_turns_a_sample_down_as_it_is_added_so_layers_have_room) {
    MemStorage st;
    putWav(st, "GB/Samples/KICK.wav", 44100, 80, 0.5);
    putWav(st, "GB/Samples/SNARE.wav", 22050, 300, 0.3);
    Rig r(true, &st);
    gb::Lsdj& l = r.app.lsdj;
    auto cnt = [&] { const uint8_t* d = r.app.kits.kit(0x15).data; return d ? gb::KitBank::count(d) : 0; };
    auto loud = [&](int i) {
    // (the loudest deviation from the centre 8 in a stored sample)
        const uint8_t* d = r.app.kits.kit(0x15).data;
        const uint8_t* s = gb::KitBank::data(d, i);
        int m = 0;
        for (int k = 0; k < gb::KitBank::length(d, i); ++k) m = std::max(m, std::max(std::abs((s[k] >> 4) - 8), std::abs((s[k] & 15) - 8)));
        return m;
    };
    tap(r, ot::btn::SampleLoader);
    keyTap(r, ot::btn::Insert);                                 // KICK at full level
    const int full = loud(cnt() - 1);
    CHECK(full >= 6);
    keyTap(r, ot::btn::Down);
    r.push(ot::InputEvent::kButtonDown, ot::btn::Shift);
    keyTap(r, ot::btn::Insert);                                 // Shift + Insert: SNARE into the editor
    r.push(ot::InputEvent::kButtonUp, ot::btn::Shift);
    CHECK(l.screen() == gb::Scr::Editor);
    CHECK_EQ(l.smpLevel, 0);
    l.smpRow = 5;                                               // LEVEL: right is louder, left quieter
    l.encoder(-2);
    CHECK_EQ(l.smpLevel, 2);                                    // 50%
    l.smpRow = 6;
    keyTap(r, ot::btn::Insert);                                 // ADD TO KIT
    const int snare = loud(cnt() - 1);
    CHECK(snare <= (full + 1) / 2 + 1);
    CHECK_EQ(l.smpLevel, 0);                                    // the next one starts at full level again
}

TEST(kit_instruments_carry_lsdjs_d0_in_byte_10_or_layered_notes_play_garbage_in_the_rom) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    uint8_t* in = r.app.song.instrument(5);
    in[0] = 0; in[1] = 0xA0; in[4] = 0xFF;
    l.instr = 5;
    keyTap(r, ot::btn::Instr);
    l.instrRow = 1;                                             // TYPE: pulse -> wave -> kit
    l.encoder(1);
    l.encoder(1);
    CHECK_EQ(int(in[0]), 2);
    CHECK_EQ(int(in[10]), 0xD0);                                // what every LSDJ kit instrument has
    CHECK_EQ(int(in[14]), 0xF3);
    // kit instruments from earlier builds (zeros there) are repaired before a save or an export
    uint8_t* old = r.app.song.instrument(9);
    old[0] = 2; old[9] = 0x15; old[2] = 0x15;
    CHECK(r.app.song.repairKitInstruments());
    CHECK_EQ(int(old[10]), 0xD0);
    CHECK_EQ(int(old[9]), 0x15);                                // nothing else touched
    CHECK(!r.app.song.repairKitInstruments());
    uint8_t* pulse = r.app.song.instrument(10);
    pulse[0] = 0;
    r.app.song.repairKitInstruments();
    CHECK_EQ(int(pulse[10]), 0);                                // only kits
}

TEST(the_peak_light_flickers_while_a_loud_sample_plays_and_stays_dark_at_a_low_level) {
    MemStorage st;
    putWav(st, "GB/Samples/KICK.wav", 44100, 80, 0.5);
    Rig r(true, &st);
    gb::Lsdj& l = r.app.lsdj;
    tap(r, ot::btn::SampleLoader);
    r.push(ot::InputEvent::kButtonDown, ot::btn::Shift);
    keyTap(r, ot::btn::Insert);                                 // into the editor
    r.push(ot::InputEvent::kButtonUp, ot::btn::Shift);
    CHECK(l.screen() == gb::Scr::Editor);
    auto flickers = [&] {
        keyTap(r, ot::btn::Play);
        int hot = 0;
        for (int i = 0; i < 300; ++i) { r.loop(5); hot += l.peakLight() ? 1 : 0; }
        return hot;
    };
    CHECK(flickers() > 3);                                      // a full-scale kick peaks
    l.smpRow = 5;
    l.encoder(-3);                                              // LEVEL 25%
    CHECK_EQ(l.smpLevel, 3);
    CHECK_EQ(flickers(), 0);
}

TEST(delete_clears_a_groove_step_to_zero) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    r.app.song.groove(0)[0] = 6;
    r.app.song.groove(0)[1] = 7;
    keyTap(r, ot::btn::Performance);
    CHECK(l.screen() == gb::Scr::Groove);
    l.grooveStep = 1;
    keyTap(r, ot::btn::Delete);
    CHECK_EQ(int(r.app.song.groove(0)[1]), 0);
    CHECK_EQ(int(r.app.song.groove(0)[0]), 6);                  // only that step
}

TEST(a_kit_instruments_kit_number_is_edited_on_the_instr_page) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    uint8_t* in = r.app.song.instrument(3);
    in[0] = 2;
    l.instr = 3;
    keyTap(r, ot::btn::Instr);
    l.instrRow = 2;                                             // KIT 1
    l.encoder(0x1A);
    CHECK_EQ(int(in[9] & 0x7F), 0x1A);
}

TEST(shift_f8_on_the_project_page_opens_the_firmware_loader_and_flashes_after_a_second_insert) {
    MemStorage st;
    st.files["Firmware/PolyendTracGBTrkv041.ptf"] = std::vector<uint8_t>(100, 1);
    st.files["Firmware/PolyendTracker_CE_1.0.0.ptf"] = std::vector<uint8_t>(100, 2);
    st.files["Firmware/ot_restore.txt"] = std::vector<uint8_t>(24, 'x');
    st.files["Firmware/notes.txt"] = std::vector<uint8_t>(10, 'x');
    st.dirs.insert("Firmware");
    Rig r(true, &st);
    gb::Lsdj& l = r.app.lsdj;
    gFwCalls = 0;
    keyTap(r, ot::btn::Config);
    CHECK(l.screen() == gb::Scr::Project);
    r.push(ot::InputEvent::kButtonDown, ot::btn::F8);           // no Shift: the page help, not the loader
    r.loop(3);
    CHECK(l.screen() == gb::Scr::Project);
    r.push(ot::InputEvent::kButtonUp, ot::btn::F8);
    r.loop(3);
    r.push(ot::InputEvent::kButtonDown, ot::btn::Shift);
    r.push(ot::InputEvent::kButtonDown, ot::btn::F8);
    r.loop(3);
    r.push(ot::InputEvent::kButtonUp, ot::btn::F8);
    r.push(ot::InputEvent::kButtonUp, ot::btn::Shift);
    r.loop(3);
    CHECK(l.screen() == gb::Scr::Firmware);
    CHECK_EQ(l.fwCount, 2);                                     // only the images the bootloader accepts
    gb::Lcd lcd;
    l.draw(lcd);
    keyTap(r, ot::btn::Insert);
    CHECK_EQ(gFwCalls, 0);                                      // asks first
    CHECK(std::strstr(l.status(), "FLASH") != nullptr);
    keyTap(r, ot::btn::Delete);                                 // anything else says no
    CHECK_EQ(gFwCalls, 0);
    keyTap(r, ot::btn::Insert);
    keyTap(r, ot::btn::Insert);
    CHECK_EQ(gFwCalls, 1);
    CHECK(std::strcmp(gFwName, "PolyendTracGBTrkv041.ptf") == 0);
}

TEST(a_heard_sample_plays_to_its_end_and_not_just_its_first_chunk) {
    Rig r;
    std::vector<uint8_t> buf(2400);
    for (size_t i = 0; i < buf.size(); ++i) buf[i] = (i / 8) % 2 ? 0xFF : 0x00;     // a loud square wave, 0.4 s at the kit rate
    r.app.player.auditionData(buf.data(), int(buf.size()));
    int16_t out[2 * 512];
    int latePeak = 0;
    for (int block = 0; block < 36; ++block) {                  // 0.42 s, through the audio queue as the Tracker plays it
        r.loop(2);
        r.app.fill(out, 512);
        if (block >= 20 && block < 30) for (int i = 0; i < 512; ++i) latePeak = std::max(latePeak, std::abs(int(out[2 * i])));
    }
    CHECK(latePeak > 500);                                      // still sounding a quarter of a second in
}

TEST(on_the_kit_page_insert_opens_a_sample_in_the_editor_and_shift_insert_renames_it) {
    MemStorage st;
    std::vector<uint8_t> rom(1u << 20, 0xFF);
    std::vector<uint8_t> kit(0x4000);
    gb::KitBank::clear(kit.data());
    std::vector<uint8_t> smp(160, 0x9A);
    gb::KitBank::add(kit.data(), "ABC", smp.data(), int(smp.size()));
    std::memcpy(rom.data() + 8 * 0x4000, kit.data(), 0x4000);   // a ROM kit 00 with one sample
    st.files["GB/LSDJ.GB"] = rom;
    Rig r(true, &st);
    r.app.loadKits();
    gb::Lsdj& l = r.app.lsdj;
    l.kitSlot = 0;
    tap(r, ot::btn::SamplePlayback);
    keyTap(r, ot::btn::Down); keyTap(r, ot::btn::Down); keyTap(r, ot::btn::Down);   // down to sample 1
    r.push(ot::InputEvent::kButtonDown, ot::btn::Shift);
    keyTap(r, ot::btn::Insert);                                 // Shift + Insert on a ROM kit: no renaming
    r.push(ot::InputEvent::kButtonUp, ot::btn::Shift);
    CHECK(l.screen() == gb::Scr::Kit);
    CHECK(!l.nameEditing());
    l.kitRow = 2;
    keyTap(r, ot::btn::Insert);                                 // opens it in the Sample Editor
    CHECK(l.screen() == gb::Scr::Editor);
    CHECK_EQ(l.smpLen, 160);
    CHECK(std::strcmp(l.sampleName(), "ABC") == 0);
    CHECK_EQ(l.kitSlot, 0x15);                                  // a ROM sample goes to the first kit of your own
}

TEST(play_on_the_chain_phrase_and_table_pages_plays_just_that_and_shift_play_plays_the_song) {
    Rig r;
    gb::Song& s = r.app.song;
    gb::Lsdj& l = r.app.lsdj;
    for (int row = 0; row < 256; ++row) for (int c = 0; c < 4; ++c) s.songCell(row, c) = 0xFF;
    s.songCell(0, 0) = 1; s.songCell(0, 1) = 1;                  // the song plays chain 1 on tracks 1 and 2
    for (int i = 0; i < 16; ++i) s.chainPhrase(1, i) = 0xFF;
    s.chainPhrase(1, 0) = 5;
    for (int i = 0; i < 16; ++i) s.chainPhrase(3, i) = 0xFF;
    s.chainPhrase(3, 0) = 7; s.chainPhrase(3, 1) = 8;            // the chain on screen: two phrases
    s.note(5, 0) = 25; s.phraseInstr(5, 0) = 0;
    s.note(7, 0) = 25; s.phraseInstr(7, 0) = 0;
    s.note(8, 0) = 30; s.phraseInstr(8, 0) = 0;
    l.songCh = 2;                                                // the cursor track
    l.chain = 3;
    keyTap(r, ot::btn::Pattern);                                 // the Chain page
    keyTap(r, ot::btn::Play);
    r.loop(3);
    CHECK(r.app.dual.running());
    CHECK(r.app.player.seq().active(2));                         // only the cursor track plays
    CHECK(!r.app.player.seq().active(0));
    CHECK(!r.app.player.seq().active(1));
    CHECK_EQ(r.app.player.seq().chain(2), 3);
    CHECK_EQ(r.app.player.seq().phrase(2), 7);
    keyTap(r, ot::btn::Play);                                    // Play again stops it
    r.loop(3);
    CHECK(!r.app.dual.running());
    l.phrase = 7;
    keyTap(r, ot::btn::Note);                                    // the Phrase page
    keyTap(r, ot::btn::Play);
    r.loop(3);
    CHECK(r.app.player.seq().active(2));
    CHECK(!r.app.player.seq().active(0));
    CHECK_EQ(r.app.player.seq().phrase(2), 7);
    keyTap(r, ot::btn::Play);
    r.loop(3);
    CHECK(!r.app.dual.running());
    r.push(ot::InputEvent::kButtonDown, ot::btn::Shift);         // Shift + Play: the whole song
    keyTap(r, ot::btn::Play);
    r.push(ot::InputEvent::kButtonUp, ot::btn::Shift);
    r.loop(3);
    CHECK(r.app.player.seq().active(0));
    CHECK(r.app.player.seq().active(1));
    keyTap(r, ot::btn::Play);
    r.loop(3);
    // a table: the instrument that uses it is played, Play again stops it
    uint8_t* in = s.instrument(6);
    in[0] = 0; in[1] = 0xF0; in[6] = 0x20 | 4;                   // a pulse sound using table 4
    s.at(uint16_t(gb::off::kTblEnv + 4 * 16)) = 0x5F;
    l.table = 4;
    l.instr = 0;
    keyTap(r, ot::btn::InstrParams);                             // the Table page
    keyTap(r, ot::btn::Play);
    r.loop(40);
    CHECK(r.app.player.voiceOn(0));
    keyTap(r, ot::btn::Play);
    r.loop(3);
    CHECK(!r.app.player.voiceOn(0));
}

TEST(new_song_asks_for_a_name_and_starts_empty_from_the_load_page_and_the_project_page) {
    MemStorage st;
    Rig r(true, &st);
    gb::Lsdj& l = r.app.lsdj;
    oneNote(r.app.song, 1, 25, 3);
    CHECK_EQ(int(r.app.song.songCell(0, 1)), 0);
    l.openFiles();
    CHECK(l.screen() == gb::Scr::File);
    CHECK_EQ(l.fileSel, 0);                                     // NEW SONG is the first entry
    r.app.song.at(gb::off::kInstrNames) = 'K';                  // instrument 0 is called K...: that must not leak into the new name
    l.instr = 0;
    keyTap(r, ot::btn::Insert);
    CHECK(l.nameEditing());
    CHECK_EQ(int(l.nameBuffer()[0]), 0);                        // the name starts blank
    l.key(gb::Key::Down, true); l.key(gb::Key::Down, false);
    l.key(gb::Key::Down, true); l.key(gb::Key::Down, false);
    l.key(gb::Key::A, true); l.key(gb::Key::A, false);          // a letter
    l.key(gb::Key::Start, true); l.key(gb::Key::Start, false);   // OK
    CHECK(!l.nameEditing());
    CHECK_EQ(int(r.app.song.songCell(0, 1)), 0xFF);             // empty (a fresh song has chain 00 on the first row of track 1 only)
    CHECK_EQ(int(r.app.song.songCell(0, 0)), 0);
    CHECK(l.name()[0] != 0);
    CHECK(l.screen() == gb::Scr::Song);
    oneNote(r.app.song, 2, 25, 3);                              // the same from the Project page: the NEW SONG row
    keyTap(r, ot::btn::Config);
    l.projRow = 12;
    keyTap(r, ot::btn::Insert);
    CHECK(l.nameEditing());
    l.key(gb::Key::B, true); l.key(gb::Key::B, false);          // Delete = NO: nothing changes
    CHECK_EQ(int(r.app.song.songCell(0, 2)), 0);
}

TEST(the_project_pages_technical_line_shows_only_while_shift_is_held) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    static const char kInfo[] = "P1K L0 R1024K1";
    l.setAudioInfo(kInfo);
    keyTap(r, ot::btn::Config);
    gb::Lcd a;
    l.draw(a);
    auto row16 = [](const gb::Lcd& lcd) { int on = 0; for (int y = 128; y < 136; ++y) for (int x = 0; x < 150; ++x) on += lcd.at(x, y) != 0; return on; };
    CHECK_EQ(row16(a), 0);
    r.push(ot::InputEvent::kButtonDown, ot::btn::Shift);
    r.loop(3);
    gb::Lcd b;
    l.draw(b);
    CHECK(row16(b) > 0);
    r.push(ot::InputEvent::kButtonUp, ot::btn::Shift);
}

TEST(the_library_lists_hundreds_of_presets_and_left_right_turn_a_page) {
    MemStorage st;
    for (int i = 0; i < 300; ++i) {
        char n[48];
        std::snprintf(n, sizeof n, "GB/Instruments/P%03d.gbi", i);
        st.files[n] = std::vector<uint8_t>(26, 0);
    }
    st.dirs.insert("GB/Instruments");
    Rig r(true, &st);
    gb::Lsdj& l = r.app.lsdj;
    l.openLibrary();
    CHECK_EQ(l.libCount, 300);
    l.libSel = 0;
    keyTap(r, ot::btn::Right);
    CHECK_EQ(l.libSel, 13);
    keyTap(r, ot::btn::Left);
    CHECK_EQ(l.libSel, 0);
    gb::Lcd lcd;
    l.draw(lcd);
}

TEST(a_new_songs_empty_instruments_sound_the_first_time_they_are_used_and_a_type_change_starts_from_its_defaults) {
    for (int type = 0; type < 4; ++type) {
        if (type == 2) continue;                                // a kit needs the ROM's kits
        Rig r;
        gb::Lsdj& l = r.app.lsdj;
        r.app.newSong(0, "X");
        l.instr = 5;                                            // an instrument nobody has touched: all zeros
        keyTap(r, ot::btn::Instr);
        CHECK_EQ(int(r.app.song.instrument(5)[1]), 0xA0);       // opening it gives the pulse default
        l.instrRow = 1;                                         // TYPE
        for (int i = 0; i < type; ++i) l.encoder(1);
        CHECK_EQ(int(r.app.song.instrument(5)[0] & 3), type);
        CHECK(r.app.song.instrument(5)[1] != 0);                // not silent: the type's own default
        padEvent(r, ot::InputEvent::kPadDown, 20);
        int peak = 0;
        int16_t out[2 * 512];
        for (int block = 0; block < 12; ++block) {
            r.loop(2);
            r.app.fill(out, 512);
            for (int i = 0; i < 512; ++i) peak = std::max(peak, std::abs(int(out[2 * i])));
        }
        CHECK(peak > 300);                                      // heard, not a click
    }
}

TEST(the_library_hides_unloadable_files_and_play_previews_a_sound_without_touching_the_song) {
    MemStorage st;
    auto gbi = [](bool table) {
        std::vector<uint8_t> v(table ? 26 + 96 : 26, 0);
        v[0] = 'G'; v[1] = 'B'; v[2] = 'I'; v[3] = '1';
        v[4] = table ? 1 : 0;
        const uint8_t in[16] = {0x00, 0xF0, 0x00, 0x00, 0xFF, 0x00, uint8_t(table ? 0x20 : 0), 0x03, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0};
        std::memcpy(v.data() + 10, in, 16);
        if (table) v[26 + 16 + 0] = 12;                          // a table row with a transpose
        return v;
    };
    st.files["GB/Instruments/LEAD.gbi"] = gbi(true);
    st.files["GB/Instruments/PLAIN.gbi"] = gbi(false);
    st.files["GB/Instruments/._LEAD.gbi"] = std::vector<uint8_t>(4096, 0);     // a macOS resource fork: not an instrument
    st.files["GB/Instruments/SHORT.gbi"] = std::vector<uint8_t>(5, 0);        // too small to be one
    st.dirs.insert("GB/Instruments");
    Rig r(true, &st);
    gb::Lsdj& l = r.app.lsdj;
    l.openLibrary();
    CHECK_EQ(l.libCount, 2);                                    // LEAD and PLAIN only
    uint8_t before[16];
    std::memcpy(before, r.app.song.instrument(l.instr), 16);
    uint8_t tableBefore[96];
    std::memcpy(tableBefore, r.app.song.mem() + gb::off::kTblEnv, 16);
    l.libSel = 1;                                               // LEAD (its table is borrowed for the preview)
    keyTap(r, ot::btn::Play);
    r.loop(40);
    CHECK(r.app.player.voiceOn(0));                             // it sounds
    CHECK(std::memcmp(before, r.app.song.instrument(l.instr), 16) == 0);       // the song's instrument is untouched
    padEvent(r, ot::InputEvent::kPadDown, 24);
    r.loop(10);
    CHECK(r.app.player.voiceOn(0));                             // the pads play the previewed sound
    padEvent(r, ot::InputEvent::kPadUp, 24);
    keyTap(r, ot::btn::Delete);                                 // leaving the library ends the preview and gives the table slot back
    r.loop(10);
    CHECK(l.screen() == gb::Scr::Instr);
    bool clean = true;
    for (int t = 0; t < 32 && clean; ++t) for (int c = 0; c < 16; ++c) if (r.app.song.at(uint16_t(gb::off::kTblTrans + t * 16 + c))) clean = false;
    CHECK(clean);
    // Insert puts it into the song
    l.openLibrary();
    l.libSel = 2;
    keyTap(r, ot::btn::Insert);
    CHECK_EQ(int(r.app.song.instrument(l.instr)[1]), 0xF0);
}

TEST(every_export_writes_the_rom_with_the_save_of_the_same_name_even_without_kits) {
    MemStorage st;
    std::vector<uint8_t> rom(1u << 20);
    for (size_t i = 0; i < rom.size(); ++i) rom[i] = uint8_t(i * 7 + i / 0x4000);
    st.files["GB/LSDJ.GB"] = rom;
    Rig r(true, &st);
    r.app.loadKits();
    oneNote(r.app.song, 0, 25, 3);
    CHECK(r.app.exportFiles(nullptr, 0));
    CHECK_EQ(int(st.files.count("GB/Export/PTSDJ_A.srm")), 1);
    CHECK_EQ(int(st.files.count("GB/Export/PTSDJ_A.gb")), 1);   // the pair, named alike
    const auto& out = st.files["GB/Export/PTSDJ_A.gb"];
    CHECK_EQ(int(out.size()), int(rom.size()));
    int differ = 0;
    for (size_t i = 0; i < rom.size(); ++i) differ += out[i] != rom[i];
    CHECK(differ <= 2);                                         // no kits: only the two checksum bytes can change
    CHECK_EQ(int(st.files.count("GB/Export/PTSDJ_B.gb")), 0);
}

TEST(kit_numbers_map_to_rom_banks_as_lsdj_reads_them) {
    std::vector<uint8_t> rom(1u << 20, 0xFF);
    for (int b = 0; b < 64; ++b) { rom[size_t(b) * 0x4000] = 0x60; rom[size_t(b) * 0x4000 + 1] = 0x40; rom[size_t(b) * 0x4000 + 2] = uint8_t(b); }   // every bank a kit, tagged with its number
    gb::RomKits k;
    k.setRom(rom.data(), rom.size());
    auto bankOf = [&](int kit) { const gb::Kit x = k.kit(kit); return x.data ? int(x.data[2]) : -1; };
    CHECK_EQ(bankOf(0x00), 8);
    CHECK_EQ(bankOf(0x12), 26);                                 // the last kit before the program code
    CHECK_EQ(bankOf(0x13), 32);                                 // then the code banks are skipped
    CHECK_EQ(bankOf(0x14), 33);
    CHECK_EQ(bankOf(0x15), 34);
    CHECK_EQ(bankOf(0x1A), 39);
    CHECK_EQ(bankOf(0x32), 63);
    CHECK_EQ(gb::kitOfBank(39), 0x1A);
    CHECK_EQ(gb::kitOfBank(30), -1);                            // program code is no kit
    CHECK_EQ(gb::kitOfBank(34), 0x15);
}

TEST(a_kit_note_in_the_phrase_picks_a_sample_of_each_of_two_kits_and_layers_them) {
    MemStorage st;
    putWav(st, "GB/Samples/KICK.wav", 44100, 80, 0.5);
    putWav(st, "GB/Samples/SNARE.wav", 22050, 300, 0.3);
    Rig r(true, &st);
    gb::Lsdj& l = r.app.lsdj;
    tap(r, ot::btn::SampleLoader);
    keyTap(r, ot::btn::Insert);                                 // KICK = sample 1 of kit 15
    keyTap(r, ot::btn::Down);
    keyTap(r, ot::btn::Insert);                                 // SNARE = sample 2
    uint8_t* in = r.app.song.instrument(3);
    in[0] = 2; in[1] = 0xA8; in[3] = 0; in[4] = 0xFF; in[7] = 3; in[9] = 0x15; in[2] = 0x15;     // a kit sound: KIT 1 and KIT 2 both kit 15
    r.app.song.phraseInstr(0, 0) = 3;
    l.instr = 3;
    l.phrase = 0;
    l.step = 0;
    keyTap(r, ot::btn::Note);
    CHECK_EQ(int(r.app.song.note(0, 0)), 0);
    keyTap(r, ot::btn::Insert);                                 // Insert on an empty step: sample 1
    CHECK_EQ(int(r.app.song.note(0, 0)), 1);
    holdTap(l, gb::Key::A, gb::Key::Right);                     // Insert + Right: the next sample of KIT 1
    CHECK_EQ(int(r.app.song.note(0, 0)), 2);
    holdTap(l, gb::Key::A, gb::Key::Up);                        // Insert + Up: a sample of KIT 2 joins in
    CHECK_EQ(int(r.app.song.note(0, 0)), 0x12);
    holdTap(l, gb::Key::A, gb::Key::Up);
    CHECK_EQ(int(r.app.song.note(0, 0)), 0x22);
    holdTap(l, gb::Key::A, gb::Key::Left);                      // the digits do not carry into each other
    holdTap(l, gb::Key::A, gb::Key::Left);
    holdTap(l, gb::Key::A, gb::Key::Left);
    CHECK_EQ(int(r.app.song.note(0, 0)), 0x20);                 // KIT 1 sample 0 = none, KIT 2 sample 2 alone
    holdTap(l, gb::Key::A, gb::Key::Down);
    holdTap(l, gb::Key::A, gb::Key::Down);
    CHECK_EQ(int(r.app.song.note(0, 0)), 0x00);
    holdTap(l, gb::Key::A, gb::Key::Up);                        // Insert + Up on an empty step: a KIT 2 sample alone, nothing from KIT 1
    CHECK_EQ(int(r.app.song.note(0, 0)), 0x10);
    holdTap(l, gb::Key::A, gb::Key::Down);
    CHECK_EQ(int(r.app.song.note(0, 0)), 0x00);
    gb::Lcd lcd;
    r.app.song.note(0, 1) = 0x21; r.app.song.phraseInstr(0, 1) = 3;
    l.draw(lcd);                                                // both names are drawn without trouble
    // the player mixes the two samples: a layered note plays and sounds
    r.app.song.note(0, 0) = 0x21;
    r.app.song.songCell(0, 2) = 0;
    r.app.song.chainPhrase(0, 0) = 0;
    gb::Player& p = r.app.player;
    p.start(0);
    int peak = 0;
    int16_t out[2 * 441];
    for (int w = 0; w < 20; ++w) { p.render(out, 441, 44100); for (int i = 0; i < 441; ++i) peak = std::max(peak, std::abs(int(out[2 * i]))); }
    CHECK(peak > 200);
    p.stop();
}

TEST(the_recorder_waits_for_sound_records_it_and_opens_it_in_the_editor) {
    Rig r;
    gb::Lsdj& l = r.app.lsdj;
    gInputSel = -1;
    gLineAmp = 0;
    CHECK_EQ(gInputSel, -1);                                    // nothing touches the input before the page is opened
    tap(r, ot::btn::SampleRecorder);
    CHECK(l.screen() == gb::Scr::Recorder);
    CHECK_EQ(gInputSel, 0);                                     // line in
    l.recRow = 0;
    l.encoder(1);                                               // the jog wheel: mic low
    CHECK_EQ(gInputSel, 2);
    l.encoder(-1);
    CHECK_EQ(gInputSel, 0);
    l.recTrig = 1;                                              // LOW
    l.recRow = 3;
    keyTap(r, ot::btn::Insert);                                 // START: waiting for sound
    int16_t out[2 * 512];
    for (int b = 0; b < 20; ++b) { r.loop(2); r.app.fill(out, 128); }
    CHECK_EQ(r.app.recStateNow(), int(1));              // still waiting: nothing is loud enough
    gLineAmp = 12000;
    for (int b = 0; b < 40; ++b) { r.loop(2); r.app.fill(out, 128); }
    CHECK_EQ(r.app.recStateNow(), int(2));              // recording
    gb::Lcd lcd;
    l.draw(lcd);
    keyTap(r, ot::btn::Insert);                                 // STOP: keeps it
    r.loop(40);                                                 // the next frame turns it into a sample
    CHECK(l.screen() == gb::Scr::Editor);
    CHECK(l.smpLen > 100);
    CHECK(std::strcmp(l.sampleName(), "REC") == 0);
    // a long take stops by itself when the buffer is full and the sample is cut to what a kit holds
    tap(r, ot::btn::SampleRecorder);
    l.recTrig = 0;                                              // AT ONCE
    l.recRow = 3;
    keyTap(r, ot::btn::Insert);
    for (int b = 0; b < 1200; ++b) { r.app.fill(out, 128); }    // about 3.5 seconds
    CHECK_EQ(r.app.recStateNow(), int(3));
    r.loop(40);
    CHECK(l.screen() == gb::Scr::Editor);
    CHECK(l.smpLen > 14000 && l.smpLen <= gb::kKitCapacity);
    // Delete while recording throws it away
    tap(r, ot::btn::SampleRecorder);
    l.recRow = 3;
    keyTap(r, ot::btn::Insert);
    for (int b = 0; b < 10; ++b) r.app.fill(out, 128);
    keyTap(r, ot::btn::Delete);
    CHECK_EQ(r.app.recStateNow(), int(0));
    CHECK(l.screen() == gb::Scr::Recorder);
}
