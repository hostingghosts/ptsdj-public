// GB app + UI host tests over fake HALs: boot, the blank first screen, the power-hold bar, the silent audio callback and the
// FT81x display-list budget (2048 words per frame; a 100-word margin below the hardware limit is kept free).
#include <cstring>
#include <deque>

#include "atw/app.h"
#include "atw/midi_in.h"
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
    explicit Rig(bool withAudio = true) {
        Platform p;
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

TEST(first_screen_is_blank) {
    Rig r;
    r.loop(50);
    CHECK(r.display.frames > 0);
    CHECK_EQ(int(r.app.ui.current()), int(ui::ScreenId::kBlank));
    CHECK_EQ(r.display.texts, 0u);   // the last frame carries no text at all
    CHECK_EQ(int(r.display.clearR), 0x0F);
    CHECK_EQ(int(r.display.clearG), 0x38);
    CHECK_EQ(int(r.display.clearB), 0x0F);
}

TEST(splash_comes_first_then_the_blank_screen) {
    Rig r;
    r.app.ui.showSplash(300);
    r.loop(100);
    CHECK(r.display.texts > 0);      // the splash shows its name
    r.loop(400);
    CHECK_EQ(r.display.texts, 0u);   // then nothing
}

TEST(blank_screen_is_redrawn_at_least_once_a_second) {
    Rig r;
    r.loop(50);
    const uint32_t before = r.display.frames;
    r.loop(1100);
    CHECK(r.display.frames > before);
}

TEST(audio_is_started_and_silent) {
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

TEST(other_keys_leave_the_screen_blank) {
    Rig r;
    for (uint8_t id : {b::F1, b::F8, b::Shift, b::Play, b::Rec, b::Help}) {
        r.push(ot::InputEvent::kButtonDown, id);
        r.push(ot::InputEvent::kButtonUp, id);
    }
    r.push(ot::InputEvent::kPadDown, 3);
    r.push(ot::InputEvent::kEncoder, 0);
    r.loop(100);
    CHECK_EQ(r.display.texts, 0u);
    CHECK_EQ(int(r.app.ui.current()), int(ui::ScreenId::kBlank));
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
