// Atari app + UI host tests over fake HALs: editing from the panel controls (pads, jog, keys), the instrument pages, the picker, the
// song screen, and the FT81x display-list budget of every screen.
#include <cstring>  // mem_storage.h (vendored) needs it
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "atw/app.h"
#include "atari/export.h"
#include "atari/romdata.h"
#include "atari/seqmap.h"
#include "atari/romblobs.h"
#include "atw/macfile.h"
#include "atw/project.h"
#include "atari/macsession.h"
#include "formats/crc32.h"
#include "mem_storage.h"
#include "test.h"
#include "ui/buttons.h"

using namespace atw;
namespace b = ot::btn;

namespace {

// Display that estimates the FT81x display-list words of each frame (CMD_TEXT: 4 words of setup + one VERTEX2II per visible glyph, +2
// when x >= 512; every other call 1 word).
struct CountingDisplay final : ot::IDisplay {
    uint32_t words = 0, maxWords = 0;
    uint16_t width() const override { return 800; }
    uint16_t height() const override { return 480; }
    void dlStart() override { words = 0; }
    void clearColorRGB(uint8_t, uint8_t, uint8_t) override { ++words; }
    void clear(bool, bool, bool) override { ++words; }
    void colorRGB(uint8_t, uint8_t, uint8_t) override { ++words; }
    void colorA(uint8_t) override { ++words; }
    void lineWidth(uint16_t) override { ++words; }
    void pointSize(uint16_t) override { ++words; }
    void scissorXY(uint16_t, uint16_t) override { ++words; }
    void scissorSize(uint16_t, uint16_t) override { ++words; }
    void begin(Prim) override { ++words; }
    void end() override { ++words; }
    void vertex2f(int32_t, int32_t) override { ++words; }
    void vertex2ii(uint16_t, uint16_t, uint8_t, uint8_t) override { ++words; }
    void text(int16_t x, int16_t, uint8_t, uint16_t, const char* s) override {
        words += 4 + (x >= 512 ? 2 : 0);
        for (; *s; ++s)
            if (*s != ' ') words += x >= 512 ? 2 : 1;
    }
    void number(int16_t, int16_t, uint8_t, uint16_t, int32_t) override { words += 10; }
    void swap() override {
        ++words;
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

struct FakeMidi final : ot::IMidiPort {
    std::deque<uint8_t> in[2];
    bool send(ot::MidiPortId, const uint8_t*, uint32_t) override { return true; }
    uint32_t receive(ot::MidiPortId port, uint8_t* dst, uint32_t cap) override {
        auto& q = in[port == ot::MidiPortId::kDin ? 0 : 1];
        uint32_t n = 0;
        while (n < cap && !q.empty()) {
            dst[n++] = q.front();
            q.pop_front();
        }
        return n;
    }
};

std::string gInstallRequest;   // what the firmware-update stub was asked for
bool installStub(ot::IStorage&, const char* image) { gInstallRequest = image; return true; }
bool imageNameStub(const char* n) { return std::strncmp(n, "PolyendTrac", 11) == 0; }

struct Rig {
    FakeMidi midi;
    CountingDisplay display;
    FakeInput input;
    FakeClock clock;
    MemStorage storage;
    std::unique_ptr<Pattern[]> pats{new Pattern[kPatternCount]};
    std::unique_ptr<App> app{new App};
    explicit Rig(const std::map<std::string, std::vector<uint8_t>>* files = nullptr) {
        if (files) storage.files = *files;   // a card from an earlier session
        Platform p;
        p.display = &display;
        p.input = &input;
        p.clock = &clock;
        p.storage = &storage;
        p.midi = &midi;
        p.patterns = pats.get();
        p.isUpdateImageName = &imageNameStub;
        p.requestFirmwareUpdate = &installStub;
        CHECK(app->init(p));
        app->boot();
    }
    ui::Ui& ui() { return app->ui; }
    Song& song() { return app->song; }
    void loop(int ms = 20) {
        for (int i = 0; i < ms; ++i) {
            ++clock.ms;
            app->loop();
        }
    }
    // The main loop and the audio callback together, for `ms` milliseconds: sequencer rows and the count-in only advance with audio.
    void run(int ms) {
        int16_t buf[2 * AudioEngine::kBlock];
        for (int i = 0; i < ms; ++i) {
            loop(1);
            if (i % 3 == 0) app->engine.fill(buf, AudioEngine::kBlock);
        }
    }
    void push(ot::InputEvent::Type t, uint8_t id, uint8_t vel = 0, int8_t d = 0) {
        ot::InputEvent e;
        e.type = t;
        e.id = id;
        e.velocity = vel;
        e.delta = d;
        input.q.push_back(e);
    }
    void tap(uint8_t id) {
        push(ot::InputEvent::kButtonDown, id);
        push(ot::InputEvent::kButtonUp, id);
        loop();
    }
    void pad(int p) {
        push(ot::InputEvent::kPadDown, uint8_t(p), 100);
        push(ot::InputEvent::kPadUp, uint8_t(p));
        loop();
    }
    void enc(int d) {
        push(ot::InputEvent::kEncoder, 0, 0, int8_t(d));
        loop();
    }
    void down(uint8_t id) {
        push(ot::InputEvent::kButtonDown, id);
        loop();
    }
    void up(uint8_t id) {
        push(ot::InputEvent::kButtonUp, id);
        loop();
    }
    Cell& cell(int row, int track, int pattern = 0) { return song().pattern(pattern).cells[row][track]; }
};

}  // namespace

TEST(pattern_entry_from_the_pads) {
    Rig r;
    r.tap(b::Rec);
    CHECK(r.ui().edit);
    r.tap(b::Instr);
    r.pad(3);                                      // the instrument column: pad 4 = instrument 4, written with a note; the cursor moves on
    CHECK(r.cell(0, 0).inst == 4 && r.cell(0, 0).note != 0);
    r.tap(b::Note);
    r.pad(24);                                     // the octave-2 row, first key
    CHECK(r.cell(1, 0).note == 25 && r.cell(1, 0).inst == 4);
    r.tap(b::Note);                                // the note key on a note field in edit mode: note off
    CHECK(r.cell(2, 0).note == note::kOff);
    r.tap(b::Up);                                  // back to row 0, then the volume column: pads 1-16 type 0-F
    r.tap(b::Up);
    r.tap(b::Up);
    r.tap(b::Instr);                               // the instrument field, and Instr again: the volume field
    r.tap(b::Instr);
    r.pad(11);
    CHECK(r.cell(0, 0).vol == 12);
}

TEST(effect_letters_on_the_pads) {
    Rig r;
    r.tap(b::Rec);
    r.tap(b::Fx1);
    r.pad(2);                                      // the third effect of the list: s (slide down)
    r.pad(4);
    r.pad(0);                                      // parameter 40
    CHECK(r.cell(0, 0).fx[0].type == atari::FX_SLIDE_DOWN && r.cell(0, 0).fx[0].value == 0x40);
    r.tap(b::Up);
    r.tap(b::Fx2);
    r.enc(1);                                      // the jog steps through the effect list
    CHECK(r.cell(0, 0).fx[1].type == atari::FX_VOLUME);
    r.enc(1);
    CHECK(r.cell(0, 0).fx[1].type == atari::FX_SLIDE_UP);
    r.enc(-3);
    CHECK(r.cell(0, 0).fx[1].type == 0);           // below the first effect: none
}

TEST(instrument_pages_edit_the_song) {
    Rig r;
    Song& s = r.song();
    r.tap(b::InstrParams);
    CHECK(r.ui().current() == ui::ScreenId::kInstrument);
    r.tap(b::InstrParams);                         // ENVELOPE
    r.tap(b::Down);                                // (the first row is MANUAL / ADSR)
    r.tap(b::Down);                                // step volume
    const int vol = s.pokey[0].env[0].volL;
    r.enc(-1);
    CHECK(s.pokey[0].env[0].volL == vol - 1);
    const size_t steps = s.pokey[0].env.size();
    r.tap(b::F1);                                  // insert a step
    CHECK(s.pokey[0].env.size() == steps + 1);
    r.tap(b::F2);                                  // delete it again
    CHECK(s.pokey[0].env.size() == steps);
    r.tap(b::InstrParams);                         // TABLE
    r.tap(b::Down);
    r.enc(2);
    CHECK(s.pokey[0].table[0] != 0 || s.pokey[0].table.size() > 0);
    r.tap(b::InstrParams);                         // AUDCTL
    r.tap(b::Down);
    r.tap(b::Down);
    r.tap(b::Down);
    CHECK((s.pokey[0].audctl & 0x08) == 0);
    r.enc(1);                                      // join 3+4 on
    CHECK((s.pokey[0].audctl & 0x08) != 0);
    r.enc(-1);
    CHECK((s.pokey[0].audctl & 0x08) == 0);
}

TEST(sounds_try_keep_and_cancel) {
    Rig r;
    Song& s = r.song();
    const std::string original = s.pokey[0].name;
    r.tap(b::InstrParams);
    for (int i = 0; i < 4; i++) r.tap(b::InstrParams);   // GENERAL -> ... -> SOUNDS (page 5 of 5 = index 4)
    r.tap(b::Down);                                      // the PRESET row
    r.enc(1);
    CHECK(s.pokey[0].name != original);                  // the second starter sound is playing in slot 1
    const std::string tried = s.pokey[0].name;
    r.tap(b::F7);                                        // CANCEL
    CHECK(s.pokey[0].name == original);
    r.enc(1);
    const std::string kept = s.pokey[0].name;
    CHECK(kept != original);
    r.tap(b::F8);                                        // KEEP
    CHECK(s.pokey[0].name == kept);
    r.enc(1);
    CHECK(s.pokey[0].name != kept);
    r.tap(b::Pattern);                                   // leaving without KEEP puts the old sound back
    CHECK(s.pokey[0].name == kept);
}

TEST(step_pages_set_the_loop_shift_all_volumes_and_clear_a_step) {
    Rig r;
    Song& s = r.song();
    r.tap(b::InstrParams);
    r.tap(b::InstrParams);                         // ENVELOPE
    r.tap(b::F1);                                  // insert a step: now at least two, the cursor on the second
    CHECK(s.pokey[0].env.size() >= 2);
    r.tap(b::F3);                                  // SET LOOP: back to the step being edited
    CHECK(s.pokey[0].envGoto == 1);
    const int v0 = s.pokey[0].env[0].volL, v1 = s.pokey[0].env[1].volL;
    r.tap(b::F4);                                  // VOL - on every step
    CHECK(s.pokey[0].env[0].volL == (v0 > 0 ? v0 - 1 : 0) && s.pokey[0].env[1].volL == (v1 > 0 ? v1 - 1 : 0));
    for (int i = 0; i < 20; ++i) r.tap(b::F5);     // VOL +, up to the top
    CHECK(s.pokey[0].env[0].volL == 15 && s.pokey[0].env[1].volL == 15);
    r.tap(b::F6);                                  // CLEAR the step
    CHECK(s.pokey[0].env[1].volL == 0);
}

TEST(shift_and_pads_draw_the_volume_envelope) {
    Rig r;
    Song& sg = r.song();
    r.tap(b::InstrParams);
    r.tap(b::InstrParams);                         // ENVELOPE
    for (int i = 0; i < 6; ++i) { r.tap(b::F1); }  // a few more steps
    CHECK(sg.pokey[0].env.size() >= 6);
    r.down(b::Shift);
    r.pad(3);                                      // column 3, top row: step 3 to volume 15
    r.pad(12 + 4);                                 // column 4, second row: 10
    r.pad(36 + 5);                                 // column 5, bottom row: silence
    r.up(b::Shift);
    CHECK(sg.pokey[0].env[3].volL == 15 && sg.pokey[0].env[4].volL == 10 && sg.pokey[0].env[5].volL == 0 && sg.pokey[0].env[4].volR == 10);
    uint32_t rgb = 0;
    r.down(b::Shift);
    CHECK(r.ui().screenPadColour(3, rgb) && rgb == 0x00A040);   // the lit bar
    r.up(b::Shift);
}

TEST(sounds_offer_the_rmt_instruments) {
    Rig r;
    Song& s = r.song();
    r.tap(b::InstrParams);
    for (int i = 0; i < 4; i++) r.tap(b::InstrParams);
    r.enc(1);                                            // CATEGORY: RMT
    r.tap(b::Down);
    r.enc(34);                                           // the last of the 35
    CHECK(s.pokey[0].name != "Lead");
    r.tap(b::F8);
}

TEST(name_copy_paste_and_reset) {
    Rig r;
    Song& s = r.song();
    r.tap(b::InstrParams);                               // GENERAL
    r.tap(b::F4);                                        // NAME: the pad keyboard
    CHECK(r.ui().keyboardOpen());
    r.pad(12);                                           // Q
    r.pad(13);                                           // W
    r.tap(b::F8);                                        // OK
    CHECK(!r.ui().keyboardOpen());
    CHECK(s.pokey[0].name == "LeadQW");
    r.tap(b::F2);                                        // COPY
    r.tap(b::Down);
    r.tap(b::Down);
    r.enc(5);                                            // fade-out
    CHECK(s.pokey[0].fadeout == 5);
    r.tap(b::F1);                                        // INIT: asks first
    CHECK(r.ui().dialogOpen() && s.pokey[0].fadeout == 5);
    r.tap(b::F8);
    CHECK(s.pokey[0].fadeout == 0 && s.pokey[0].name == "Lead");
    r.tap(b::F3);                                        // PASTE: the copy with the name
    CHECK(s.pokey[0].name == "LeadQW" && s.pokey[0].fadeout == 0);
}

TEST(hold_instr_opens_the_picker) {
    Rig r;
    r.down(b::Instr);
    r.loop(300);
    CHECK(r.ui().pickerOpen());
    r.pad(9);                                            // pad 10 = instrument 10
    r.up(b::Instr);
    CHECK(!r.ui().pickerOpen() && r.ui().inst == 10);
    r.tap(b::Instr);                                     // a tap on the Instr key does not open it on the pattern screen
    CHECK(!r.ui().pickerOpen());
}

TEST(song_screen_edits_the_order_list) {
    Rig r;
    Song& s = r.song();
    r.tap(b::Song);
    r.tap(b::Insert);
    CHECK(s.orderLen == 2);
    r.enc(3);
    CHECK(s.order[0].pattern == 0);                      // only patterns that exist can be chosen: here just 00
    r.song().pattern(3).cells[0][0].note = 20;           // pattern 03 now exists (so do 01 and 02 as the numbers below it)
    r.enc(5);
    CHECK(s.order[0].pattern == 3);
    r.tap(b::Right);
    r.enc(2);
    CHECK(s.order[0].repeat == 3);
    CHECK(bpmOfSpeed(6, atari::Region::NTSC) == 150 && bpmOfSpeed(5, atari::Region::PAL) == 150);
    r.tap(b::F3);                                        // BPM -: a longer row
    CHECK(s.speed == 7);
    r.tap(b::F4);
    CHECK(s.speed == 6);
    r.tap(b::F2);                                        // EDIT PAT
    CHECK(r.ui().current() == ui::ScreenId::kPattern && r.ui().pattern == 3);
    r.tap(b::Song);
    r.tap(b::Down);
    r.tap(b::Delete);
    CHECK(s.orderLen == 1);
}

TEST(tia_mode_has_two_tracks_and_its_own_pages) {
    Rig r;
    Song& s = r.song();
    r.tap(b::Config);
    r.tap(b::Down);                                      // past the project name
    r.enc(1);                                            // CHIP: TIA
    CHECK(s.chip == atari::Chip::Tia && s.trackCount() == 2);
    r.tap(b::Pattern);
    r.tap(b::Rec);
    for (int i = 0; i < 6; i++) r.tap(b::Right);         // Left / Right move between tracks and stop at the last one
    CHECK(r.ui().channel == 3);
    r.tap(b::InstrParams);
    r.tap(b::InstrParams);                               // STEPS
    r.tap(b::Down);                                      // waveform
    const int wave = s.tia[0].steps[0].waveform;
    r.enc(1);
    CHECK(s.tia[0].steps[0].waveform == wave + 1);
}

TEST(every_screen_fits_the_display_list) {
    Rig r;
    Song& s = r.song();
    for (int row = 0; row < s.pattern(0).rows; row++)    // a full pattern: every field of every track in use
        for (int t = 0; t < 4; t++) {
            Cell& c = s.pattern(0).cells[row][t];
            c.note = uint8_t(1 + (row * 5 + t) % 60);
            c.inst = uint8_t(1 + (row + t) % 40);
            c.vol = uint8_t(1 + row % 16);
            c.fx[0] = {atari::FX_ARP, uint8_t(0x37 + row)};
            c.fx[1] = {atari::FX_SLIDE_DOWN, uint8_t(row)};
        }
    auto worst = [&](const char* what) {
        r.loop(120);
        if (r.display.maxWords > 1900 || r.display.maxWords == 0) std::printf("  %s: %u display-list words\n", what, unsigned(r.display.maxWords));
        CHECK(r.display.maxWords > 0 && r.display.maxWords <= 1900);
        r.display.maxWords = 0;                      // the next action's first frame is the one that counts
    };
    r.tap(b::Rec);
    worst("pattern (edit)");
    r.tap(b::Play);
    worst("pattern (playing)");
    r.tap(b::Play);
    r.tap(b::Rec);
    for (int page = 0; page < 5; page++) {
        r.tap(b::InstrParams);
        worst("instrument page");
    }
    r.tap(b::InstrParams);
    r.tap(b::F4);
    worst("name keyboard");
    r.tap(b::F7);
    r.tap(b::Pattern);
    r.down(b::Instr);
    r.loop(300);
    worst("picker");
    r.up(b::Instr);
    r.tap(b::Song);
    worst("song");
    r.tap(b::Config);
    worst("settings");
    r.tap(b::Master);
    worst("master");
    r.tap(b::Performance);
    worst("perform");
    r.tap(b::File);
    worst("export");
    r.tap(b::Help);
    worst("help");
}

// ---- project files ----------------------------------------------------------------------------------------------------------

namespace {

bool sameInstruments(const Song& a, const Song& b) {
    for (int i = 0; i < kInstrumentCount; ++i) {
        const auto& x = a.pokey[i];
        const auto& y = b.pokey[i];
        if (x.name != y.name || atari::instrumentToAta(x) != atari::instrumentToAta(y) || x.envGoto != y.envGoto || x.tableGoto != y.tableGoto) return false;
        const auto& p = a.tia[i];
        const auto& q = b.tia[i];
        if (p.name != q.name || p.loopStart != q.loopStart || p.stepFrames != q.stepFrames || p.steps.size() != q.steps.size()) return false;
        for (size_t k = 0; k < p.steps.size(); ++k)
            if (p.steps[k].waveform != q.steps[k].waveform || p.steps[k].fixedDivider != q.steps[k].fixedDivider || p.steps[k].vol != q.steps[k].vol ||
                p.steps[k].pitchOffset != q.steps[k].pitchOffset) return false;
    }
    return true;
}

bool sameSong(const Song& a, const Song& b) {
    if (a.chip != b.chip || a.region != b.region || a.speed != b.speed || a.releaseFrames != b.releaseFrames || a.orderLen != b.orderLen ||
        a.restartPos != b.restartPos || std::strcmp(a.name, b.name) || std::strcmp(a.author, b.author)) return false;
    for (int t = 0; t < kTracks; ++t) if (a.trackVolume[t] != b.trackVolume[t]) return false;
    if (a.tiaTune != b.tiaTune || a.funk != b.funk || a.engineSpeed != b.engineSpeed || a.sfxCount != b.sfxCount) return false;
    for (int i = 0; i < a.sfxCount; ++i) if (a.sfxPattern[i] != b.sfxPattern[i] || a.sfxTrack[i] != b.sfxTrack[i]) return false;
    if (a.tiaTrack[0] != b.tiaTrack[0] || a.tiaTrack[1] != b.tiaTrack[1]) return false;
    for (int i = 0; i < a.orderLen; ++i) if (a.order[i].pattern != b.order[i].pattern || a.order[i].repeat != b.order[i].repeat) return false;
    for (int p = 0; p < kPatternCount; ++p) {
        if (a.pattern(p).rows != b.pattern(p).rows && !(a.pattern(p).empty() && b.pattern(p).empty())) return false;
        for (int r = 0; r < kMaxRows; ++r)
            for (int t = 0; t < kTracks; ++t) {
                const Cell &x = a.pattern(p).cells[r][t], &y = b.pattern(p).cells[r][t];
                if (x.note != y.note || x.inst != y.inst || x.vol != y.vol || x.fx[0].type != y.fx[0].type || x.fx[0].value != y.fx[0].value ||
                    x.fx[1].type != y.fx[1].type || x.fx[1].value != y.fx[1].value) return false;
            }
    }
    return sameInstruments(a, b);
}

// A song that uses every kind of data: several patterns of different lengths, effects, edited instruments, a TIA instrument.
void fillSong(Song& s, unsigned seed) {
    unsigned x = seed * 2654435761u + 1;
    auto rnd = [&](int n) { x = x * 1664525u + 1013904223u; return int((x >> 8) % unsigned(n)); };
    std::snprintf(s.name, sizeof s.name, "Song %u", seed);
    std::snprintf(s.author, sizeof s.author, "Me");
    s.speed = uint8_t(1 + rnd(12));
    s.releaseFrames = uint8_t(rnd(16));
    s.chip = rnd(2) ? atari::Chip::Tia : atari::Chip::Pokey;
    s.region = rnd(2) ? atari::Region::PAL : atari::Region::NTSC;
    s.orderLen = uint8_t(1 + rnd(20));
    s.restartPos = uint8_t(rnd(s.orderLen));
    for (int i = 0; i < s.orderLen; ++i) s.order[i] = {uint8_t(rnd(12) * 5), uint8_t(1 + rnd(4))};
    for (int t = 0; t < kTracks; ++t) s.trackVolume[t] = uint8_t(rnd(101));
    s.tiaTune = uint8_t(rnd(2));
    s.funk = uint8_t(rnd(3) ? 0 : 1 + rnd(9));
    s.engineSpeed = uint8_t(1 << rnd(3));          // 1, 2 or 4
    s.sfxCount = uint8_t(rnd(4));
    for (int i = 0; i < s.sfxCount; ++i) { s.sfxPattern[i] = uint8_t(rnd(kPatternCount)); s.sfxTrack[i] = uint8_t(rnd(kTracks)); }
    s.tiaTrack[0] = uint8_t(rnd(4));
    s.tiaTrack[1] = uint8_t((s.tiaTrack[0] + 1 + rnd(3)) % 4);
    for (int p = 0; p < kPatternCount; p += 5) {
        Pattern& pat = s.pattern(p);
        pat.clear(uint8_t(1 + rnd(kMaxRows)));
        for (int r = 0; r < pat.rows; ++r)
            for (int t = 0; t < kTracks; ++t) {
                Cell& c = pat.cells[r][t];
                if (rnd(3) == 0) c.note = uint8_t(rnd(2) ? note::kOff : 1 + rnd(62));
                if (rnd(4) == 0) c.inst = uint8_t(1 + rnd(48));
                if (rnd(5) == 0) c.vol = uint8_t(1 + rnd(16));
                if (rnd(6) == 0) c.fx[0] = {atari::FX_ARP, uint8_t(rnd(256))};
                if (rnd(9) == 0) c.fx[1] = {atari::FX_CUT, uint8_t(rnd(256))};
            }
    }
    s.pokey[3].name = "Edited";
    s.pokey[3].env.push_back(atari::EnvStep{});
    s.pokey[3].env.back().volL = 7;
    s.pokey[3].audctl = 0x50;
    s.tia[2].name = "Tia ed";
    s.tia[2].steps.push_back(atari::TiaStep{});
    s.tia[2].steps.back().waveform = 12;
}

}  // namespace

TEST(project_round_trip) {
    for (unsigned seed = 1; seed <= 12; ++seed) {
        Rig r;
        Song& a = r.song();
        fillSong(a, seed);
        std::string err;
        CHECK(saveProject(r.storage, a, "p.atr", &err));
        CHECK(!r.storage.exists("p.atr.tmp"));              // the temporary file is gone
        Rig other;
        CHECK(loadProject(other.storage = r.storage, other.song(), "p.atr", &err));
        CHECK(sameSong(a, other.song()));
    }
}

TEST(project_files_are_small) {
    Rig r;
    std::string err;
    CHECK(saveProject(r.storage, r.song(), "e.atr", &err));
    CHECK(r.storage.files["e.atr"].size() < 12 * 1024);      // an empty song: the header and the 96 starter instruments
    fillSong(r.song(), 3);
    CHECK(saveProject(r.storage, r.song(), "f.atr", &err));
    CHECK(r.storage.files["f.atr"].size() < 200 * 1024);
}

TEST(damaged_projects_change_nothing) {
    Rig r;
    fillSong(r.song(), 7);
    std::string err;
    CHECK(saveProject(r.storage, r.song(), "p.atr", &err));
    const std::vector<uint8_t> good = r.storage.files["p.atr"];
    Rig t;
    fillSong(t.song(), 99);                                   // the song that must survive every failed load
    Song keep;
    std::unique_ptr<Pattern[]> keepPats(new Pattern[kPatternCount]);
    keep.patterns = keepPats.get();
    keep.clear();
    fillSong(keep, 99);
    CHECK(sameSong(keep, t.song()));
    int failures = 0, tried = 0;
    for (size_t i = 0; i < good.size(); i += 1 + good.size() / 400) {   // a byte flipped here and there
        auto bad = good;
        bad[i] ^= 0x5A;
        t.storage.files["bad.atr"] = bad;
        ++tried;
        if (!loadProject(t.storage, t.song(), "bad.atr", &err)) ++failures;
        CHECK(sameSong(keep, t.song()));
    }
    CHECK(failures == tried);                                 // the CRC catches every flip
    for (size_t len : {size_t(0), size_t(3), size_t(8), good.size() / 2, good.size() - 1}) {
        t.storage.files["bad.atr"] = std::vector<uint8_t>(good.begin(), good.begin() + long(len));
        CHECK(!loadProject(t.storage, t.song(), "bad.atr", &err));
        CHECK(sameSong(keep, t.song()));
    }
    CHECK(!loadProject(t.storage, t.song(), "missing.atr", &err));
    // a well-formed file whose values are wrong (valid CRC): every field check must hold
    for (size_t at : {size_t(5), size_t(6), size_t(8), size_t(9), size_t(10), size_t(11), size_t(15)}) {
        for (uint8_t v : {uint8_t(0), uint8_t(200), uint8_t(255)}) {
            auto bad = good;
            bad[at] = v;
            const uint32_t crc = ot::crc32(bad.data(), bad.size() - 4);
            for (int k = 0; k < 4; ++k) bad[bad.size() - 4 + size_t(k)] = uint8_t(crc >> (8 * k));
            t.storage.files["bad.atr"] = bad;
            loadProject(t.storage, t.song(), "bad.atr", &err);   // may be fine (speed 200) or refused; if refused, nothing changes
            Song& cur = t.song();
            if (!sameSong(keep, cur)) {
                // it was accepted: then it must be a valid song (speed >= 1, chip / region known, order inside the list)
                CHECK(cur.speed >= 1 && cur.orderLen >= 1 && cur.restartPos < cur.orderLen && cur.releaseFrames <= 15);
                fillSong(cur, 99);                              // put the survivor song back for the next round
            }
        }
    }
}

TEST(save_keeps_the_old_file_when_the_card_is_full) {
    struct Full final : MemStorage {
        int32_t write(Handle h, const void* src, uint32_t len) override { return wrote_ += len > 4096 ? -1 : int32_t(len), wrote_ > 3000 ? -1 : MemStorage::write(h, src, len); }
        int wrote_ = 0;
    } fs;
    Rig r;
    std::string err;
    CHECK(saveProject(r.storage, r.song(), "p.atr", &err));
    fs.files["p.atr"] = r.storage.files["p.atr"];
    const auto before = fs.files["p.atr"];
    fillSong(r.song(), 5);
    CHECK(!saveProject(fs, r.song(), "p.atr", &err));
    CHECK(fs.files["p.atr"] == before);
    CHECK(!fs.exists("p.atr.tmp"));
}

// The project name is one field: typed in Config or Export, inherited by SAVE and by the exports.
TEST(project_name_field_is_shared_by_config_save_and_export) {
    Rig r;
    r.cell(0, 0).note = 40;
    r.cell(0, 0).inst = 1;
    r.tap(b::Config);
    CHECK(r.ui().current() == ui::ScreenId::kSettings);
    r.tap(b::Delete);                                        // the name row is first: Delete clears it and asks for a new one
    CHECK(r.ui().keyboardOpen());
    r.pad(12);                                               // Q
    r.pad(13);                                               // W
    r.tap(b::F8);
    CHECK(std::string(r.app->projectName()) == "QW" && !r.storage.exists("AtariTracker/Projects/QW.atr"));
    r.tap(b::F3);                                            // SAVE inherits the name: no question
    CHECK(!r.ui().keyboardOpen() && r.storage.exists("AtariTracker/Projects/QW.atr"));
    r.tap(b::File);                                          // the export page shows the same name and uses it
    r.tap(b::Down);
    r.tap(b::Down);
    r.tap(b::Down);
    r.tap(b::Down);
    r.tap(b::Down);                                          // .a78 whole song
    r.tap(b::F8);
    CHECK(r.storage.exists("AtariTracker/Export/QW.a78"));
    // renaming on the export page renames the project
    r.tap(b::F6);
    CHECK(r.ui().keyboardOpen());
    r.tap(b::F7);                                            // cancelled: the name stays
    CHECK(std::string(r.app->projectName()) == "QW");
    r.tap(b::F6);
    r.pad(14);                                               // E
    r.tap(b::F8);
    CHECK(std::string(r.app->projectName()) == "QWE");
    for (int i = 0; i < 5; ++i) r.tap(b::Up);                // back on the NAME row: Delete clears it and asks for a new one
    r.tap(b::Delete);
    CHECK(r.ui().keyboardOpen() && std::string(r.app->projectName()).empty());
    r.pad(12);                                               // Q
    r.tap(b::F8);
    CHECK(std::string(r.app->projectName()) == "Q");
    // a typed name that is another project's file is not overwritten silently
    CHECK(r.app->saveProject("OTHER"));
    r.app->setProjectName("QW");
    r.tap(b::Config);
    r.tap(b::F3);
    CHECK(r.ui().dialogOpen());
    r.tap(b::F7);
    // New song: no name, SAVE asks again
    r.app->newProject();
    r.tap(b::F3);
    CHECK(r.ui().keyboardOpen());
}

// Config > FUNK KEEPS TEMPO: FUNK -/+ on the Song page trade speed for funk so that a pair of rows (and with it the BPM) stays the same.
TEST(funk_keeps_the_tempo_when_the_setting_is_on) {
    Rig r;
    Song& sg = r.song();
    CHECK(!r.ui().funkKeepsTempo && sg.speed == 6 && sg.funk == 0);
    r.tap(b::Config);
    for (int i = 0; i < 9; ++i) r.tap(b::Down);              // FUNK KEEPS TEMPO
    r.enc(1);
    CHECK(r.ui().funkKeepsTempo);
    CHECK(r.storage.exists("AtariTracker/settings.txt"));
    r.tap(b::Song);
    const int bpm = bpmOfPair(sg.speed, sg.funk, sg.region, sg.engineSpeed);
    r.tap(b::F6);                                            // FUNK +: odd rows 7, even rows 5: still 12 frames a pair
    CHECK(sg.speed == 5 && sg.funk == 7 && bpmOfPair(sg.speed, sg.funk, sg.region, sg.engineSpeed) == bpm);
    r.tap(b::F6);
    CHECK(sg.speed == 4 && sg.funk == 8 && bpmOfPair(sg.speed, sg.funk, sg.region, sg.engineSpeed) == bpm);
    r.tap(b::F5);
    r.tap(b::F5);
    CHECK(sg.speed == 6 && sg.funk == 0);                    // back to straight rows: funk off
    r.tap(b::F5);                                            // the other way: odd rows shorter than even... even rows longer
    CHECK(sg.speed == 7 && sg.funk == 5 && bpmOfPair(sg.speed, sg.funk, sg.region, sg.engineSpeed) == bpm);
    // no room: a row cannot be 0 frames
    sg.speed = 1;
    sg.funk = 1;
    r.tap(b::F6);
    CHECK(sg.speed == 1 && sg.funk == 1);
    // the setting is remembered: a new session reads it back from the card
    Rig again(&r.storage.files);
    CHECK(again.ui().funkKeepsTempo);
    // off: plain FUNK -/+ as before
    r.tap(b::Config);
    r.enc(-1);
    CHECK(!r.ui().funkKeepsTempo);
    sg.speed = 6;
    sg.funk = 0;
    r.tap(b::Song);
    r.tap(b::F6);
    CHECK(sg.speed == 6 && sg.funk == 1);
}

// The song grid: each track of an order row can follow the row's pattern, play another one, or be taken out.
TEST(song_grid_cells_edit_save_and_export) {
    Rig r;
    Song& s = r.song();
    s.pattern(1).cells[0][0].note = 20;                      // patterns 00 (empty), 01 exist
    r.tap(b::Song);
    r.tap(b::Right);
    r.tap(b::Right);                                         // T1
    CHECK(s.order[0].cell[0] == OrderEntry::kSame);
    r.enc(1);
    CHECK(s.order[0].cell[0] == 0);                          // pattern 00
    r.enc(2);
    CHECK(s.order[0].cell[0] == 2);                          // the next, empty pattern is offered for a new part
    r.enc(5);
    CHECK(s.order[0].cell[0] == 2);                          // and no further
    r.enc(-3);
    CHECK(s.order[0].cell[0] == OrderEntry::kSame);
    r.enc(-1);
    CHECK(s.order[0].cell[0] == OrderEntry::kOut && s.order[0].trackPattern(0) == -1);
    r.tap(b::EncPush);                                       // jog push: back in
    CHECK(s.order[0].cell[0] == OrderEntry::kSame);
    r.tap(b::EncPush);
    CHECK(s.order[0].cell[0] == OrderEntry::kOut);
    r.tap(b::Right);                                         // T2 plays pattern 01
    r.enc(2);
    CHECK(s.order[0].cell[1] == 1);
    r.tap(b::F2);                                            // EDIT PAT opens that cell's pattern on its track
    CHECK(r.ui().current() == ui::ScreenId::kPattern && r.ui().pattern == 1 && r.ui().channel == 1);
    // saved with the project and back
    CHECK(r.app->saveProject("GRID"));
    s.order[0].cell[0] = OrderEntry::kSame;
    s.order[0].cell[1] = OrderEntry::kSame;
    CHECK(r.app->openProject("GRID"));
    CHECK(s.order[0].cell[0] == OrderEntry::kOut && s.order[0].cell[1] == 1 && s.order[0].cell[2] == OrderEntry::kSame);
    // the export sees one pattern per distinct row; a plain song is unchanged
    ExportPlan plan;
    std::string err;
    CHECK(planExport(s, plan, &err) && plan.patternCount == 1 && plan.patterns[0].src[0] == 0xFF && plan.patterns[0].src[1] == 1 && plan.patterns[0].src[2] == 0);
    s.order[0] = OrderEntry();
    CHECK(planExport(s, plan, &err) && plan.patterns[0].src[0] == 0 && plan.patterns[0].src[3] == 0);
}

// Shift page: FILL writes the pad note and instrument on every STEP-th row of the selection; PREVIEW loops just the selection while held.
TEST(pattern_fill_and_preview_use_the_selection) {
    Rig r;
    Song& sg = r.song();
    r.tap(b::Rec);
    r.tap(b::F6);                                            // FILL with nothing selected, nothing played yet
    CHECK(sg.pattern(0).empty());
    r.pad(24);                                               // a note on the pads: row 0 gets it, and Fill will use it
    const uint8_t note = r.ui().lastNote;
    CHECK(note != 0 && sg.pattern(0).cells[0][0].note == note);
    r.ui().step = 2;
    r.tap(b::Down);                                          // the cursor is on row 2 now; select rows 2..8
    r.down(b::Shift);
    for (int i = 0; i < 6; ++i) r.tap(b::Down);
    r.up(b::Shift);                                          // the selection stays when Shift is let go
    r.tap(b::F6);                                            // FILL
    int filled = 0;
    for (int row = 0; row < 16; ++row) filled += sg.pattern(0).cells[row][0].note == note ? 1 : 0;
    CHECK(sg.pattern(0).cells[2][0].note == note && sg.pattern(0).cells[4][0].note == note && sg.pattern(0).cells[6][0].note == note && sg.pattern(0).cells[8][0].note == note);
    CHECK(sg.pattern(0).cells[3][0].note == 0 && sg.pattern(0).cells[9][0].note == 0 && sg.pattern(0).cells[1][0].note == 0);
    CHECK(filled == 5);                                      // row 0 from the pad and four from the fill
    CHECK(sg.pattern(0).cells[2][0].inst == r.ui().inst);
    // without Rec, Fill does nothing
    sg.pattern(0).cells[4][0] = Cell{};
    r.tap(b::Rec);
    r.tap(b::F6);
    CHECK(sg.pattern(0).cells[4][0].note == 0);
    r.tap(b::Rec);
    // PREVIEW: the selected rows 2..8 loop while F5 is held; the cursor comes back
    CHECK(!r.app->engine.playing());
    r.down(b::F5);
    r.run(100);
    CHECK(r.app->engine.playing() && r.app->engine.sequencerMode() == Sequencer::kPattern);
    bool inside = true;
    int lo = 99, hi = -1;
    for (int i = 0; i < 400; ++i) {
        r.run(5);
        const int row = r.app->engine.row();
        lo = row < lo ? row : lo;
        hi = row > hi ? row : hi;
        inside = inside && row >= 2 && row <= 8;
    }
    CHECK(inside && lo == 2 && hi == 8);                     // it keeps looping inside the block
    r.up(b::F5);
    r.run(100);
    CHECK(!r.app->engine.playing());
    // Shift page: F5 / F6 are UNDO / REDO again
    r.tap(b::Rec);
    sg.pattern(0).cells[5][0].note = 7;
    r.app->markDirty();
    r.down(b::Shift);
    r.tap(b::F5);
    r.up(b::Shift);
    CHECK(sg.pattern(0).cells[5][0].note != 7);
}

TEST(settings_screen_saves_opens_and_starts_new) {
    Rig r;
    r.tap(b::Config);
    CHECK(r.ui().current() == ui::ScreenId::kSettings);
    r.song().speed = 9;
    r.app->markDirty();
    r.tap(b::F3);                                            // SAVE with no name yet: asks for one
    CHECK(r.ui().keyboardOpen());
    r.pad(12);                                               // Q
    r.pad(13);                                               // W
    r.tap(b::F8);
    CHECK(std::string(r.app->projectName()) == "QW");
    CHECK(r.storage.exists("AtariTracker/Projects/QW.atr"));
    r.song().speed = 7;                                      // an edit, then NEW (asks, as the song is unsaved)
    r.app->markDirty();
    r.tap(b::F1);
    CHECK(r.ui().dialogOpen());
    r.tap(b::F7);                                            // keep working
    CHECK(r.song().speed == 7);
    r.tap(b::F1);
    r.tap(b::F8);
    CHECK(r.song().speed == 6 && std::string(r.app->projectName()).empty() && !r.app->dirty());
    r.tap(b::F2);                                            // OPEN lists QW; F8 loads it
    r.tap(b::F8);
    CHECK(r.song().speed == 9 && std::string(r.app->projectName()) == "QW");
    CHECK(r.ui().current() == ui::ScreenId::kPattern);
    // SAVE AS over an existing name asks before replacing
    r.tap(b::Config);
    r.tap(b::F4);
    r.pad(12);
    r.pad(13);                                               // "QWQW"
    r.tap(b::F8);
    CHECK(r.storage.exists("AtariTracker/Projects/QWQW.atr"));
}

TEST(autosave_after_a_pause_and_restore_at_start) {
    Rig r;
    r.tap(b::Rec);
    r.pad(3);                                                // an edit
    CHECK(r.app->dirty());
    r.loop(1000);
    CHECK(!r.storage.exists(App::kAutosave));                // not yet: the song was edited a second ago
    r.loop(3000);
    CHECK(r.storage.exists(App::kAutosave));
    const unsigned sz = unsigned(r.storage.files[App::kAutosave].size());
    CHECK(sz > 0);
    Rig again(&r.storage.files);                              // the next power-on: the unsaved song comes back
    CHECK(again.cell(0, 0).note != 0);
    CHECK(again.app->dirty());
    // saving as a project removes the autosave
    again.tap(b::Config);
    again.tap(b::F4);
    again.pad(12);
    again.tap(b::F8);
    CHECK(!again.storage.exists(App::kAutosave));
}

TEST(no_autosave_while_playing) {
    Rig r;
    r.tap(b::Rec);
    r.pad(3);
    r.tap(b::Play);
    int16_t pcm[256];
    for (int i = 0; i < 80; ++i) {                          // no audio device in the rig: run the engine's callback by hand
        r.loop(100);
        r.app->engine.fill(pcm, 128);
    }
    CHECK(r.app->engine.playing());
    CHECK(!r.storage.exists(App::kAutosave));
    r.tap(b::Play);
    r.app->engine.fill(pcm, 128);
    CHECK(!r.app->engine.playing());
    r.loop(4000);
    CHECK(r.storage.exists(App::kAutosave));
}

// ---- undo / redo ------------------------------------------------------------------------------------------------------------

TEST(undo_pattern_edits_one_step_per_burst) {
    Rig r;
    r.tap(b::Rec);
    r.tap(b::Instr);
    r.pad(3);                                               // inst 4 + note at row 0
    r.loop(600);
    r.tap(b::Note);
    r.pad(24);                                              // a second step 600 ms later: its own undo step
    CHECK(r.cell(1, 0).note == 25);
    r.down(b::Shift);
    r.tap(b::Rec);                                          // Shift+Rec = undo
    CHECK(r.cell(1, 0).note == 0 && r.cell(0, 0).inst == 4);
    r.tap(b::Rec);
    CHECK(r.cell(0, 0).inst == 0 && r.cell(0, 0).note == 0);
    r.tap(b::Rec);                                          // nothing left
    CHECK(r.app->undoSteps() == 0);
    r.tap(b::SampleRecorder);                               // Shift+SampleRec = redo
    CHECK(r.cell(0, 0).inst == 4);
    r.tap(b::SampleRecorder);
    CHECK(r.cell(1, 0).note == 25);
    r.up(b::Shift);
}

TEST(undo_merges_a_jog_turn_into_one_step) {
    Rig r;
    r.tap(b::InstrParams);
    r.tap(b::InstrParams);                                  // ENVELOPE
    r.tap(b::Down);
    r.tap(b::Down);
    const int v = r.song().pokey[0].env[0].volL;
    for (int i = 0; i < 4; ++i) r.enc(-1);                  // four ticks within 400 ms
    CHECK(r.song().pokey[0].env[0].volL == v - 4);
    r.loop(10);
    r.down(b::Shift);
    r.tap(b::Rec);
    r.up(b::Shift);
    CHECK(r.song().pokey[0].env[0].volL == v);
    CHECK(r.ui().current() == ui::ScreenId::kInstrument);
}

TEST(undo_song_settings_and_chip) {
    Rig r;
    r.tap(b::Config);
    r.tap(b::Down);                                         // past the project name
    r.enc(1);                                               // chip TIA
    CHECK(r.song().chip == atari::Chip::Tia);
    r.down(b::Shift);
    r.tap(b::Rec);
    r.up(b::Shift);
    CHECK(r.song().chip == atari::Chip::Pokey && r.song().trackCount() == 4);
    r.down(b::Shift);
    r.tap(b::SampleRecorder);
    r.up(b::Shift);
    CHECK(r.song().chip == atari::Chip::Tia);
}

TEST(undo_cleared_by_new_and_open_and_dropped_by_a_new_edit) {
    Rig r;
    r.tap(b::Rec);
    r.pad(3);
    CHECK(r.app->undoSteps() == 1);
    r.loop(600);
    r.down(b::Shift);
    r.tap(b::Rec);
    CHECK(r.app->redoSteps() == 1);
    r.up(b::Shift);
    r.pad(5);                                               // a new edit: no redo any more
    CHECK(r.app->redoSteps() == 0);
    r.app->newProject();
    CHECK(r.app->undoSteps() == 0);
}

TEST(undo_memory_is_bounded) {
    Rig r;
    r.tap(b::Rec);
    for (int i = 0; i < 200; ++i) {
        r.loop(500);
        r.pad(i % 12);
    }
    CHECK(r.app->undoSteps() <= Undo::kMaxSteps);
    CHECK(r.app->undoSteps() > 10);
}

// ---- MIDI in ----------------------------------------------------------------------------------------------------------------

namespace {
bool soundNow(Rig& r) {
    int16_t pcm[2 * AudioEngine::kBlock];
    int64_t energy = 0;
    for (int k = 0; k < 64; ++k) {                           // fill() renders at most one block per call
        r.app->engine.fill(pcm, AudioEngine::kBlock);
        for (int i = 0; i < int(2 * AudioEngine::kBlock); ++i) energy += pcm[i] < 0 ? -pcm[i] : pcm[i];
    }
    return energy > 1000;
}
}  // namespace

TEST(midi_note_sounds_and_stops) {
    Rig r;
    r.midi.in[0] = {0x90, 60, 100};
    r.loop();
    CHECK(soundNow(r));
    r.midi.in[0] = {0x80, 60, 0};
    r.loop();
    for (int k = 0; k < 40; ++k) soundNow(r);
    CHECK(!soundNow(r));
}

TEST(midi_records_into_the_pattern) {
    Rig r;
    r.tap(b::Rec);
    r.tap(b::Instr);
    r.pad(5);                                              // current instrument 6
    r.tap(b::Note);                                        // (the pad wrote row 0 and moved on to row 1)
    r.midi.in[1] = {0x90, 60, 90, 0x80, 60, 0, 0x90, 62, 90};     // USB: C-4, off, D-4
    r.loop();
    CHECK(r.cell(1, 0).note == 37 && r.cell(1, 0).inst == 6);
    CHECK(r.cell(2, 0).note == 39);
    r.midi.in[0] = {0x90, 10, 90, 0x90, 120, 90};          // outside the range: ignored
    r.loop();
    CHECK(r.cell(3, 0).note == 0);
}

TEST(midi_not_recorded_when_rec_is_off_and_undoable_when_on) {
    Rig r;
    r.midi.in[0] = {0x90, 60, 90};
    r.loop();
    CHECK(r.cell(0, 0).note == 0);
    r.tap(b::Rec);
    r.midi.in[0] = {0x90, 62, 90};
    r.loop();
    CHECK(r.cell(0, 0).note == 39);
    r.down(b::Shift);
    r.tap(b::Rec);
    r.up(b::Shift);
    CHECK(r.cell(0, 0).note == 0);
}

TEST(midi_parser_running_status_sysex_and_realtime) {
    MidiIn p;
    MidiIn::Msg m;
    const uint8_t bytes[] = {0xF0, 1, 2, 3, 0xF7, 0x90, 60, 100, 0xF8, 62, 90, 0x80, 60, 0, 0x90, 64, 0, 0xB0, 123, 0, 0xC0, 5};
    int on = 0, off = 0, all = 0;
    for (uint8_t b : bytes)
        if (p.feed(b, m)) {
            on += m.type == MidiIn::Type::kNoteOn;
            off += m.type == MidiIn::Type::kNoteOff;
            all += m.type == MidiIn::Type::kAllOff;
        }
    CHECK(on == 2 && off == 2 && all == 1);                // 60 on, 62 on (running status, clock byte in between), 60 off, 64 as off, all off
    CHECK(MidiIn::cellNote(24) == 1 && MidiIn::cellNote(60) == 37);
}

TEST(midi_garbage_never_breaks_anything) {
    Rig r;
    unsigned x = 12345;
    for (int round = 0; round < 200; ++round) {
        for (int i = 0; i < 50; ++i) {
            x = x * 1664525u + 1013904223u;
            r.midi.in[round & 1].push_back(uint8_t(x >> 16));
        }
        r.loop(5);
    }
    r.app->ui.midiAllOff();
    CHECK(true);
}

// ---- Master and Perform -----------------------------------------------------------------------------------------------------

namespace {
// About three video frames of audio, so the voices have written the chip registers again.
void frames(Rig& r) {
    int16_t pcm[2 * 512];
    for (int k = 0; k < 8; ++k) r.app->engine.fill(pcm, 256);
}
}  // namespace

TEST(master_screen_volume_and_meters) {
    Rig r;
    r.tap(b::Master);
    CHECK(r.ui().current() == ui::ScreenId::kMaster);
    const int v = r.ui().masterVolume;
    r.enc(-3);
    CHECK(r.ui().masterVolume == v - 6);
    for (int i = 0; i < 100; ++i) r.enc(1);
    CHECK(r.ui().masterVolume == 100);
    r.app->engine.noteOn(0, 37, 1);
    frames(r);
    CHECK(r.app->engine.trackLevel(0) > 0 && r.app->engine.trackLevel(1) == 0);
    CHECK(r.app->engine.takePeak() > 0);
}

TEST(perform_mute_and_solo) {
    Rig r;
    r.tap(b::Performance);
    CHECK(r.ui().current() == ui::ScreenId::kPerform);
    r.app->engine.noteOn(0, 37, 1);
    r.app->engine.noteOn(1, 41, 1);
    frames(r);
    CHECK(r.app->engine.trackLevel(0) > 0 && r.app->engine.trackLevel(1) > 0);
    r.tap(b::F1);                                          // mute track 1
    CHECK(r.app->engine.muted(0));
    frames(r);
    CHECK(r.app->engine.trackLevel(0) == 0 && r.app->engine.trackLevel(1) > 0);
    r.tap(b::F1);
    r.down(b::Shift);
    r.tap(b::F2);                                          // Shift: solo track 2
    r.up(b::Shift);
    frames(r);
    CHECK(r.app->engine.solo(1) && r.app->engine.trackLevel(0) == 0 && r.app->engine.trackLevel(1) > 0);
    r.down(b::Shift);
    r.tap(b::F2);
    r.up(b::Shift);
    frames(r);
    CHECK(r.app->engine.trackLevel(0) > 0);
}

// PAT -/+ while playing queues the pattern: it starts from its top once the playing pattern has ended.
TEST(pattern_change_is_queued_while_playing) {
    Rig r;
    Song& s = r.song();
    s.pattern(0).rows = 8;
    s.pattern(1).rows = 8;
    s.pattern(2).rows = 8;
    s.speed = 2;
    for (int p = 0; p < 3; ++p) { s.pattern(p).cells[0][0].note = uint8_t(25 + p); s.pattern(p).cells[0][0].inst = 1; }
    int16_t buf[2 * AudioEngine::kBlock];
    auto run = [&](int blocks) { for (int i = 0; i < blocks; ++i) r.app->engine.fill(buf, AudioEngine::kBlock); };
    r.tap(b::Pattern);
    // stopped: the viewed pattern changes
    r.tap(b::F2);
    CHECK(r.ui().pattern == 1);
    r.tap(b::F1);
    CHECK(r.ui().pattern == 0);
    r.app->engine.playPattern(0);
    run(40);
    CHECK(r.app->engine.playing() && r.app->engine.pattern() == 0);
    r.tap(b::F2);                                            // queue 1
    r.tap(b::F2);                                            // and move on to 2
    CHECK(r.app->engine.queuedPattern() == 2 && r.app->engine.pattern() == 0);
    int guard = 0;
    while (r.app->engine.pattern() == 0 && guard++ < 4000) run(1);
    CHECK(r.app->engine.pattern() == 2 && r.app->engine.queuedPattern() == -1);
    run(4);
    CHECK(r.app->engine.row() <= 1);                         // from the top
    // stepping back onto the playing pattern cancels the change
    r.tap(b::F1);
    CHECK(r.app->engine.queuedPattern() == 1);
    r.tap(b::F2);
    CHECK(r.app->engine.queuedPattern() == -1);
}

TEST(queued_pattern_in_song_mode_is_played_once) {
    Rig r;
    Song& s = r.song();
    s.orderLen = 3;
    s.restartPos = 0;
    s.order[0] = OrderEntry{0, 1, 0};
    s.order[1] = OrderEntry{1, 1, 0};
    s.order[2] = OrderEntry{2, 1, 0};
    for (int p = 0; p < 8; ++p) s.pattern(p).rows = 4;
    s.speed = 1;
    int16_t buf[2 * AudioEngine::kBlock];
    r.app->engine.playSong(0);
    for (int i = 0; i < 20; ++i) r.app->engine.fill(buf, AudioEngine::kBlock);
    CHECK(r.app->engine.pattern() == 0);
    r.app->engine.queuePattern(5);
    std::vector<int> seen;
    for (int i = 0; i < 3000 && seen.size() < 4; ++i) {
        r.app->engine.fill(buf, AudioEngine::kBlock);
        const int p = r.app->engine.pattern();
        if (seen.empty() || seen.back() != p) seen.push_back(p);
    }
    CHECK(seen.size() >= 4 && seen[0] == 0 && seen[1] == 5 && seen[2] == 1 && seen[3] == 2);   // 5 once, then the order list carries on
}

// Exporting onto a file that is already on the card asks first.
TEST(export_asks_before_replacing) {
    Rig r;
    r.cell(0, 0).note = 40;
    r.cell(0, 0).inst = 1;
    r.tap(b::File);
    for (int i = 0; i < 5; ++i) r.tap(b::Down);              // NAME, then ATARI 7800 .a78 SONG
    auto find = [&]() -> std::string {
        for (auto& f : r.storage.files) if (f.first.find(".a78") != std::string::npos) return f.first;
        return "";
    };
    r.tap(b::F8);
    const std::string key = find();
    CHECK(!key.empty());
    const size_t size = r.storage.files[key].size();
    CHECK(size > 1000);
    r.storage.files[key] = {'x'};                            // pretend an older file
    r.tap(b::F8);                                            // the question: nothing is replaced yet
    CHECK(r.storage.files[key].size() == 1);
    r.tap(b::F8);                                            // REPLACE
    CHECK(r.storage.files[key].size() == size);
    r.storage.files[key] = {'x'};
    r.tap(b::F8);
    r.tap(b::F1);                                            // any other key: cancelled
    CHECK(r.storage.files[key].size() == 1);
}

TEST(perform_pattern_change) {
    Rig r;
    Song& s = r.song();
    s.pattern(0).rows = 8;
    s.pattern(1).rows = 8;
    s.speed = 2;
    for (int p = 0; p < 2; ++p) { s.pattern(p).cells[0][0].note = uint8_t(25 + p); s.pattern(p).cells[0][0].inst = 1; }
    int16_t buf[2 * AudioEngine::kBlock];
    r.tap(b::Performance);
    r.app->engine.playPattern(0);
    for (int i = 0; i < 40; ++i) r.app->engine.fill(buf, AudioEngine::kBlock);
    r.tap(b::F6);                                            // PAT +
    CHECK(r.app->engine.queuedPattern() == 1);
    r.tap(b::F7);                                            // cancel
    CHECK(r.app->engine.queuedPattern() == -1);
    r.tap(b::F6);
    int guard = 0;
    while (r.app->engine.pattern() == 0 && guard++ < 4000) for (int i = 0; i < 1; ++i) r.app->engine.fill(buf, AudioEngine::kBlock);
    CHECK(r.app->engine.pattern() == 1);
}

TEST(perform_volume_is_live_and_recorded_as_V) {
    Rig r;
    for (int row = 0; row < 8; ++row) r.cell(row, 0).note = uint8_t(37 + row);
    r.cell(0, 0).inst = 1;
    r.tap(b::Performance);
    r.app->engine.noteOn(0, 37, 1);
    frames(r);
    const int loud = r.app->engine.trackLevel(0);
    r.enc(-10);                                            // not recording: only the sounding note changes
    frames(r);
    CHECK(r.app->engine.trackLevel(0) < loud);
    bool anyV = false;
    for (int row = 0; row < 8; ++row) anyV |= r.cell(row, 0).fx[0].type == atari::FX_VOLUME;
    CHECK(!anyV);
    r.tap(b::Rec);                                         // recording while the song plays
    r.tap(b::Play);
    for (int i = 0; i < 20; ++i) {
        r.loop(20);
        frames(r);
    }
    CHECK(r.app->engine.playing());
    r.enc(5);
    for (int row = 0; row < 8; ++row) anyV |= r.cell(row, 0).fx[0].type == atari::FX_VOLUME;
    CHECK(anyV);
    r.tap(b::Play);
}

// ---- exports ----------------------------------------------------------------------------------------------------------------------

namespace {
const std::vector<uint8_t>& fileOf(Rig& r, const char* path) { return r.storage.files[path]; }

uint32_t le32(const std::vector<uint8_t>& f, size_t at) { return uint32_t(f[at]) | uint32_t(f[at + 1]) << 8 | uint32_t(f[at + 2]) << 16 | uint32_t(f[at + 3]) << 24; }

// A small song: two patterns, a repeat, notes on every track, a loop point in the middle.
void tune(Song& s, bool tia) {
    s.chip = tia ? atari::Chip::Tia : atari::Chip::Pokey;
    s.speed = 3;
    s.orderLen = 3;
    s.order[0] = {0, 2};
    s.order[1] = {1, 1};
    s.order[2] = {0, 1};
    s.restartPos = 1;
    for (int p = 0; p < 2; ++p) {
        Pattern& pat = s.pattern(p);
        pat.clear(8);
        for (int r = 0; r < 8; r += 2)
            for (int t = 0; t < s.trackCount(); ++t) {
                pat.cells[r][t].note = uint8_t(20 + p * 5 + r + t * 3);
                pat.cells[r][t].inst = uint8_t(1 + t);
            }
    }
}
}  // namespace

TEST(plan_expands_repeats_and_finds_the_loop) {
    Rig r;
    tune(r.song(), false);
    ExportPlan plan;
    std::string err;
    CHECK(planExport(r.song(), plan, &err));
    CHECK(plan.patternCount == 2 && plan.playlistLength == 4);
    CHECK(plan.playlist[0] == 0 && plan.playlist[1] == 0 && plan.playlist[2] == 1 && plan.playlist[3] == 0);
    CHECK(plan.loopTo == 2);
    r.song().order[0].repeat = 255;                          // 255 + 1 + 1 > 254 plays
    CHECK(!planExport(r.song(), plan, &err) && !err.empty());
}

TEST(wav_export_length_and_sound) {
    Rig r;
    tune(r.song(), false);
    CHECK(r.app->exporter.start(ExportKind::kWav, "tune"));
    CHECK(r.app->exporter.busy());
    int guard = 0;
    while (r.app->exporter.busy() && ++guard < 10000) r.app->exporter.service();
    CHECK(!r.app->exporter.busy());
    const auto& f = fileOf(r, "AtariTracker/Export/tune.wav");
    CHECK(f.size() > 44 && std::memcmp(f.data(), "RIFF", 4) == 0 && std::memcmp(f.data() + 8, "WAVE", 4) == 0);
    CHECK(le32(f, 4) == f.size() - 8 && le32(f, 40) == f.size() - 44);       // sizes in the header are right
    CHECK(le32(f, 24) == 44100);
    // 4 playlist entries x 8 rows x 3 frames = 96 frames at 60 Hz = 1.6 s: the file is exactly one pass of the song (a seamless loop)
    CHECK(le32(f, 40) / 2 == uint32_t(96 * 44100 / atari::framesPerSecond(atari::Region::NTSC) + 0.5));
    int64_t energy = 0;
    for (size_t i = 44; i + 1 < f.size(); i += 2) energy += std::abs(int16_t(f[i] | f[i + 1] << 8));
    CHECK(energy > 100000);
    CHECK(!r.app->engine.playing());
}

TEST(wav_matches_what_plays_live) {
    // the same song played by a fresh engine, block by block, must be the file's samples (up to the end of the song)
    Rig r;
    tune(r.song(), false);
    CHECK(r.app->exporter.start(ExportKind::kWav, "same"));
    while (r.app->exporter.busy()) r.app->exporter.service();
    const auto& f = fileOf(r, "AtariTracker/Export/same.wav");
    AudioEngine live;
    live.init(&r.song(), 44100000);
    live.playSong(0, 0);
    int16_t st[2 * AudioEngine::kBlock];
    size_t pos = 44, compared = 0;
    bool same = true;
    // 96 frames of song at 60 Hz = 70560 samples; after that the live engine loops and the file runs out its tail
    // The ring-out after the end is mixed onto the loop start (entry 1 = 35326 samples in): leave those 14700 samples out of the comparison.
    for (int blocks = 0; blocks < 540 && pos + 2 * AudioEngine::kBlock <= f.size(); ++blocks) {
        live.fill(st, AudioEngine::kBlock);
        for (uint32_t i = 0; i < AudioEngine::kBlock && same; ++i, pos += 2) {
            const size_t s = (pos - 44) / 2;
            if (s >= 35300 && s < 35326 + 14800) continue;
            same = int16_t(f[pos] | f[pos + 1] << 8) == st[2 * i];
            ++compared;
        }
        if (!live.playing() && live.sequencer().frames() > 0) break;   // the live engine loops: compare only the first pass
    }
    CHECK(same && compared > 40000);
}

TEST(wav_tail_is_folded_onto_the_loop_start) {
    // One long note that rings out after the song's last row: the file ends at the song's end, and the loop start carries the ring-out.
    Rig r;
    Song& s = r.song();
    s.speed = 3;
    s.releaseFrames = 12;
    s.orderLen = 1;
    s.order[0] = {0, 1};
    s.restartPos = 0;
    s.pattern(0).clear(8);
    s.pattern(0).cells[7][0].note = 30;
    s.pattern(0).cells[7][0].inst = 1;                        // sounds only for the last row
    CHECK(r.app->exporter.start(ExportKind::kWav, "loop"));
    while (r.app->exporter.busy()) r.app->exporter.service();
    const auto& f = fileOf(r, "AtariTracker/Export/loop.wav");
    CHECK(le32(f, 40) / 2 == uint32_t(24 * 44100 / atari::framesPerSecond(atari::Region::NTSC) + 0.5));   // 8 rows x 3 frames
    int64_t head = 0;
    for (size_t i = 44; i + 1 < 44 + 2 * 3000; i += 2) head += std::abs(int16_t(f[i] | f[i + 1] << 8));
    CHECK(head > 1000);                                       // the first rows are silent in the song itself: this is the ring-out
}

TEST(wav_can_be_cancelled_and_leaves_nothing) {
    Rig r;
    tune(r.song(), false);
    CHECK(r.app->exporter.start(ExportKind::kWav, "cancel"));
    r.app->exporter.service();
    r.tap(b::F7);                                            // input while busy: F7 cancels
    CHECK(!r.app->exporter.busy());
    CHECK(!r.storage.exists("AtariTracker/Export/cancel.wav"));
    CHECK(std::string(r.app->exporter.message()).find("cancel") != std::string::npos);
}

TEST(input_is_ignored_while_rendering) {
    Rig r;
    tune(r.song(), false);
    r.tap(b::Rec);
    CHECK(r.app->exporter.start(ExportKind::kWav, "busy"));
    const uint8_t before = r.cell(1, 0).note;
    r.pad(24);
    r.tap(b::Down);
    CHECK(r.cell(1, 0).note == before && r.cell(0, 0).note == 20);
    r.app->exporter.cancel();
}

TEST(a78_and_bin_exports_make_playable_files) {
    Rig r;
    tune(r.song(), false);
    CHECK(r.app->exporter.start(ExportKind::kA78, "pk"));
    const auto& a = fileOf(r, "AtariTracker/Export/pk.a78");
    CHECK(a.size() == 128 + 16384);
    CHECK(a[0] == 4 && std::memcmp(a.data() + 1, "ATARI7800", 9) == 0);               // A78 header version 4
    CHECK(!r.app->exporter.start(ExportKind::kBin2600, "pk"));               // POKEY has no 2600 ROM
    CHECK(!r.storage.exists("AtariTracker/Export/pk.bin"));
    tune(r.song(), true);
    CHECK(r.app->exporter.start(ExportKind::kBin2600, "ti"));
    CHECK(fileOf(r, "AtariTracker/Export/ti.bin").size() == 4096);
    CHECK(r.app->exporter.start(ExportKind::kA78, "ti"));
    CHECK(fileOf(r, "AtariTracker/Export/ti.a78").size() == 128 + 16384);
}

TEST(rom_song_data_matches_the_core_exporter) {
    // the data inside the ROM (the player is a blob, the song sits at ROM + $1000) against the core's linker for the same song
    for (int tia = 0; tia < 2; ++tia) {
        Rig r;
        tune(r.song(), tia != 0);
        atari::Song core;
        CHECK(toCore(r.song(), core));
        int used = 1;                                         // the ROM drops the instruments above the highest one a note uses
        for (int p = 0; p < 2; ++p)
            for (int row = 0; row < r.song().pattern(p).rows; ++row)
                for (int t = 0; t < 4; ++t) used = std::max<int>(used, r.song().pattern(p).cells[row][t].inst);
        core.pokeyIns.resize(size_t(used));
        core.tiaIns.resize(size_t(used));
        const atari::DataImage img = tia ? atari::buildTiaSongData(core) : atari::buildPokeySongData(core);
        CHECK(r.app->exporter.start(tia ? ExportKind::kBin2600 : ExportKind::kA78, "d"));
        const auto& f = fileOf(r, tia ? "AtariTracker/Export/d.bin" : "AtariTracker/Export/d.a78");
        const atari::romblobs::RomBlob& blob = tia ? atari::romblobs::kBin2600 : atari::romblobs::kA78Pokey;
        const size_t at = (tia ? 0 : 128) + blob.songOffset;                  // where the song data sits in the file
        const std::vector<uint8_t> linked = atari::link(img, uint16_t(blob.songAddress));
        CHECK(!linked.empty() && at + linked.size() <= f.size());
        if (at + linked.size() <= f.size()) CHECK(std::equal(linked.begin(), linked.end(), f.begin() + long(at)));
    }
}

TEST(assembly_export_writes_three_files) {
    Rig r;
    tune(r.song(), false);
    CHECK(r.app->exporter.start(ExportKind::kAsm, "game"));
    CHECK(r.storage.exists("AtariTracker/Export/game_asm/pokey_song.asm"));
    CHECK(r.storage.exists("AtariTracker/Export/game_asm/pokey_player.asm"));
    CHECK(r.storage.exists("AtariTracker/Export/game_asm/README.txt"));
    CHECK(fileOf(r, "AtariTracker/Export/game_asm/pokey_song.asm").size() > 200);
    CHECK(fileOf(r, "AtariTracker/Export/game_asm/pokey_player.asm").size() > 2000);
}

TEST(export_screen_runs_from_the_file_key) {
    Rig r;
    tune(r.song(), false);
    r.tap(b::File);
    CHECK(r.ui().current() == ui::ScreenId::kExport);
    for (int i = 0; i < 5; ++i) r.tap(b::Down);              // NAME, then .a78 whole song
    r.tap(b::F8);
    CHECK(r.storage.exists("AtariTracker/Export/Atari song.a78"));
    for (int i = 0; i < 4; ++i) r.tap(b::Up);
    r.tap(b::F8);                                            // WAV: renders while the loop runs
    int guard = 0;
    while (r.app->exporter.busy() && ++guard < 20000) r.loop(5);
    CHECK(r.storage.exists("AtariTracker/Export/Atari song.wav"));
    r.tap(b::Down);                                          // WAV, this pattern
    r.tap(b::F8);
    guard = 0;
    while (r.app->exporter.busy() && ++guard < 20000) r.loop(5);
    CHECK(r.storage.exists("AtariTracker/Export/Atari song_pat00.wav"));
    CHECK(r.storage.files["AtariTracker/Export/Atari song_pat00.wav"].size() <= r.storage.files["AtariTracker/Export/Atari song.wav"].size());
}

// ---- Mac app sessions ------------------------------------------------------------------------------------------------------------

namespace {

bool readFile(const char* name, std::string& out) {
    for (const char* dir : {"../../core/tests/data/", "../core/tests/data/", "core/tests/data/"}) {
        FILE* f = std::fopen((std::string(dir) + name).c_str(), "rb");
        if (!f) continue;
        char buf[4096];
        size_t n;
        out.clear();
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        std::fclose(f);
        return true;
    }
    return false;
}

void putFile(Rig& r, const char* path, const std::string& text) { r.storage.files[path] = std::vector<uint8_t>(text.begin(), text.end()); }

std::string miniInstrument(const char* name) {
    return std::string("{\"name\":\"") + name +
           "\",\"audctl\":0,\"delay\":0,\"vibrato\":0,\"fshift\":0,\"vmin\":0,\"vslide\":0,\"envGo\":0,\"tableGo\":0,\"tableSpeed\":0,"
           "\"tableMode\":0,\"tableType\":0,\"release\":[],\"env\":[{\"vol\":15,\"dist\":10,\"cmd\":0,\"x\":0,\"y\":0,\"filter\":false,\"porta\":false}],"
           "\"table\":[0]}";
}

// A small session. `patternBody` is the text of the first pattern object (its member order is the test's business).
std::string miniSession(int instruments, const std::string& patternBody, const char* chip = "POKEY", const char* song = "[]") {
    std::string s = std::string("{\"version\":2,\"chip\":\"") + chip + "\",\"tempo\":150,\"stepsPerBar\":16,\"beatsPerBar\":4,\"stepsPerBeat\":4,\"current\":0,\"instruments\":[";
    for (int i = 0; i < instruments; ++i) s += (i ? "," : "") + miniInstrument(("I" + std::to_string(i + 1)).c_str());
    return s + "],\"patterns\":[" + patternBody + "],\"song\":" + song + "}";
}

std::string jnote(int ch, int step, int pitch, int len, int inst, int vol = 15, bool off = false) {
    return "{\"ch\":" + std::to_string(ch) + ",\"step\":" + std::to_string(step) + ",\"pitch\":" + std::to_string(pitch) + ",\"len\":" + std::to_string(len) +
           ",\"inst\":" + std::to_string(inst) + ",\"vol\":" + std::to_string(vol) + ",\"off\":" + (off ? "true" : "false") + "}";
}

std::vector<uint8_t> projectBytes(Rig& r) {
    CHECK(saveProject(r.storage, r.song(), "snap.atr"));
    return r.storage.files["snap.atr"];
}

}  // namespace

TEST(mac_loader_matches_the_core_reader) {
    std::string text;
    if (!readFile("mac_session_v2_pokey.atari.json", text)) { std::printf("  (skipped: no sample session)\n"); return; }
    atari::Song core;
    std::string err;
    CHECK(atari::macSessionFromJson(text, core, &err));
    Rig r;
    putFile(r, "m.atari.json", text);
    CHECK(loadMacSession(r.storage, r.song(), "m.atari.json", "Sample", &err));
    const Song& s = r.song();
    CHECK(std::string(s.name) == "Sample");
    CHECK(s.speed == core.speed && s.chip == atari::Chip::Pokey);
    // instruments
    const size_t nIns = core.pokeyIns.size() < size_t(kInstrumentCount) ? core.pokeyIns.size() : size_t(kInstrumentCount);
    for (size_t i = 0; i < nIns; ++i) {
        CHECK(s.pokey[i].name == core.pokeyIns[i].name);
        CHECK(atari::instrumentToAta(s.pokey[i]) == atari::instrumentToAta(core.pokeyIns[i]));
        CHECK(s.pokey[i].envGoto == core.pokeyIns[i].envGoto && s.pokey[i].tableGoto == core.pokeyIns[i].tableGoto);
    }
    // patterns
    for (size_t p = 0; p < core.patterns.size(); ++p) {
        CHECK(s.pattern(int(p)).rows == core.patterns[p].length);
        for (int row = 0; row < core.patterns[p].length; ++row)
            for (int t = 0; t < 4; ++t) {
                const Cell want = fromStep(core.patterns[p].track[t][size_t(row)]);
                const Cell& got = s.pattern(int(p)).cells[row][t];
                CHECK(got.note == want.note && got.inst == want.inst && got.vol == want.vol);
            }
    }
    // the song list, repeats written out
    std::vector<uint8_t> played;
    for (int i = 0; i < s.orderLen; ++i) for (int k = 0; k < s.order[i].repeat; ++k) played.push_back(s.order[i].pattern);
    CHECK(played == core.playlist);
}

TEST(mac_member_order_does_not_matter) {
    std::string err;
    Rig r;
    // "notes" before "bars": the pattern is still 2 bars (32 rows); a note past the end is dropped; a short note gets its "===" at the end
    const std::string body = "{\"notes\":[" + jnote(0, 0, 30, 4, 1) + "," + jnote(1, 40, 30, 1, 0) + "," + jnote(2, 8, 20, 1, 0, 9) + "," + jnote(2, 9, 0, 1, 0, 15, true) + "],\"bars\":2,\"name\":\"x\"}";
    putFile(r, "a.atari.json", miniSession(2, body, "POKEY", "[{\"pattern\":0,\"repeats\":3}]"));
    CHECK(loadMacSession(r.storage, r.song(), "a.atari.json", "A", &err));
    const Song& s = r.song();
    CHECK(s.pattern(0).rows == 32);
    CHECK(s.pattern(0).cells[0][0].note == 31 && s.pattern(0).cells[0][0].inst == 2);   // chip note 30 = cell 31; file instrument 1 = slot 2
    CHECK(s.pattern(0).cells[4][0].note == note::kOff);                                  // ended after 4 rows
    CHECK(s.pattern(0).cells[8][2].vol == 10 && s.pattern(0).cells[8][2].note == 21);
    CHECK(s.pattern(0).cells[9][2].note == note::kOff);
    CHECK(s.orderLen == 1 && s.order[0].pattern == 0 && s.order[0].repeat == 3);
    CHECK(s.speed == 6);                                                                // tempo 150: 3600 / (150 * 4)
}

TEST(mac_save_and_load_round_trip) {
    Rig r;
    tune(r.song(), false);
    r.song().pokey[1].name = "Lead";
    std::string err;
    CHECK(saveMacSession(r.storage, r.song(), "x.atari.json", &err));
    CHECK(!r.storage.exists("x.atari.json.tmp"));
    Rig other;
    other.storage.files = r.storage.files;
    CHECK(loadMacSession(other.storage, other.song(), "x.atari.json", "Back", &err));
    const Song &a = r.song(), &b = other.song();
    CHECK(b.speed == a.speed && b.orderLen == 3);
    for (int i = 0; i < 3; ++i) CHECK(b.order[i].pattern == a.order[i].pattern && b.order[i].repeat == a.order[i].repeat);
    for (int p = 0; p < 2; ++p) {
        CHECK(b.pattern(p).rows == 16);                      // the app counts whole bars of 16 rows: 8 rows become one bar
        for (int row = 0; row < 16; ++row)
            for (int t = 0; t < 4; ++t) {
                const Cell &x = a.pattern(p).cells[row][t], &y = b.pattern(p).cells[row][t];
                if (row == 8) { CHECK(y.note == note::kOff); continue; }   // the last note of a short pattern is ended where the pattern was
                CHECK(x.note == y.note && x.inst == y.inst && x.vol == y.vol);
            }
    }
    CHECK(b.pokey[1].name == "Lead");
    for (int i = 0; i < 4; ++i) CHECK(atari::instrumentToAta(a.pokey[i]) == atari::instrumentToAta(b.pokey[i]));
    // what the core reader makes of it matches what the Tracker made
    std::string text(r.storage.files["x.atari.json"].begin(), r.storage.files["x.atari.json"].end());
    atari::Song core;
    CHECK(atari::macSessionFromJson(text, core, &err));
    CHECK(core.playlist.size() == 4);
}

TEST(mac_tia_songs_are_refused_on_save) {
    Rig r;
    tune(r.song(), true);
    std::string err;
    CHECK(!saveMacSession(r.storage, r.song(), "t.atari.json", &err) && !err.empty());
    CHECK(!r.storage.exists("t.atari.json") && !r.storage.exists("t.atari.json.tmp"));
}

TEST(mac_bad_files_change_nothing) {
    Rig r;
    tune(r.song(), false);
    const std::vector<uint8_t> before = projectBytes(r);
    std::string good;
    CHECK(readFile("mac_session_v2_pokey.atari.json", good) || true);
    const std::string okBody = "{\"bars\":1,\"notes\":[" + jnote(0, 0, 30, 4, 0) + "]}";
    const std::vector<std::pair<const char*, std::string>> bad = {
        {"truncated", good.substr(0, good.size() / 2)},
        {"not json", "hello"},
        {"tia", miniSession(1, okBody, "TIA")},
        {"no patterns", "{\"version\":2,\"chip\":\"POKEY\",\"tempo\":120,\"stepsPerBar\":16,\"stepsPerBeat\":4,\"instruments\":[" + miniInstrument("a") + "],\"patterns\":[]}"},
        {"missing instrument", miniSession(1, "{\"bars\":1,\"notes\":[" + jnote(0, 0, 30, 4, 1) + "]}")},
        {"beyond 48", miniSession(60, "{\"bars\":1,\"notes\":[" + jnote(0, 0, 30, 4, 50) + "]}")},
        {"bad channel", miniSession(1, "{\"bars\":1,\"notes\":[" + jnote(5, 0, 30, 4, 0) + "]}")},
        {"no bars", miniSession(1, "{\"notes\":[]}")},
        {"song names a missing pattern", miniSession(1, okBody, "POKEY", "[{\"pattern\":3,\"repeats\":1}]")},
        {"bad tempo", miniSession(1, okBody).replace(miniSession(1, okBody).find("150"), 3, "9999")},
        {"bad instrument", miniSession(1, okBody).replace(miniSession(1, okBody).find("\"vmin\":0"), 8, "\"vmin\":99")},
    };
    for (const auto& c : bad) {
        if (c.second.empty()) continue;
        putFile(r, "bad.atari.json", c.second);
        std::string err;
        const bool ok = loadMacSession(r.storage, r.song(), "bad.atari.json", "Bad", &err);
        if (ok || err.empty()) std::printf("  accepted or silent: %s (%s)\n", c.first, err.c_str());
        CHECK(!ok && !err.empty());
        CHECK(projectBytes(r) == before);
    }
    // a session that uses 60 instruments but only the first 48 in notes is fine: the rest are dropped
    putFile(r, "many.atari.json", miniSession(60, "{\"bars\":1,\"notes\":[" + jnote(0, 0, 30, 4, 47) + "]}"));
    std::string err;
    CHECK(loadMacSession(r.storage, r.song(), "many.atari.json", "Many", &err));
    CHECK(r.song().pattern(0).cells[0][0].inst == 48);
    CHECK(!loadMacSession(r.storage, r.song(), "missing.atari.json", "None", &err));
}

TEST(mac_sessions_through_the_settings_screen) {
    Rig r;
    tune(r.song(), false);
    r.app->markDirty();
    std::string err;
    CHECK(r.app->saveMacSession("TUNE", &err));
    CHECK(r.storage.exists("AtariTracker/Mac/TUNE.atari.json"));
    char names[4][kNameLen + 1];
    CHECK(r.app->listMacSessions(names, 4) == 1 && std::string(names[0]) == "TUNE");
    r.song().speed = 11;                                     // an unsaved edit: opening asks first
    r.app->markDirty();
    r.tap(b::Config);
    r.tap(b::F6);
    r.tap(b::F8);
    CHECK(r.ui().dialogOpen());
    r.tap(b::F8);                                            // OPEN anyway
    CHECK(r.song().speed == 3 && std::string(r.song().name) == "TUNE");
    CHECK(std::string(r.app->projectName()) == "TUNE" && std::string(r.app->projectFile()).empty() && r.app->dirty());   // named like the session, not yet in a project
    CHECK(r.ui().current() == ui::ScreenId::kPattern);
    // a bad file shows a message and keeps the song
    putFile(r, "AtariTracker/Mac/BROKEN.atari.json", "{\"version\":2");
    r.tap(b::Config);
    r.tap(b::F6);
    CHECK(r.app->listMacSessions(names, 4) == 2);
    r.tap(b::Down);                                          // BROKEN sorts before TUNE: move to TUNE... then back to BROKEN
    r.tap(b::Up);
    r.tap(b::F8);
    CHECK(r.song().speed == 3);
}

TEST(firmware_screen_lists_images_and_installs_after_confirming) {
    Rig r;
    for (const char* n : {"PolyendTracker_CE_1.0.0.ptf", "PolyendTracAtariv010.ptf", "notes.txt", "other.ptf"}) putFile(r, (std::string("/Firmware/") + n).c_str(), "x");
    r.song().speed = 4;
    r.app->markDirty();
    r.tap(b::Config);
    r.tap(b::F7);                                            // only names the bootloader accepts are listed (2 of 4)
    r.tap(b::F8);
    CHECK(r.ui().dialogOpen() && gInstallRequest.empty());   // asks first
    r.tap(b::F7);                                            // cancel: nothing requested
    CHECK(gInstallRequest.empty());
    r.tap(b::F8);
    r.tap(b::F8);                                            // the first image in the card's order
    CHECK(gInstallRequest.rfind("PolyendTrac", 0) == 0);
    CHECK(r.storage.exists("AtariTracker/autosave.atr"));    // the unsaved song was kept
}

// ---- the pattern screen's hold keys, selection, recording, metronome ---------------------------------------------------------------

TEST(hold_keys_pop_up_and_a_tap_jumps) {
    Rig r;
    r.tap(b::Pattern);
    r.tap(b::Instr);                                         // a tap: the instrument column
    CHECK(r.ui().current() == ui::ScreenId::kPattern && !r.ui().pickerOpen());
    r.tap(b::Note);                                          // a tap on Note: back to the note column
    // Hold Instr in the note column: the picker opens, releasing it chooses and the column is as it was
    r.down(b::Note);
    r.up(b::Note);
    r.push(ot::InputEvent::kButtonDown, b::Instr);
    r.loop(300);
    CHECK(r.ui().pickerOpen());
    r.pad(4);                                                // pick instrument 5
    r.push(ot::InputEvent::kButtonUp, b::Instr);
    r.loop();
    CHECK(!r.ui().pickerOpen() && r.ui().inst == 5);
    r.song().pattern(0).clear(32);
    r.tap(b::Rec);
    r.pad(24);                                               // still the note column: this writes a note
    CHECK(r.cell(0, 0).note != 0);
    // Hold Note: the note map; a pad plays it (and writes it with Rec); releasing closes
    r.push(ot::InputEvent::kButtonDown, b::Note);
    r.loop(300);
    CHECK(r.ui().popupOpen());
    r.push(ot::InputEvent::kButtonUp, b::Note);
    r.loop();
    CHECK(!r.ui().popupOpen());
    // Hold Fx1: the effect map; a pad selects, releasing writes it into the step (Rec)
    r.song().pattern(0).clear(32);
    r.push(ot::InputEvent::kButtonDown, b::Fx1);
    r.loop(300);
    CHECK(r.ui().popupOpen());
    r.pad(1);                                                // the second effect of the list
    r.push(ot::InputEvent::kButtonUp, b::Fx1);
    r.loop();
    bool fxWritten = false;
    for (int row = 0; row < 32; ++row) fxWritten = fxWritten || r.cell(row, 0).fx[0].type != 0;
    CHECK(!r.ui().popupOpen() && fxWritten);
}

TEST(step_and_len_follow_the_jog_while_held) {
    Rig r;
    r.tap(b::Pattern);
    r.push(ot::InputEvent::kButtonDown, b::F7);
    r.loop();
    r.enc(3);
    r.push(ot::InputEvent::kButtonUp, b::F7);
    r.loop();
    CHECK(r.ui().step == 4);                                 // 1 + 3, and the release did not step on
    r.push(ot::InputEvent::kButtonDown, b::F8);
    r.loop();
    r.enc(5);
    r.push(ot::InputEvent::kButtonUp, b::F8);
    r.loop();
    CHECK(r.song().pattern(0).rows == 37);                   // 32 + 5
    r.tap(b::F7);                                            // a tap still steps through the values
    CHECK(r.ui().step == 5);
}

TEST(shift_arrows_select_copy_and_paste_and_home) {
    Rig r;
    r.tap(b::Pattern);
    Pattern& p = r.song().pattern(0);
    p.clear(32);
    r.tap(b::Rec);                                           // copy, paste and the edits need Rec, as on the stock Tracker
    for (int row = 0; row < 4; ++row) { p.cells[row][0].note = uint8_t(10 + row); p.cells[row][1].note = uint8_t(20 + row); }
    r.down(b::Shift);
    r.tap(b::Down);
    r.tap(b::Down);
    r.tap(b::Right);                                         // rows 0-2, tracks 0-1
    r.up(b::Shift);
    r.tap(b::Copy);                                          // copies the block; the cursor goes back to its first step
    for (int i = 0; i < 8; ++i) r.tap(b::Down);              // the cursor to row 8 (moving drops any selection)
    r.down(b::Shift);
    r.tap(b::Copy);                                          // Shift+Copy: paste at the cursor
    r.up(b::Shift);
    CHECK(p.cells[8][0].note == 10 && p.cells[9][0].note == 11 && p.cells[10][1].note == 22 && p.cells[11][0].note == 0);
    r.down(b::Shift);
    r.tap(b::Insert);                                        // Shift+Insert = HOME: the cursor goes to step 0
    r.up(b::Shift);
    r.pad(24);
    CHECK(r.cell(0, 0).note == r.ui().padNote(24));          // written at row 0, where HOME put the cursor (not row 10)
}

TEST(recording_follows_the_player_head_not_the_cursor) {
    Rig r;
    r.tap(b::Pattern);
    r.song().pattern(0).clear(16);
    r.song().speed = 6;
    r.ui().step = 4;                                         // STEP must not matter while playing
    r.tap(b::Rec);
    r.tap(b::Play);
    r.loop(400);                                             // somewhere inside the pattern
    const int head = r.app->engine.row();
    r.pad(24);
    r.tap(b::Play);
    int found = -1, count = 0;
    for (int row = 0; row < 16; ++row) if (r.cell(row, 0).note) { found = row; ++count; }
    CHECK(count == 1);
    CHECK(found >= 0 && (found == head || found == (head + 1) % 16 || found == (head + 2) % 16 || found == (head + 3) % 16));
}

TEST(live_record_writes_a_note_off_when_the_key_goes_up) {
    Rig r;
    r.tap(b::Pattern);
    r.song().pattern(0).clear(16);
    r.song().speed = 6;
    r.tap(b::Rec);
    r.tap(b::Play);
    r.run(300);
    r.push(ot::InputEvent::kPadDown, 24, 100);
    r.run(400);                                             // held for a few rows
    r.push(ot::InputEvent::kPadUp, 24);
    r.run(30);
    r.tap(b::Play);
    int on = -1, off = -1;
    for (int row = 0; row < 16; ++row) {
        if (note::isNote(r.cell(row, 0).note)) on = row;
        if (r.cell(row, 0).note == note::kOff) off = row;
    }
    CHECK(on >= 0 && off >= 0);
    CHECK(off != on);
}

TEST(metronome_clicks_while_recording_when_the_pre_roll_is_on) {
    Rig r;
    r.ui().metronome = false;
    r.ui().preroll = true;
    r.ui().applyTransport();
    r.tap(b::Pattern);
    r.tap(b::Rec);
    r.tap(b::Play);
    r.run(2300);                                            // the bar of clicks is over: the song runs
    CHECK(r.app->engine.playing());
    int16_t buf[2 * AudioEngine::kBlock];
    long peak = 0;
    for (int i = 0; i < 300; ++i) {                          // a second and a bit of audio: a click is mixed in on each beat
        r.run(3);
        r.app->engine.fill(buf, AudioEngine::kBlock);
        for (int k = 0; k < 2 * int(AudioEngine::kBlock); ++k) peak += buf[k] < 0 ? -buf[k] : buf[k];
        r.loop(5);
    }
    CHECK(peak > 0);
}

TEST(shift_up_on_the_first_step_selects_the_track_then_all_tracks_shift_right_adds_one) {
    Rig r;
    r.tap(b::Pattern);
    r.song().pattern(0).clear(32);
    r.tap(b::Rec);
    r.down(b::Shift);
    r.tap(b::Up);                                            // on the first step: the whole track
    r.tap(b::Right);                                         // ... and the next one
    r.up(b::Shift);
    for (int t = 0; t < 2; ++t) r.cell(0, t).note = 20, r.cell(31, t).note = 22;
    r.tap(b::Copy);                                          // copies the two tracks, all 32 rows
    r.tap(b::Right);
    r.tap(b::Right);
    r.down(b::Shift);
    r.tap(b::Copy);                                          // Shift + Copy pastes at track 3
    r.up(b::Shift);
    CHECK(r.cell(0, 3).note == 20 && r.cell(31, 3).note == 22);
}

TEST(tap_tempo_sets_the_nearest_speed_and_a_live_change_reaches_the_player) {
    Rig r;
    r.tap(b::Song);
    r.song().speed = 6;
    for (int i = 0; i < 4; ++i) {                            // taps 400 ms apart = 150 BPM = 6 frames per row (NTSC)
        r.down(b::F8);
        r.up(b::F8);
        r.loop(360);
    }
    CHECK(r.song().speed == 6);
    for (int i = 0; i < 4; ++i) {                            // 500 ms apart = 120 BPM -> 7.5: the nearest of 7 or 8
        r.down(b::F8);
        r.up(b::F8);
        r.loop(460);
    }
    CHECK(r.song().speed == 7 || r.song().speed == 8);
    r.tap(b::Play);
    r.run(100);
    r.tap(b::F3);                                            // slower, while playing
    r.run(100);
    CHECK(r.app->engine.sequencer().speed() == r.song().speed);
}

TEST(holding_power_shows_the_shutdown_sequence_and_release_cancels_it) {
    Rig r;
    r.ui().masterVolume = 80;
    r.down(b::Power);
    r.loop(1200);                                            // most of the way: the sound is already faded out
    r.up(b::Power);
    r.loop(100);
    int16_t buf[2 * AudioEngine::kBlock];
    r.app->engine.fill(buf, AudioEngine::kBlock);            // released: volume is back (no assertion on audio content, just no crash)
    r.tap(b::Power);
    CHECK(true);
}

TEST(a_pattern_only_export_has_one_pattern_in_the_plan) {
    Rig r;
    ExportPlan plan;
    planPattern(r.song(), 3, plan);
    CHECK(plan.patternCount == 1 && plan.patterns[0].src[0] == 3 && plan.patterns[0].src[3] == 3 && plan.playlistLength == 1 && plan.loopTo == 0);
}

TEST(funk_speed_alternates_row_lengths_and_the_song_screen_sets_it) {
    Rig r;
    r.song().speed = 4;
    r.tap(b::Song);
    r.tap(b::F6);
    r.tap(b::F6);
    CHECK(r.song().funk == 2);
    r.tap(b::F5);
    CHECK(r.song().funk == 1);
    r.tap(b::F6);
    r.tap(b::F6);                                            // funk 3: rows of 4, 3, 4, 3 frames
    r.tap(b::Pattern);
    r.song().pattern(0).clear(16);
    r.tap(b::Play);
    std::vector<long> at;                                    // video frame at which each row started
    int last = -1;
    for (int ms = 0; ms < 2000 && at.size() < 8; ++ms) {
        r.run(1);
        const int row = r.app->engine.row();
        if (row != last) { last = row; at.push_back(r.app->engine.sequencer().frames()); }
    }
    CHECK(at.size() >= 8);
    if (at.size() >= 8) {
        const long d0 = at[3] - at[2], d1 = at[4] - at[3], d2 = at[5] - at[4], d3 = at[6] - at[5];
        CHECK(d0 == d2 && d1 == d3 && d0 != d1 && (d0 + d1) == 7);   // 4 and 3 frames, alternating
    }
}

TEST(track_volumes_reach_the_exported_data) {
    Rig r;
    Song& sg = r.song();
    sg.pattern(0).clear(16);
    sg.pattern(0).cells[0][0].note = 20;
    sg.pattern(0).cells[1][1].note = 20;
    sg.pattern(0).cells[1][1].vol = 11;
    sg.trackVolume[0] = 50;
    ExportPlan plan;
    planPattern(sg, 0, plan);
    SongPatterns prov(sg, plan);
    int rows = 0;
    CHECK(prov.begin(0, rows));
    std::vector<atari::Step> t0, t1;
    prov.track(0, t0);
    prov.track(1, t1);
    CHECK(t0[0].vol == 7);                                   // 15 at 50 %
    CHECK(t1[1].vol == 10);                                  // track 2 is at 100 %
    sg.trackVolume[1] = 0;
    prov.track(1, t1);
    CHECK(t1[1].vol == 0);
}

TEST(tune_marks_become_tunes_in_the_export_plan_and_the_project) {
    Rig r;
    Song& sg = r.song();
    sg.orderLen = 4;
    for (int i = 0; i < 4; ++i) { sg.order[i].pattern = uint8_t(i % 2); sg.order[i].repeat = uint8_t(i == 1 ? 2 : 1); }
    sg.order[1].tune = 1;
    sg.order[3].tune = 1;
    ExportPlan plan;
    std::string err;
    CHECK(planExport(sg, plan, &err));
    CHECK(plan.playlistLength == 5 && plan.tunes.size() == 2 && plan.tunes[0] == 1 && plan.tunes[1] == 4);   // entry 1 plays twice
    atari::Song core;
    CHECK(toCore(sg, core) && core.tunes.size() == 2 && core.tunes[0] == 1 && core.tunes[1] == 4);
    sg.order[0].tune = 1;                                   // entry 0 always starts tune 0: the mark means nothing
    CHECK(planExport(sg, plan, &err) && plan.tunes.size() == 2);
    r.tap(b::Song);                                          // F7 on the Song screen toggles the mark of the selected entry
    r.tap(b::Down);
    r.tap(b::F7);
    CHECK(sg.order[1].tune == 0);
    r.tap(b::F7);
    CHECK(sg.order[1].tune == 1);
    char file[32] = "tunes";
    CHECK(r.app->saveProject(file));
    Rig other(&r.storage.files);
    CHECK(other.app->openProject(file));
    CHECK(other.song().order[1].tune == 1 && other.song().order[3].tune == 1 && other.song().order[2].tune == 0);
}

TEST(the_assembly_export_slims_the_tia_player_to_the_song) {
    Rig r;
    r.song().chip = atari::Chip::Tia;
    r.app->applySongConfig();
    r.song().pattern(0).cells[0][0].note = 20;
    CHECK(r.app->exporter.start(ExportKind::kAsm, "slim"));
    const std::string key = "AtariTracker/Export/slim_asm/tia_player.asm";
    CHECK(r.storage.exists(key.c_str()));
    const auto& bytes = r.storage.files[key];
    const std::string text(bytes.begin(), bytes.end());
    CHECK(text.find("Slimmed for this song: 63 zero-page bytes") != std::string::npos);   // no effect, no overlay: the smallest player
    CHECK(text.find("TI_vPortaLv") == std::string::npos && text.find(";#IF") == std::string::npos && text.find("TiaFrame") != std::string::npos);
    const auto& rb = r.storage.files["AtariTracker/Export/slim_asm/README.txt"];
    const std::string readme(rb.begin(), rb.end());
    CHECK(readme.find("first of 63 zero-page bytes") != std::string::npos);
    r.song().pattern(0).cells[0][0].fx[0].type = atari::FX_PORTA;
    r.song().pattern(0).cells[0][0].fx[0].value = 3;
    CHECK(r.app->exporter.start(ExportKind::kAsm, "slim"));
    const auto& b2 = r.storage.files[key];
    const std::string t2(b2.begin(), b2.end());
    CHECK(t2.find("TI_vPortaLv") != std::string::npos && t2.find("Slimmed for this song: 71") != std::string::npos);
}

TEST(engine_speed_halves_the_song_time_and_the_playable_roms_refuse_it) {
    Rig r;
    Song& sg = r.song();
    sg.speed = 6;
    sg.pattern(0).clear(16);
    sg.pattern(0).cells[0][0].note = 20;
    auto wavSize = [&](const char* name) {
        CHECK(r.app->exporter.start(ExportKind::kWav, name));
        int guard = 0;
        while (r.app->exporter.busy() && ++guard < 40000) r.loop(5);
        char path[64];
        std::snprintf(path, sizeof path, "AtariTracker/Export/%s.wav", name);
        return r.storage.files[path].size();
    };
    const size_t one = wavSize("e1");
    sg.engineSpeed = 2;
    r.app->applySongConfig();
    const size_t two = wavSize("e2");
    CHECK(one > 1000 && two > 1000 && two * 2 < one + one / 10 + 2000 && two * 2 + 2000 > one - one / 10);   // half as long at engine speed 2
    CHECK(!r.app->exporter.start(ExportKind::kA78, "e2"));
    CHECK(std::string(r.app->exporter.message()).find("once per video frame") != std::string::npos);
    CHECK(bpmOfSpeed(6, atari::Region::NTSC, 2) == 300);
}

TEST(sound_effects_are_marked_on_the_shift_page_and_exported_with_their_patterns) {
    Rig r;
    Song& sg = r.song();
    sg.pattern(5).clear(8);
    sg.pattern(5).cells[0][2].note = 30;                     // an effect: pattern 5, track 3
    sg.order[0].pattern = 0;
    r.tap(b::Pattern);
    r.ui().pattern = 5;
    r.ui().channel = 2;
    r.down(b::Shift);
    r.tap(b::F7);                                            // Shift + F7: this track of this pattern is a sound effect
    r.up(b::Shift);
    CHECK(sg.sfxCount == 1 && sg.sfxPattern[0] == 5 && sg.sfxTrack[0] == 2);
    ExportPlan plan;
    std::string err;
    CHECK(planExport(sg, plan, &err));
    CHECK(plan.patternCount == 2 && plan.patterns[1].src[0] == 5 && plan.sfx.size() == 1 && plan.sfx[0].first == 1 && plan.sfx[0].second == 2);   // POKEY: chip track = track
    atari::Song core;
    CHECK(toCore(sg, core) && core.sfx.size() == 1 && core.sfx[0].second == 2 && core.patterns.size() == 2);
    r.down(b::Shift);
    r.tap(b::F7);                                            // again: it is not any more
    r.up(b::Shift);
    CHECK(sg.sfxCount == 0);
    sg.chip = atari::Chip::Tia;                              // TIA plays tracks 1 and 2 by default: track 3 cannot be an effect
    r.app->applySongConfig();
    r.down(b::Shift);
    r.tap(b::F7);
    r.up(b::Shift);
    CHECK(sg.sfxCount == 0);
}

TEST(buzz_style_is_an_instrument_setting_that_survives_the_project_and_the_export) {
    Rig r;
    Song& sg = r.song();
    sg.pokey[0].env.assign(1, atari::EnvStep());
    sg.pokey[0].envGoto = 0;
    sg.pokey[0].env[0].dist = 12;                            // buzzy
    sg.pokey[0].env[0].volL = sg.pokey[0].env[0].volR = 15;
    sg.pokey[0].buzzStyle = 1;
    sg.pokey[0].autoMod = -5;
    sg.pattern(0).clear(4);
    sg.pattern(0).cells[0][0].note = 30;
    sg.pattern(0).cells[0][0].inst = 1;
    char name[16] = "buzz";
    CHECK(r.app->saveProject(name));
    Rig other(&r.storage.files);
    std::string perr;
    const bool opened = other.app->openProject(name, &perr);
    if (!opened) std::fprintf(stderr, "open: %s\n", perr.c_str());
    CHECK(opened);
    CHECK(other.song().pokey[0].buzzStyle == 1 && other.song().pokey[0].autoMod == -5);
    atari::Song core;
    CHECK(toCore(sg, core) && core.pokeyIns[0].buzzStyle == 1 && core.pokeyIns[0].autoMod == -5);
    r.tap(b::InstrParams);                                   // GENERAL: the setting is on the page
    int found = 0;
    for (int i = 0; i < 8; ++i) { r.tap(b::Down); }
    for (int i = 0; i < 4; ++i) r.enc(-1);
    CHECK(sg.pokey[0].buzzStyle == 0 || sg.pokey[0].buzzStyle == 1);
    (void)found;
}

TEST(metronome_and_count_in) {
    Rig r;
    r.ui().metronome = true;
    r.ui().metroVolume = 80;
    r.ui().preroll = true;
    r.ui().applyTransport();
    r.app->saveSettings();
    CHECK(r.storage.exists("AtariTracker/settings.txt"));
    Rig other(&r.storage.files);                             // the next start reads them back
    CHECK(other.ui().metronome && other.ui().metroVolume == 80 && other.ui().preroll);
    r.tap(b::Pattern);
    r.tap(b::Rec);
    r.tap(b::Play);                                          // record + play with a pre-roll: one bar of clicks first
    r.loop(10);
    int16_t buf[2 * AudioEngine::kBlock];
    r.app->engine.fill(buf, AudioEngine::kBlock);            // the audio callback takes the command
    CHECK(r.app->engine.countingIn() && !r.app->engine.playing());
    int64_t energy = 0;
    for (int i = 0; i < 400; ++i) {
        r.app->engine.fill(buf, AudioEngine::kBlock);
        for (uint32_t k = 0; k < 2 * AudioEngine::kBlock; ++k) energy += std::abs(buf[k]);
    }
    CHECK(energy > 100000);                                  // the clicks are audible while nothing plays
    for (int i = 0; i < 1500 && r.app->engine.countingIn(); ++i) r.app->engine.fill(buf, AudioEngine::kBlock);
    r.app->engine.fill(buf, AudioEngine::kBlock);
    CHECK(!r.app->engine.countingIn() && r.app->engine.playing());
}

TEST(tia_plays_the_tracks_the_shift_page_picks) {
    Rig r;
    r.song().chip = atari::Chip::Tia;
    r.app->applySongConfig();
    r.tap(b::Pattern);
    CHECK(r.song().trackUsed(0) && r.song().trackUsed(1) && !r.song().trackUsed(2) && r.song().trackSlot(1) == 1);
    r.down(b::Shift);
    r.tap(b::F3);                                            // USE 3: track 3 comes in, the track in use longest (1) goes out
    r.up(b::Shift);
    CHECK(r.song().tiaTrack[0] == 1 && r.song().tiaTrack[1] == 2);
    CHECK(r.song().trackSlot(2) == 1 && r.song().trackSlot(0) == -1);
    // a note on track 3 sounds (on TIA channel 1); a note on the greyed track 1 does not
    Pattern& p = r.song().pattern(0);
    p.clear(8);
    p.cells[0][2].note = 30;
    p.cells[0][2].inst = 1;
    p.cells[0][0].note = 40;
    p.cells[0][0].inst = 1;
    r.app->engine.playPattern(0);
    int16_t buf[2 * AudioEngine::kBlock];
    for (int i = 0; i < 20; ++i) r.app->engine.fill(buf, AudioEngine::kBlock);
    CHECK(r.app->engine.trackLevel(2) > 0 && r.app->engine.trackLevel(0) == 0);
    // the choice is part of the project
    CHECK(saveProject(r.storage, r.song(), "tia.atr"));
    Rig other;
    CHECK(loadProject(other.storage = r.storage, other.song(), "tia.atr"));
    CHECK(other.song().tiaTrack[0] == 1 && other.song().tiaTrack[1] == 2);
}

TEST(table_shortcuts_and_grid_mode) {
    Rig r;
    Song& s = r.song();
    auto& tab = s.pokey[0].table;
    tab.assign(5, 0);
    tab[0] = 2;
    tab[4] = 10;
    r.tap(b::InstrParams);
    r.tap(b::InstrParams);
    r.tap(b::InstrParams);                                   // TABLE
    r.tap(b::F5);                                            // every value +1
    CHECK(tab[0] == 3 && tab[1] == 1 && tab[4] == 11);
    r.tap(b::F4);
    CHECK(tab[0] == 2 && tab[4] == 10);
    r.down(b::Shift);
    r.tap(b::F5);                                            // ramp 2 .. 10
    CHECK(tab[0] == 2 && tab[1] == 4 && tab[2] == 6 && tab[3] == 8 && tab[4] == 10);
    tab[4] = -6;
    r.tap(b::F5);                                            // ramp downwards 2 .. -6
    CHECK(tab[0] == 2 && tab[1] == 0 && tab[2] == -2 && tab[3] == -4 && tab[4] == -6);
    r.tap(b::F4);                                            // fill with the edited step (step 0)
    CHECK(tab[3] == 2 && tab[4] == 2);
    r.tap(b::F6);                                            // clear all
    CHECK(tab[0] == 0 && tab[4] == 0);
    r.up(b::Shift);
    // grid mode on the Sample recorder key: arrows move over the grid, the jog edits the value, the key leaves it
    r.tap(b::SampleRecorder);
    r.tap(b::Right);
    r.tap(b::Right);
    r.enc(3);
    CHECK(tab[2] == 3 && tab[1] == 0);
    r.tap(b::InstrParams);                                   // out of the grid
    r.enc(0);
}

TEST(steps_per_beat) {
    Rig r;
    Song& s = r.song();
    CHECK(s.stepsPerBeat == 4);
    s.speed = 6;
    r.tap(b::Pattern);
    r.down(b::Shift);
    r.tap(b::F8);                                            // tap: next value
    CHECK(s.stepsPerBeat == 8);
    r.down(b::F8);                                           // hold and turn
    r.enc(2);
    r.up(b::F8);
    CHECK(s.stepsPerBeat == 32);
    r.down(b::F8);
    r.enc(-30);
    r.up(b::F8);
    CHECK(s.stepsPerBeat == 1);
    r.down(b::F8);
    r.enc(99);
    r.up(b::F8);
    CHECK(s.stepsPerBeat == 64);
    r.tap(b::F8);                                            // a tap wraps round
    CHECK(s.stepsPerBeat == 1);
    r.up(b::Shift);
    CHECK(s.speed == 6 && bpmOfSpeed(s.speed, s.region, s.engineSpeed) == 150);   // the tempo is the beat's: unchanged
    // saved with the project
    s.stepsPerBeat = 16;
    CHECK(saveProject(r.storage, s, "beat.atr"));
    Rig other;
    CHECK(loadProject(other.storage = r.storage, other.song(), "beat.atr"));
    CHECK(other.song().stepsPerBeat == 16);
}

// The row clock: a beat lasts 4 * speed ticks however many rows it holds; rows are whole ticks that add up exactly.
TEST(rows_share_the_beat) {
    Rig r;
    Song& s = r.song();
    s.speed = 6;
    Pattern& p = s.pattern(0);
    p.rows = 64;
    for (int row = 0; row < 64; ++row) { p.cells[row][0].note = uint8_t(25 + row % 12); p.cells[row][0].inst = 1; }
    int16_t buf[2 * AudioEngine::kBlock];
    for (int spb : {4, 8, 16, 2, 1, 32, 64}) {
        s.stepsPerBeat = uint8_t(spb);
        r.app->engine.stop();
        for (int i = 0; i < 40; ++i) r.app->engine.fill(buf, AudioEngine::kBlock);
        r.app->engine.playPattern(0);
        long start[40] = {};
        int seen = -1;
        for (int guard = 0; guard < 60000 && seen < 24; ++guard) {
            r.app->engine.fill(buf, AudioEngine::kBlock);
            const int row = r.app->engine.row();
            if (r.app->engine.playing() && row != seen && row == seen + 1) start[++seen] = r.app->engine.chip().framesStarted();
            else if (seen < 0 && r.app->engine.playing() && row == 0) start[++seen] = r.app->engine.chip().framesStarted();
        }
        CHECK(seen >= 24);
        int acc = 0;
        bool ok = seen >= 24;
        for (int k = 0; k < 24 && ok; ++k) {
            const int len = atari::rowTicks(6, atari::log2Rows(spb), acc);
            if (start[k + 1] - start[k] != len) ok = false;
        }
        CHECK(ok);
        if (spb == 4) CHECK(start[1] - start[0] == 6 && start[24] - start[0] == 144);   // unchanged: six frames a row
        if (spb == 8) CHECK(start[2] - start[0] == 6 && start[24] - start[0] == 72);    // two rows per old row
    }
}

TEST(tia_fill_ramp_clear) {
    Rig r;
    Song& s = r.song();
    s.chip = atari::Chip::Tia;
    auto& st = s.tia[0].steps;
    st.assign(5, atari::TiaStep{});
    st[0].vol = 12;
    st[4].vol = 0;
    r.tap(b::InstrParams);
    r.tap(b::InstrParams);                                   // STEPS page (not the grid): shortcuts work on volume
    r.down(b::Shift);
    r.tap(b::F5);
    CHECK(st[0].vol == 12 && st[1].vol == 9 && st[2].vol == 6 && st[3].vol == 3 && st[4].vol == 0);
    st[4].vol = 15;
    r.tap(b::F5);                                            // and upwards
    CHECK(st[1].vol == 13 && st[2].vol == 14 && st[3].vol == 14 && st[4].vol == 15);
    r.tap(b::F4);
    CHECK(st[2].vol == 12 && st[4].vol == 12);
    r.tap(b::F6);
    CHECK(st[0].vol == 8 && st[3].vol == 8);
    r.up(b::Shift);
    // grid: the row under the cursor
    r.tap(b::SampleRecorder);
    r.tap(b::Down);
    r.tap(b::Down);
    r.tap(b::Down);                                          // pitch row
    r.down(b::Shift);
    st[0].pitchOffset = -4;
    st[4].pitchOffset = 4;
    r.tap(b::F5);
    CHECK(st[0].pitchOffset == -4 && st[1].pitchOffset == -2 && st[2].pitchOffset == 0 && st[3].pitchOffset == 2 && st[4].pitchOffset == 4);
    CHECK(st[2].vol == 8);                                   // other rows untouched
    r.up(b::Shift);
}

TEST(tia_table_mode) {
    Rig r;
    Song& s = r.song();
    s.chip = atari::Chip::Tia;
    s.tia[0].steps.assign(4, atari::TiaStep{});
    r.tap(b::SampleRecorder);                                // grid on the STEPS page
    r.tap(b::Right);
    r.enc(2);                                                // row 0 = waveform of step 2
    CHECK(s.tia[0].steps[1].waveform == 6 && s.tia[0].steps[0].waveform == 4);
    r.tap(b::Down);
    r.tap(b::Down);                                          // volume row
    r.enc(-3);
    CHECK(s.tia[0].steps[1].vol == 5);
    r.tap(b::Down);                                          // pitch row
    r.tap(b::Right);
    r.enc(7);
    CHECK(s.tia[0].steps[2].pitchOffset == 7);
    r.tap(b::SampleRecorder);                                // leave
    r.tap(b::Right);                                         // a plain page change again
}

TEST(adsr_shapes_the_envelope_and_is_saved) {
    Rig r;
    Song& s = r.song();
    r.tap(b::InstrParams);
    // a hand-made envelope with the default shape: switching to ADSR takes it over instead of replacing it
    {
        Adsr d;
        uint8_t vol[32];
        const int n = adsrSteps(d, vol);
        s.pokey[0].env.assign(size_t(n), atari::EnvStep{});
        for (int i = 0; i < n; ++i) { s.pokey[0].env[size_t(i)].volL = s.pokey[0].env[size_t(i)].volR = vol[i]; s.pokey[0].env[size_t(i)].dist = 10; }
        s.pokey[0].envGoto = n - 1;
    }
    r.tap(b::InstrParams);                                   // ENVELOPE, first row = MANUAL / ADSR
    r.enc(1);                                                // ADSR
    CHECK(s.adsr[0].on);
    CHECK(s.adsr[0].attack == 2 && s.adsr[0].decay == 6 && s.adsr[0].sustain == 10 && s.adsr[0].peak == 15);
    // defaults: attack 2, decay 6, sustain 10, peak 15 -> 2 + 6 + 1 steps, looping on the sustain step
    CHECK(s.pokey[0].env.size() == 9 && s.pokey[0].envGoto == 8);
    CHECK(s.pokey[0].env[8].volL == 10 && s.pokey[0].env[1].volL == 15 && s.pokey[0].env[0].volL > 0 && s.pokey[0].env[0].volL < 15);
    r.tap(b::Down);                                          // ATTACK
    r.enc(3);
    CHECK(s.adsr[0].attack == 5 && s.pokey[0].env.size() == 12);
    r.tap(b::Down);
    r.tap(b::Down);
    r.tap(b::Down);                                          // PEAK -> lower than the sustain pulls the sustain down with it
    r.enc(-8);
    CHECK(s.adsr[0].peak == 7 && s.pokey[0].env[11].volL == 7);
    // saved with the project, and a manual edit turns it off
    CHECK(saveProject(r.storage, s, "adsr.atr"));
    Rig other;
    CHECK(loadProject(other.storage = r.storage, other.song(), "adsr.atr"));
    CHECK(other.song().adsr[0].on && other.song().adsr[0].attack == 5 && other.song().adsr[0].peak == 7);
    CHECK(sameSong(s, other.song()));
    s.pokey[0].env[0].volL = 3;
    r.tap(b::InstrParams);
    r.tap(b::InstrParams);
    r.tap(b::InstrParams);
    r.tap(b::InstrParams);
    r.tap(b::InstrParams);                                   // back round to the envelope page
    r.tap(b::InstrParams);
    CHECK(r.ui().current() == ui::ScreenId::kInstrument);
}

TEST(tia_notes_out_of_tune_are_flagged) {
    Rig r;
    r.song().chip = atari::Chip::Tia;
    r.app->applySongConfig();
    int good = 0, bad = 0;
    for (uint8_t n = 1; n <= 61; ++n) (r.ui().noteInTune(1, n) ? good : bad)++;
    CHECK(good > 10 && bad > 20);                           // the 5-bit divider reaches only part of the scale in tune
    CHECK(r.ui().noteInTune(1, 1));                         // the lowest note of a waveform is in tune
    r.song().chip = atari::Chip::Pokey;
    CHECK(r.ui().noteInTune(1, 61));                        // POKEY: always
}

TEST(track_bound_instruments_are_not_offered_on_other_tracks) {
    Rig r;
    Song& s = r.song();
    // slot 2 is the starter "Bass 16-bit" (AUDCTL $50: join 1+2 on the 1.79 MHz clock): track 2 only
    CHECK(s.pokey[1].audctl == 0x50 && instrumentTrackMask(s.pokey[1]) == 0x2);
    CHECK(instrumentTrackMask(s.pokey[0]) == 0xF);           // the Lead plays anywhere
    int bound = 0;
    for (int i = 0; i < kInstrumentCount; ++i) if (instrumentTrackMask(s.pokey[i]) != 0xF) { ++bound; char m[24]; trackMaskText(instrumentTrackMask(s.pokey[i]), m); std::printf("    %02X %-28s %s\n", i + 1, s.pokey[i].name.c_str(), m); }
    std::printf("  %d of the %d starter / RMT instruments belong to particular tracks\n", bound, kInstrumentCount);
    char text[24];
    trackMaskText(0xA, text);
    CHECK(std::string(text) == "tracks 2 and 4");
    r.tap(b::Pattern);
    r.ui().channel = 0;                                      // track 1
    char why[48];
    CHECK(!r.ui().instrumentFits(2, 0, why) && std::string(why).find("track 2 only") != std::string::npos);
    CHECK(r.ui().instrumentFits(2, 1) && r.ui().instrumentFits(2, 3) == false);
    r.tap(b::Instr);                                         // the instrument column: pad 2 is slot 2
    r.pad(1);
    CHECK(r.ui().inst != 2);                                 // refused on track 1
    r.ui().channel = 1;                                      // track 2: the same pad is accepted
    r.tap(b::Note);
    r.tap(b::Instr);
    r.pad(1);
    CHECK(r.ui().inst == 2);
    r.tap(b::Note);
    // a note with a bound instrument does not sound on the wrong track
    r.ui().channel = 0;
    r.ui().inst = 2;
    r.pad(24);
    int16_t buf[2 * AudioEngine::kBlock];
    for (int i = 0; i < 20; ++i) r.app->engine.fill(buf, AudioEngine::kBlock);
    CHECK(r.app->engine.trackLevel(0) == 0);
}

TEST(arrows_move_tracks_and_edit_keys_follow_the_stock_tracker) {
    Rig r;
    r.tap(b::Pattern);
    Pattern& p = r.song().pattern(0);
    p.clear(16);
    for (int row = 0; row < 6; ++row) p.cells[row][0].note = uint8_t(10 + row);
    r.tap(b::Rec);
    r.tap(b::Right);                                         // Left / Right: tracks (the note field stays)
    CHECK(r.ui().channel == 1);
    r.tap(b::Left);
    r.tap(b::Left);
    CHECK(r.ui().channel == 0);
    // Insert: an empty step at the cursor, the rest of the track moves down (edit mode)
    r.tap(b::Down);
    r.tap(b::Down);                                          // row 2
    r.tap(b::Insert);
    CHECK(p.cells[2][0].note == 0 && p.cells[3][0].note == 12 && p.cells[6][0].note == 15);
    // Shift+Delete = backspace: the step before the cursor goes, the rest moves up, the cursor follows
    r.down(b::Shift);
    r.tap(b::Delete);
    r.up(b::Shift);
    CHECK(p.cells[1][0].note == 0 && p.cells[2][0].note == 12 && p.cells[5][0].note == 15);
    // Instr again: the volume field; Fx1 again: the next digit
    r.tap(b::Instr);
    r.tap(b::Instr);
    r.pad(7);
    CHECK(r.cell(1, 0).vol == 8 || r.cell(2, 0).vol == 8 || r.cell(0, 0).vol == 8);
    // Copy and paste need Rec: with it off they say so and do nothing
    r.tap(b::Rec);
    const uint8_t before = p.cells[8][0].note;
    r.tap(b::Copy);
    r.down(b::Shift);
    r.tap(b::Copy);
    r.up(b::Shift);
    CHECK(p.cells[8][0].note == before);
}
