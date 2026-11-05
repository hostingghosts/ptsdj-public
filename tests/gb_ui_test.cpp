// GB app + UI host tests over fake HALs: boot, the blank first screen, the power-hold bar, the silent audio callback and the
// FT81x display-list budget (2048 words per frame; a 100-word margin below the hardware limit is kept free).
#include <cstring>
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

struct Rig {
    CountingDisplay display;
    FakeInput input;
    FakeClock clock;
    FakeAudio audio;
    App app;
    explicit Rig(bool withAudio = true, ot::IStorage* st = nullptr) {
        Platform p;
        p.storage = st;
        p.display = &display;
        p.input = &input;
        p.clock = &clock;
        p.audio = withAudio ? &audio : nullptr;
        p.muteOutput = &muteStub;
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
    CHECK_EQ(r.display.texts, 0u);                     // no CMD_TEXT at all: the screen is one bitmap
    CHECK(r.display.maxWords < 100);                   // and the display list stays tiny
}

TEST(splash_comes_first_then_the_lcd) {
    Rig r;
    r.app.ui.showSplash(300);
    r.loop(100);
    CHECK(r.display.texts > 0);
    r.loop(400);
    CHECK_EQ(r.display.texts, 0u);
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
    CHECK_EQ(r.display.texts, 0u);
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

TEST(project_screen_saves_loads_and_exports_the_session) {
    MemStorage st;
    Rig r(true, &st);
    oneNote(r.app.song, 0, 25, 3);
    r.app.lsdj.show(gb::Scr::Project);
    auto tap = [&](gb::Key k) { r.app.lsdj.key(k, true); r.app.lsdj.key(k, false); };
    for (int i = 0; i < 9; ++i) tap(gb::Key::Down);
    tap(gb::Key::A);
    CHECK(std::strcmp(r.app.lsdj.status(), "DONE") == 0);
    CHECK(st.files.count("GB/Songs/SESSION_A.lsdsng") == 1 && st.files.count("GB/Songs/SESSION_B.lsdsng") == 1);
    tap(gb::Key::Down); tap(gb::Key::A);                       // LOAD SESSION
    CHECK(std::strcmp(r.app.lsdj.status(), "DONE") == 0);
    tap(gb::Key::Down); tap(gb::Key::A);                       // EXPORT WAV
    CHECK(std::strcmp(r.app.lsdj.status(), "DONE") == 0);
    CHECK(st.files["GB/Export/SESSION.wav"].size() > 44u);
}
